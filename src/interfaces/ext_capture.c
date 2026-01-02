#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "ext_capture.h"
#include "log.h"
#include "toplevel_handle.h"

#include "ext-image-copy-capture-v1.xml.h"
#include "ext-image-capture-source-v1.xml.h"

#define SESSION_CALLBACK(session, name, ...) do {                \
	struct ext_capture_session_handler *handler;             \
	wl_array_for_each(handler, &(session)->callbacks) {      \
		if (handler->name) {                             \
			handler->name((session), handler->data,  \
				##__VA_ARGS__);                  \
		}                                                \
	}                                                        \
} while (0)

struct frame {
	struct ext_capture_session *session;
	struct buffer *buffer;
};

static void _capture(struct ext_capture_session *session);

static void
frame_handle_transform(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t transform)
{
	//log("Got frame transform");
}

static void
frame_handle_damage(void *data, struct ext_image_copy_capture_frame_v1 *frame,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	// TODO: accumulate damage regions in pixman area
	//log("Got frame damage %dx%d@%d,%d", width, height, x, y);
}

static void
frame_handle_presentation_time(void *data, struct ext_image_copy_capture_frame_v1 *frame,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
	//log("Got frame presentation time");
}

static void
frame_handle_ready(void *data, struct ext_image_copy_capture_frame_v1 *frame)
{
	struct frame *capture_data = data;
	struct ext_capture_session *session = capture_data->session;
	ext_image_copy_capture_frame_v1_destroy(frame);

	capture_data->buffer->busy = true;
	SESSION_CALLBACK(session, buffer_ready, capture_data->buffer);
	free(capture_data);
	_capture(session);
}

static void
frame_handle_failed(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t reason)
{
	struct frame *capture_data = data;
	ext_image_copy_capture_frame_v1_destroy(frame);

	free(capture_data);
	log("Capturing frame failed: %u", reason);
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = frame_handle_transform,
	.damage = frame_handle_damage,
	.presentation_time = frame_handle_presentation_time,
	.ready = frame_handle_ready,
	.failed = frame_handle_failed,
};

static void
_capture(struct ext_capture_session *session) {
	struct buffer *buffer = session->request_buffer(session);
	if (!buffer) {
		log("no buffer received from caller");
		return;
	}
	struct ext_image_copy_capture_frame_v1 *frame =
		ext_image_copy_capture_session_v1_create_frame(session->session);
	struct frame *capture_data = calloc(1, sizeof(*capture_data));
	capture_data->session = session;
	capture_data->buffer = buffer;
	ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, capture_data);
	ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer->buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, buffer->width, buffer->height);
	ext_image_copy_capture_frame_v1_capture(frame);
}

static void
session_handle_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *session_handle,
		uint32_t width, uint32_t height)
{
	struct ext_capture_session *session = data;
	session->width = width;
	session->height = height;
}

static void
session_handle_shm_format(void *data, struct ext_image_copy_capture_session_v1 *session_handle,
		uint32_t format)
{
	struct ext_capture_session *session = data;
	uint32_t *entry = wl_array_add(&session->shm_formats_tmp, sizeof(*entry));
	assert(entry);
	*entry = format;
}

static void
session_handle_dmabuf_device(void *data, struct ext_image_copy_capture_session_v1 *session_handle,
		struct wl_array *device)
{
	//log("Got dmabuf device");
}

struct dmabuf_format {
	uint32_t fourcc;
	uint64_t modifier;
};

static void
session_handle_dmabuf_format(void *data, struct ext_image_copy_capture_session_v1 *session_handle,
		uint32_t format, struct wl_array *modifiers)
{
	struct ext_capture_session *session = data;
	uint64_t *modifier;
	wl_array_for_each(modifier, modifiers) {
		struct dmabuf_format *entry = wl_array_add(&session->drm_formats_tmp, sizeof(*entry));
		assert(entry);
		*entry = (struct dmabuf_format){ format, *modifier };
	}
}

