#include <assert.h>
#include <errno.h>
#include <gbm.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "buffer.h"
#include "drm.h"
#include "log.h"
#include "render.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

struct drm_buffer {
	void (*destroy)(struct drm_buffer *buffer);
	struct drm *drm;
	uint32_t fb_id;
};

struct prop_map {
	const char *name;
	uint32_t *prop;
};

static void
_fetch_props(int fd, uint32_t obj, uint32_t type, const struct prop_map map[]) {
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	if (!props) {
		log("Failed to fetch props for object %u", obj);
		return;
	}
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			continue;
		}
		for (const struct prop_map *p = map; p->name; p++) {
			if (!strcmp(p->name, prop->name)) {
				*p->prop = prop->prop_id;
				break;
			}
		}
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
	for (const struct prop_map *p = map; p->name; p++) {
		if (*p->prop == 0) {
			log("Failed to find property '%s' for object %u", p->name, obj);
		}
	}
}

static bool
plane_is_primary(int fd, uint32_t plane_id)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props) {
		log("Failed to fetch plane type");
		return false;
	}
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (!strcmp(prop->name, "type")) {
			bool is_primary = false;
			for (uint32_t v = 0; v < prop->count_values; v++) {
				if (prop->values[v] == DRM_PLANE_TYPE_PRIMARY) {
					is_primary = true;
					break;
				}
			}
			drmModeFreeProperty(prop);
			drmModeFreeObjectProperties(props);
			return is_primary;
		}
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
	return false;
}


static void
drm_output_get_props(struct drm_output *output)
{
	_fetch_props(output->drm->fd, output->connector_id,
		DRM_MODE_OBJECT_CONNECTOR, (const struct prop_map[]) {
			{ "CRTC_ID", &output->props.connector.crtc },
			{ NULL, NULL },
		}
	);
	_fetch_props(output->drm->fd, output->crtc_id,
		DRM_MODE_OBJECT_CRTC, (const struct prop_map[]) {
			{ "MODE_ID", &output->props.crtc.mode },
			{ "ACTIVE",  &output->props.crtc.active },
			{ NULL, NULL },
		}
	);
	_fetch_props(output->drm->fd, output->plane_id,
		DRM_MODE_OBJECT_PLANE, (const struct prop_map[]) {
			{ "FB_ID",   &output->props.plane.fb },
			{ "CRTC_ID", &output->props.plane.crtc },
			{ "CRTC_X",  &output->props.plane.crtc_x },
			{ "CRTC_Y",  &output->props.plane.crtc_y },
			{ "CRTC_W",  &output->props.plane.crtc_w },
			{ "CRTC_H",  &output->props.plane.crtc_h },
			{ "SRC_X",   &output->props.plane.src_x },
			{ "SRC_Y",   &output->props.plane.src_y },
			{ "SRC_W",   &output->props.plane.src_w },
			{ "SRC_H",   &output->props.plane.src_h },
			{ NULL, NULL },
		}
	);
}

static bool
find_crtc_encoder_plane_for_connector(struct drm_output *output, drmModeRes *res, drmModeConnector *connector, uint32_t *taken_crtcs)
{
	// TODO: this may reuse planes across connectors
	int drm_fd = output->drm->fd;
	for (int e = 0; e < connector->count_encoders; e++) {
		drmModeEncoder *encoder = drmModeGetEncoder(drm_fd, connector->encoders[e]);
		if (!encoder) {
			continue;
		}
		for (int c = 0; c < res->count_crtcs; c++) {
			uint32_t bit = 1u << c;
			if (!(encoder->possible_crtcs & bit)) {
				continue;
			}
			if (*taken_crtcs & bit) {
				continue;
			}
			drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm_fd);
			for (int p = 0; p < plane_res->count_planes; p++) {
				drmModePlane *plane = drmModeGetPlane(drm_fd, plane_res->planes[p]);
				if (!plane) {
					continue;
				}
				if (!(plane->possible_crtcs & bit)) {
					drmModeFreePlane(plane);
					continue;
				}
				if (plane_is_primary(drm_fd, plane->plane_id)) {
					output->plane_id = plane->plane_id;
				}
				/* TODO: add further checks for formats, modifiers and so on */
				drmModeFreePlane(plane);
				if (output->plane_id) {
					break;
				}
			}
			drmModeFreePlaneResources(plane_res);
			if (output->plane_id) {
				output->encoder_id = encoder->encoder_id;
				output->crtc_id = res->crtcs[c];
				*taken_crtcs |= bit;
			}
			drmModeFreeEncoder(encoder);
			return output->plane_id != 0;
		}
		drmModeFreeEncoder(encoder);
	}
	return false;
}

static void
drm_buffer_destroy(struct drm_buffer *drm_buffer)
{
	if (drm_buffer->fb_id) {
		drmModeCloseFB(drm_buffer->drm->fd, drm_buffer->fb_id);
	}
	free(drm_buffer);
}

