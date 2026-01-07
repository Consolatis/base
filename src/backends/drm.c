#include "drm.h"
#include "log.h"
#include "render.h"
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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

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

static bool
drm_output_set_mode(struct drm_output *output, struct drm_output_mode *mode, struct drm_buffer *buffer)
{
	bool ret = false;
	int fd = output->drm->fd;

	uint32_t mode_blob_id = 0;
	if (drmModeCreatePropertyBlob(fd, &mode->mode, sizeof(mode->mode), &mode_blob_id)) {
		perror("drmModeCreatePropertyBlob");
		return false;
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
	drmModeAtomicAddProperty(req, output->plane_id, output->props.plane.fb, buffer->fb_id);

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
drm_output_set_buffer(struct drm_output *output, struct drm_buffer *buffer, bool block)
{
	if (block) {
		return _do_commit(output, buffer->fb_id, block);
	}
	assert(!output->requested_pageflip_fb_id);

	// maybe try to commit with NONBLOCK first?
	output->requested_pageflip_fb_id = buffer->fb_id;
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
drm_buffer_destroy(struct drm_buffer *buffer)
{
	if (buffer->fb_id) {
		drmModeCloseFB(buffer->drm->fd, buffer->fb_id);
	}

	switch(buffer->type) {
	case DRM_GBM_BUFFER:
		/* Called by the destruction handler of gbm_bo so nothing to do */
		break;
	case DRM_DUMB_BUFFER:
		// FIXME: properly unmap and destroy
		break;
	}
	free(buffer);
}

static struct drm_dumb_buffer *
create_dumb_buffer(struct drm *drm, uint32_t width, uint32_t height, uint32_t format)
{
	// FIXME:
	//        - Parse format and calculate bpp, maybe there is a fourcc helper?
	//        - unmap on error

	/* Create buffer */
	struct drm_dumb_buffer *buffer = calloc(1, sizeof(*buffer));
	assert(buffer);
	*buffer = (struct drm_dumb_buffer) {
		.base = {
			.drm = drm,
			.type = DRM_DUMB_BUFFER,
			.fourcc = format,
			.destroy = drm_buffer_destroy,
		},
		.req.width = width,
		.req.height = height,
		.req.bpp = 32,
	};
	if (ioctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &buffer->req) < 0) {
		perror("Failed to create dumb buffer");
		goto cleanup_buffer;
	}
	buffer->base.width = buffer->req.width;
	buffer->base.height = buffer->req.height;
	buffer->base.stride = buffer->req.pitch;

	/* Map into address space */
	// TODO: should we always keep it mapped or map before use and then unmap?
	//       would require ->map() and ->unmap() functions in that case
	struct drm_mode_map_dumb mreq = {
		.handle = buffer->req.handle,
	};
	if (ioctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		perror("Failed to import dumb buffer");
		goto cleanup_buffer;
	}
	buffer->pixels = mmap(NULL, buffer->req.size,
		PROT_READ | PROT_WRITE, MAP_SHARED, drm->fd, mreq.offset);
	if (buffer->pixels == MAP_FAILED) {
		perror("Failed to map dumb buffer");
		goto cleanup_buffer;
	}

	/* Create framebuffer */
	// TODO: pixel depth is hardcoded to 24
	if (drmModeAddFB(drm->fd, buffer->req.width, buffer->req.height, 24, buffer->req.bpp, buffer->req.pitch, buffer->req.handle, &buffer->base.fb_id)) {
		perror("Failed to create framebuffer for dumb buffer");
		goto cleanup_map;
	}

	return buffer;

cleanup_map:
	// FIXME

cleanup_buffer:
	free(buffer);

	return NULL;
}

static struct drm_buffer *
drm_import_gbm_bo(struct drm *drm, struct gbm_bo *bo)
{
	struct drm_buffer *buffer = calloc(1, sizeof(*buffer));
	assert(buffer);

	*buffer = (struct drm_buffer) {
		.type = DRM_GBM_BUFFER,
		.width = gbm_bo_get_width(bo),
		.height = gbm_bo_get_height(bo),
		.stride = gbm_bo_get_stride(bo),
		.fourcc = gbm_bo_get_format(bo),
		.destroy = drm_buffer_destroy,
		.drm = drm,
	};
	uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
	uint32_t strides[4] = { buffer->stride };
	uint32_t offsets[4] = { 0 };
	uint64_t modifiers[4] = { gbm_bo_get_modifier(bo) };
	if (drmModeAddFB2WithModifiers(drm->fd, buffer->width, buffer->height, buffer->fourcc,
		handles, strides, offsets, modifiers, &buffer->fb_id, 0))
	{
		perror("importing gbm buffer into drm failed");
		free(buffer);
		return NULL;
	}
	return buffer;
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
		.import_gbm_bo = drm_import_gbm_bo,
		.read_events = drm_read_events,
		.destroy = drm_destroy,
		.allocator.create_dumb_buffer = create_dumb_buffer,
	};
	wl_list_init(&drm->outputs);
	if (!drm_discover(drm)) {
		log("Failed to initialize drm");
		drm_destroy(drm);
		return NULL;
	}
	return drm;
}