static void
session_handle_done(void *data, struct ext_image_copy_capture_session_v1 *session_handle)
{
	struct ext_capture_session *session = data;
	assert(session->shm_formats_tmp.size);

	log("Session received done event")
	wl_array_release(&session->shm.formats);
	wl_array_init(&session->shm.formats);
	wl_array_copy(&session->shm.formats, &session->shm_formats_tmp);
	wl_array_release(&session->shm_formats_tmp);
	wl_array_init(&session->shm_formats_tmp);

	log("Supported SHM formats:");
	uint32_t *shm_format;
	wl_array_for_each(shm_format, &session->shm.formats) {
		log(" - 0x%x", *shm_format);
	}

	wl_array_release(&session->drm.formats);
	wl_array_init(&session->drm.formats);
	wl_array_copy(&session->drm.formats, &session->drm_formats_tmp);
	wl_array_release(&session->drm_formats_tmp);
	wl_array_init(&session->drm_formats_tmp);

	log("Supported DRM formats:");
	struct dmabuf_format *drm_format;
	wl_array_for_each(drm_format, &session->drm.formats) {
		log(" - 0x%08x (modifier 0x%016lx)", drm_format->fourcc, drm_format->modifier);
	}
	_capture(session);
}

static void
session_handle_stopped(void *data, struct ext_image_copy_capture_session_v1 *session_handle)
{
	log("Session stopped");
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
	.buffer_size = session_handle_buffer_size,
	.shm_format = session_handle_shm_format,
	.dmabuf_device = session_handle_dmabuf_device,
	.dmabuf_format = session_handle_dmabuf_format,
	.done = session_handle_done,
	.stopped = session_handle_stopped,
};

static void
session_add_handler(struct ext_capture_session *session, struct ext_capture_session_handler handler)
{
	struct ext_capture_session_handler *data = wl_array_add(&session->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

static struct ext_capture_session *
_create_session(struct ext_capture_manager *manager, struct ext_image_capture_source_v1 *src,
		struct buffer *(*allocator)(struct ext_capture_session *session))
{
	struct ext_capture_session *session = calloc(1, sizeof(*session));
	assert(session);
	wl_array_init(&session->shm_formats_tmp);
	wl_array_init(&session->shm.formats);
	wl_array_init(&session->drm_formats_tmp);
	wl_array_init(&session->drm.formats);
	wl_array_init(&session->callbacks);
	session->add_handler = session_add_handler;
	session->manager = manager;
	session->source = src;
	session->request_buffer = allocator;

	const bool paint_cursors = false;
	uint32_t session_options = 0;
	if (paint_cursors) {
		session_options |= EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS;
	}
	session->session = ext_image_copy_capture_manager_v1_create_session(
		manager->manager, session->source, session_options);
	ext_image_copy_capture_session_v1_add_listener(session->session, &session_listener, session);

	wl_list_insert(manager->sessions.prev, &session->link);
	return session;
}

static struct ext_capture_session *
manager_handle_capture_toplevel(struct ext_capture_manager *manager,
		struct toplevel_handle *toplevel, ext_capture_allocator_func_t allocator)
{
	struct ext_image_capture_source_v1 *src =
		ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
			manager->toplevel_source_manager, toplevel->handle);
	struct ext_capture_session *session = _create_session(manager, src, allocator);
	session->toplevel = toplevel;
	return session;
}

#if 0
static struct ext_capture_session *
manager_handle_capture_output(struct ext_capture_manager *manager, struct output *output)
{
	struct ext_image_capture_source_v1 *src =
		ext_output_image_capture_source_manager_v1_create_source(
			manager->output_source_manager, output->handle)
	return _create_session(manager, src);
}
#endif

static void
client_handle_global(struct client *client, void *data, struct wl_registry *registry,
		const char *iface_name, uint32_t global, uint32_t version)
{
	struct ext_capture_manager *manager = data;
	if (!manager->manager && !strcmp(iface_name, ext_image_copy_capture_manager_v1_interface.name)) {
		manager->manager = wl_registry_bind(registry, global,
			&ext_image_copy_capture_manager_v1_interface, version);
	}
	if (!manager->output_source_manager && !strcmp(iface_name, ext_output_image_capture_source_manager_v1_interface.name)) {
		manager->output_source_manager = wl_registry_bind(registry, global,
			&ext_output_image_capture_source_manager_v1_interface, version);
	}
	if (!manager->toplevel_source_manager && !strcmp(iface_name, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name)) {
		manager->toplevel_source_manager = wl_registry_bind(registry, global,
			&ext_foreign_toplevel_image_capture_source_manager_v1_interface, version);
	}
}

struct ext_capture_manager *
ext_capture_manager_create(struct client *client)
{
	struct ext_capture_manager *manager = calloc(1, sizeof(*manager));
	assert(manager);
	client->add_handler(client, (struct client_handler) {
		.registry = client_handle_global,
		.data = manager,
	});
	wl_list_init(&manager->sessions);
	manager->client = client;
	manager->capture_toplevel = manager_handle_capture_toplevel;
	return manager;
}
