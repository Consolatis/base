#pragma once

#include <wayland-util.h>

struct ext_capture_dmabuf_format {
	uint32_t fourcc;
	uint64_t modifier;
};

struct client;
struct base_buffer;
struct toplevel_handle;

struct ext_capture_session;
struct ext_capture_session_handler {
	void (*buffer_ready)(struct ext_capture_session *session, void *data, struct base_buffer *buffer);
	void *data;
};

typedef struct base_buffer *(*ext_capture_allocator_func_t)(struct ext_capture_session *session);

struct ext_capture_session {
	struct ext_capture_manager *manager;
	void (*add_handler)(struct ext_capture_session *session, struct ext_capture_session_handler handler);
	//void (*capture_frame)(struct ext_capture_session *session, struct buffer *buffer);
	struct toplevel_handle *toplevel; /* may be NULL */
	uint32_t width, height;
	struct {
		struct wl_array formats;
	} shm;
	struct {
		struct wl_array formats;
	} drm;
	void *data;
	//struct output *output; /* may be NULL */

	/* Private */
	struct wl_array shm_formats_tmp;
	struct wl_array drm_formats_tmp;
	ext_capture_allocator_func_t request_buffer;
	struct wl_array callbacks;
	struct wl_list link;
	struct ext_image_capture_source_v1 *source; // or just temporary?
	struct ext_image_copy_capture_session_v1 *session;
};


struct ext_capture_manager {
	struct client *client;
	struct ext_capture_session *(*capture_toplevel)(struct ext_capture_manager *manager, struct toplevel_handle *toplevel, ext_capture_allocator_func_t allocator);
	//struct ext_capture_session *(*capture_output)(struct ext_capture_manager *manager, struct output *output);
	struct wl_list sessions;
	/* Private */
	struct ext_image_copy_capture_manager_v1 *manager;
	struct ext_output_image_capture_source_manager_v1 *output_source_manager;
	struct ext_foreign_toplevel_image_capture_source_manager_v1 *toplevel_source_manager;
};

struct ext_capture_manager *ext_capture_manager_create(struct client *client);