static struct drm_buffer *
drm_import_base_buffer(struct drm *drm, struct base_buffer *buffer)
{
	if (!(buffer->caps & BASE_ALLOCATOR_CAP_EXPORT_DMABUF)) {
		log("Can't import non dmabuf buffer into DRM. for now.");
		return NULL;
	}

	uint32_t handle;
	if (drmPrimeFDToHandle(drm->fd, buffer->get_fd(buffer), &handle) != 0) {
		perror("Failed to get handle for dmabuf");
		return NULL;
	}

	struct drm_buffer *drm_buffer = calloc(1, sizeof(*drm_buffer));
	assert(drm_buffer);

	*drm_buffer = (struct drm_buffer) {
		.destroy = drm_buffer_destroy,
		.drm = drm,
	};
	uint32_t handles[4] = { handle };
	uint32_t strides[4] = { buffer->stride };
	uint32_t offsets[4] = { 0 };
	uint64_t modifiers[4] = { buffer->modifier };
	if (drmModeAddFB2WithModifiers(drm->fd, buffer->width, buffer->height, buffer->fourcc,
		handles, strides, offsets, modifiers, &drm_buffer->fb_id, 0))
	{
		perror("importing base buffer into drm failed");
		free(drm_buffer);
		return NULL;
	}

	log("Imported base_buffer %p into drm", drm_buffer);
	return drm_buffer;
}

void
cb_base_buffer_destroy(struct base_buffer *buffer, void *key, void *value)
{
	struct drm_buffer *drm_buffer = value;
	drm_buffer->destroy(drm_buffer);
}

static bool
drm_output_set_mode(struct drm_output *output, struct drm_output_mode *mode, struct base_buffer *buffer)
{
	bool ret = false;
	int fd = output->drm->fd;

	uint32_t mode_blob_id = 0;
	if (drmModeCreatePropertyBlob(fd, &mode->mode, sizeof(mode->mode), &mode_blob_id)) {
		perror("drmModeCreatePropertyBlob");
		return false;
	}

	struct drm_buffer *drm_buffer = buffer->get_attachment(buffer, output->drm);
	if (!drm_buffer) {
		drm_buffer = drm_import_base_buffer(output->drm, buffer);
		if (!drm_buffer) {
			return false;
		}
		buffer->set_attachment(buffer, output->drm, drm_buffer, cb_base_buffer_destroy);
	}

	drmModeAtomicReq *req = drmModeAtomicAlloc();
	if (!req) {
		log("drmModeAtomicAlloc failed");
		drmModeDestroyPropertyBlob(fd, mode_blob_id);
		return false;
	}

	uint32_t width = mode->mode.hdisplay;
	uint32_t height = mode->mode.vdisplay;

	/* SRC values are 16.16 fixed point */
	uint64_t src_w = ((uint64_t)buffer->width) << 16;
	uint64_t src_h = ((uint64_t)buffer->height) << 16;

	/* Connector */
	drmModeAtomicAddProperty(req, output->connector_id, output->props.connector.crtc, output->crtc_id);

	/* CRTC */
	drmModeAtomicAddProperty(req, output->crtc_id, output->props.crtc.mode, mode_blob_id);
	drmModeAtomicAddProperty(req, output->crtc_id, output->props.crtc.active, 1);

	/* Plane */
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.crtc, output->crtc_id);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.fb, drm_buffer->fb_id);

	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.src_x, 0);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.src_y, 0);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.src_w, src_w);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.src_h, src_h);

	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.crtc_x, 0);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.crtc_y, 0);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.crtc_w, width);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.crtc_h, height);

	uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
	flags |= DRM_MODE_PAGE_FLIP_EVENT;
	if (drmModeAtomicCommit(fd, req, flags, output) != 0) {
		perror("drmModeAtomicCommit");
		goto out;
	}
	ret = true;
out:
	drmModeAtomicFree(req);
	if (mode_blob_id) {
		drmModeDestroyPropertyBlob(fd, mode_blob_id);
	}
	return ret;
}

static bool
_do_commit(struct drm_output *output, uint32_t fb_id, bool block)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	assert(req);
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.fb, fb_id);

	bool ret = false;
	uint32_t flags = 0;
	if (!block) {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
		flags |= DRM_MODE_PAGE_FLIP_EVENT;
	}
	if (drmModeAtomicCommit(output->drm->fd, req, flags, output) != 0) {
		perror("drmModeAtomicCommit");
		goto out;
	}
	ret = true;
out:
	drmModeAtomicFree(req);
	return ret;
}

static bool
drm_output_set_buffer(struct drm_output *output, struct base_buffer *buffer, bool block)
{
	struct drm_buffer *drm_buffer = buffer->get_attachment(buffer, output->drm);
	if (!drm_buffer) {
		drm_buffer = drm_import_base_buffer(output->drm, buffer);
		if (!drm_buffer) {
			return false;
		}
		buffer->set_attachment(buffer, output->drm, drm_buffer, cb_base_buffer_destroy);
	}

	if (block) {
		return _do_commit(output, drm_buffer->fb_id, block);
	}
	assert(!output->requested_pageflip_fb_id);

	// maybe try to commit with NONBLOCK first?
	output->requested_pageflip_fb_id = drm_buffer->fb_id;
	return true;
}

