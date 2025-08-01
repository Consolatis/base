#pragma once

#include <stdbool.h>
#include <wayland-client.h>
//#include <wayland-util.h>

struct client;
struct client_handler {
	void (*registry)(struct client *client, void *data, struct wl_registry *registry,
		const char *iface_name, uint32_t global, uint32_t version);
	void (*initial_sync)(struct client *client, void *data);
	void (*disconnected)(struct client *client, void *data);
	void (*destroy)(struct client *client, void *data);
	void *data;
};

struct client {
	/* client functions */
	void (*add_handler)(struct client *client, struct client_handler handler);
	void (*connect)(struct client *client);
	void (*loop)(struct client *client);
	void (*terminate)(struct client *client);
	void (*destroy)(struct client *client);

	/* TODO: maybe rename to wayland_context or something? */
	struct client_state {
		struct wl_shm *wl_shm;
		struct wl_seat *wl_seat;
		struct xdg_wm_base *xdg_base;
		struct wl_display *wl_display;
		struct wl_registry *wl_registry;
		struct wl_compositor *wl_compositor;
		struct zwlr_layer_shell_v1 *layershell_manager;
		struct zxdg_decoration_manager_v1 *deco_manager;
		struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
	} state;

	struct shm_pool *shm_pool;
	struct seat *seat;

	/* Private */
	bool should_terminate;
	struct wl_array callbacks;
};

struct client *client_create(void);

// maybe move to seat.h?
struct surface;
struct seat {
	struct client *client;
	void (*register_surface)(struct seat *seat, struct surface *surface);
	void (*unregister_surface)(struct seat *seat, struct surface *surface);
	void (*pointer_set_shape)(struct seat *seat, uint32_t shape);
	// FIXME: destroy() missing, including wl_array cleanup
	/* Private */
	struct wl_pointer *pointer;
	struct wp_cursor_shape_device_v1 *pointer_shape;
	struct {
		struct surface *surface;
		uint32_t enter_serial;
	} focused_surface;
	struct wl_array surfaces;
};

struct seat *seat_create(struct client *client);

// move to buffer.h
struct buffer {
	/* Read only */
	int fd;
	bool busy;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t shm_format;
	struct shm_pool *pool;
	struct wl_buffer *buffer;
};
void renderer_shm_solid(struct buffer *buffer, uint32_t pixel_value);
void renderer_shm_solid_black(struct buffer *buffer);
void renderer_shm_checkerboard(struct buffer *buffer);

// move to renderers.h
void renderer_shm_solid(struct buffer *buffer, uint32_t pixel_value);
void renderer_shm_checkerboard(struct buffer *buffer);

struct surface;
struct surface_handler {
	void (*pointer_enter)(struct surface *surface, void *data, wl_fixed_t sx, wl_fixed_t sy);
	void (*pointer_motion)(struct surface *surface, void *data, wl_fixed_t sx, wl_fixed_t sy);
	void (*pointer_button)(struct surface *surface, void *data, uint32_t button, uint32_t state);
	void (*pointer_leave)(struct surface *surface, void *data);
	void *data;
};

// move to surface.h
struct renderer;
struct surface {
	struct client *client;
	struct wl_surface *surface;

	/* surface functions */
	void (*add_handler)(struct surface *surface, struct surface_handler handler);
	void (*set_buffer)(struct surface *surface, struct buffer *buffer);
	void (*set_render_func)(struct surface *surface, void (*render_func)(struct buffer *buffer));
	void (*request_frame)(struct surface *surface, void (*callback)(struct surface *surface, uint32_t time_ms, void *data), void *data);
	void (*render_frame)(struct surface *surface, uint32_t width, uint32_t height);
	void (*unmap)(struct surface *surface);
	void (*destroy)(struct surface *surface);

	void (*emit_pointer_enter)(struct surface *surface, wl_fixed_t sx, wl_fixed_t sy);
	void (*emit_pointer_motion)(struct surface *surface, wl_fixed_t sx, wl_fixed_t sy);
	void (*emit_pointer_button)(struct surface *surface, uint32_t button, uint32_t state);
	void (*emit_pointer_leave)(struct surface *surface);

	/* Private */
	struct {
		struct wl_callback *wl_callback;
		void (*user_callback)(struct surface *surface, uint32_t time_ms, void *data);
		void *data;
	} frame_callback;
	void (*render_func)(struct buffer *buffer); /* Defaults to shm_buffer_render_checkerboard */
	struct wl_array callbacks;
};

struct surface *surface_create(struct client *client);

// move to toplevel.h
struct toplevel;
struct toplevel_handler {
	void (*reconfigure)(struct toplevel *toplevel, void *data, int width, int height);
	void (*close)(struct toplevel *toplevel, void *data);
	void *data;
};

struct geometry {
	int width;
	int height;
};

struct toplevel {
	struct surface *surface;

	/* toplevel functions */
	void (*add_handler)(struct toplevel *toplevel, struct toplevel_handler handler);
	void (*decorate)(struct toplevel *toplevel);
	void (*set_title)(struct toplevel *toplevel, const char *title);
	void (*set_app_id)(struct toplevel *toplevel, const char *app_id);
	void (*destroy)(struct toplevel *toplevel);

	/* Private */
	bool configured;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *deco;
	struct geometry pending, current;
	struct wl_array callbacks;
};
struct toplevel *toplevel_create(struct client *client);
//struct toplevel *toplevel_create_from_surface(struct surface *surface);

// move to layershell.h
struct layershell;
struct layershell_handler {
	void (*reconfigure)(struct layershell *layershell, void *data, int width, int height);
	void *data;
};


struct layershell {
	struct surface *surface;

	/* layershell functions */
	void (*add_handler)(struct layershell *layershell, struct layershell_handler handler);
	void (*destroy)(struct layershell *layershell);

	/* Private */
	struct zwlr_layer_surface_v1 *layer_surface;
	struct geometry current;
	struct wl_array callbacks;
};

struct layershell *layershell_create(struct client *client, struct wl_output *output,
	uint32_t width, uint32_t height, uint32_t layer, uint32_t anchors);
//struct toplevel *layershell_create_from_surface(struct surface *surface);


struct shm_pool {
	struct buffer *(*get_buffer)(struct shm_pool *pool, uint32_t width, uint32_t height);
	/* Private */
	struct client *client;
	struct wl_array buffers;
};
struct shm_pool *shm_pool_create(struct client *client);