static void
drm_output_destroy(struct drm_output *output)
{
	struct drm_output_mode *mode, *tmp;
	wl_list_for_each_safe(mode, tmp, &output->modes, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}
	free(output);
}

static struct drm_output *
connector_init(struct drm *drm, drmModeRes *res, drmModeConnector *connector, uint32_t *taken_crtcs)
{
	if (connector->connection != DRM_MODE_CONNECTED) {
		return NULL;
	}
	if (!connector->count_modes) {
		log("Connector %s doesn't have any modes", "TODO-figure-out-name");
		return NULL;
	}

	struct drm_output *output = calloc(1, sizeof(*output));
	assert(output);
	output->drm = drm;
	output->set_mode = drm_output_set_mode;
	output->set_buffer = drm_output_set_buffer;
	output->connector_id = connector->connector_id;
	// TODO: fetch props
	wl_list_init(&output->modes);

	if (!find_crtc_encoder_plane_for_connector(output, res, connector, taken_crtcs)) {
		log("Failed to initialize output: could not find encoder, crtc or plane");
		drm_output_destroy(output);
		return NULL;
	}

	for (int i = 0; i < connector->count_modes; i++) {
		struct drm_output_mode *mode = calloc(1, sizeof(*mode));
		assert(mode);
		mode->mode = connector->modes[i];
		mode->width = mode->mode.hdisplay;
		mode->height = mode->mode.vdisplay;
		mode->refresh = mode->mode.vrefresh;
		mode->preferred = mode->mode.type & DRM_MODE_TYPE_PREFERRED;
		wl_list_insert(output->modes.prev, &mode->link);
	}

	drm_output_get_props(output);
	return output;
}

static bool
drm_discover(struct drm *drm)
{
	int fd = drm->fd;
	if (fd < 0) {
		log("Failed to init KMS: invalid fd %d", fd);
		return false;
	}

	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	drmModeRes *res = drmModeGetResources(fd);
	if (!res) {
		log("Failed to get drmModeResources");
		return false;
	}

	uint32_t taken_crtcs = 0;

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) {
			continue;
		}
		struct drm_output *output = connector_init(drm, res, conn, &taken_crtcs);
		if (output) {
			wl_list_insert(drm->outputs.prev, &output->link);
		}
		drmModeFreeConnector(conn);
	}

	struct drm_output *output;
	wl_list_for_each(output, &drm->outputs, link) {
		log("Got connector %u, crtc %u, encoder %u and plane %u",
			output->connector_id, output->crtc_id,
			output->encoder_id, output->plane_id
		);
	}

	drmModeFreeResources(res);
	return true;
}

static void
page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec,
		void *user_data)
{
	struct drm_output *output = user_data;
	assert(output);
	if (!output->requested_pageflip_fb_id) {
		log("Connector %u: no frame scheduled", output->connector_id);
		return;
	}
	/*
	 * blocking may cause delays in a multi output scenario, depending on the resolution
	 * not blocking causes tearing when cycling between 2 drm dumb buffers
	 *
	 * So we go with non-blocking and in case of using drm dumb buffers, use 3 per output
	 */
	static const bool block = false;
	if (!_do_commit(output, output->requested_pageflip_fb_id, block)) {
		perror("commit failed");
		return;
	}
	output->requested_pageflip_fb_id = 0;
	if (output->on_frame_presented) {
		output->on_frame_presented(output);
	}
}

static bool
drm_read_events(struct drm *drm, bool block)
{
	drmEventContext ev = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	struct pollfd fds[1] = {
		{ .fd = drm->fd, .events = POLLIN }
	};

	int timeout = block ? -1 : 0;
	int ret = poll(fds, ARRAY_SIZE(fds), timeout);
	if (ret < 0) {
		perror("drm poll failed");
		return false;
	} else if (!ret) {
		log("drm poll timeout");
		return false;
	} else if (ret == 1) {
		if (drmHandleEvent(drm->fd, &ev) != 0) {
			perror("drmHandleEvent failed");
			return false;
		}
		return true;
	}
	log("Unhandled drm poll response %d", ret);
	return false;
}

static void
drm_destroy(struct drm *drm)
{
	struct drm_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &drm->outputs, link) {
		wl_list_remove(&output->link);
		drm_output_destroy(output);
	}
	free(drm);
}

struct drm *
drm_create(int fd)
{
	struct drm *drm = calloc(1, sizeof(*drm));
	assert(drm);
	*drm = (struct drm) {
		.fd = fd,
		.read_events = drm_read_events,
		.destroy = drm_destroy,
	};
	wl_list_init(&drm->outputs);
	if (!drm_discover(drm)) {
		log("Failed to initialize drm");
		drm_destroy(drm);
		return NULL;
	}
	return drm;
}
