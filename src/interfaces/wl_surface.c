#include "base.h"
#include <assert.h>
#include <stdlib.h>

#include "cursor-shape-v1.xml.h"

#define SURFACE_CALLBACK(surface, name, ...) do {                \
	struct surface_handler *handler;                         \
	wl_array_for_each(handler, &(surface)->callbacks) {      \
		if (handler->name) {                             \
			handler->name((surface), handler->data,  \
				##__VA_ARGS__);                  \
		}                                                \
	}                                                        \
} while (0)

static void
surface_add_handler(struct surface *surface, struct surface_handler handler)
{
	struct surface_handler *data = wl_array_add(&surface->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

static void
surface_set_render_func(struct surface *surface, void (*render_func)(struct buffer *buffer))
{
	surface->render_func = render_func;
}

static void
surface_set_buffer(struct surface *surface, struct buffer *buffer)
{
	assert(surface->surface);
	assert(buffer->buffer);
	wl_surface_attach(surface->surface, buffer->buffer, 0, 0);
	wl_surface_damage_buffer(surface->surface, 0, 0, buffer->width, buffer->height);
	wl_surface_commit(surface->surface);
	surface->geometry.width = buffer->width;
	surface->geometry.height = buffer->height;
	wl_display_flush(surface->client->state.wl_display);
}

static void
frame_callback(void *data, struct wl_callback *wl_callback, uint32_t time_ms)
{
	struct surface *surface = data;
	assert(surface->frame_callback.wl_callback == wl_callback);
	wl_callback_destroy(wl_callback);
	surface->frame_callback.wl_callback = NULL;
	surface->frame_callback.user_callback(surface, time_ms, surface->frame_callback.data);
}

const struct wl_callback_listener frame_listener = {
	.done = frame_callback,
};

static void
surface_request_frame(struct surface *surface,
	void (*frame_callback)(struct surface *surface, uint32_t time_msec, void *data),
	void *data)
{
	if (surface->frame_callback.wl_callback) {
		return;
	}
	surface->frame_callback.user_callback = frame_callback;
	surface->frame_callback.data = data;
	surface->frame_callback.wl_callback = wl_surface_frame(surface->surface);
	wl_callback_add_listener(surface->frame_callback.wl_callback, &frame_listener, surface);
	wl_display_flush(surface->client->state.wl_display);
}

static void
surface_render_frame(struct surface *surface, uint32_t width, uint32_t height)
{
	struct shm_pool *pool = surface->client->shm_pool;
	struct buffer *buffer = pool->get_buffer(pool, width, height);
	surface->render_func(buffer);
	surface->set_buffer(surface, buffer);
}

static void
surface_destroy(struct surface *surface)
{
	struct seat *seat = surface->client->seat;
	if (seat) {
		seat->unregister_surface(seat, surface);
	}
	if (surface->frame_callback.wl_callback) {
		wl_callback_destroy(surface->frame_callback.wl_callback);
	}
	wl_surface_destroy(surface->surface);
	wl_array_release(&surface->callbacks);
	free(surface);
}

static void
surface_unmap(struct surface *surface)
{
	assert(surface->surface);
	wl_surface_attach(surface->surface, NULL, 0, 0);
	wl_surface_commit(surface->surface);
	wl_display_flush(surface->client->state.wl_display);
}

static void
surface_emit_pointer_enter(struct surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
	SURFACE_CALLBACK(surface, pointer_enter, sx, sy);

	/* Manually set the default cursor if there are no pointer_enter handlers installed */
	struct surface_handler *handler;
	wl_array_for_each(handler, &surface->callbacks) {
		if (handler->pointer_enter) {
			return;
		}
	}
	struct seat *seat = surface->client->seat;
	if (seat) {
		seat->pointer_set_shape(seat, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
	}
}

static void
surface_emit_pointer_motion(struct surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
	SURFACE_CALLBACK(surface, pointer_motion, sx, sy);
}

static void
surface_emit_pointer_button(struct surface *surface, uint32_t button, uint32_t state)
{
	SURFACE_CALLBACK(surface, pointer_button, button, state);
}

static void
surface_emit_pointer_axis(struct surface *surface, uint32_t axis, wl_fixed_t value)
{
	SURFACE_CALLBACK(surface, pointer_axis, axis, value);
}

static void
surface_emit_pointer_leave(struct surface *surface)
{
	SURFACE_CALLBACK(surface, pointer_leave);
}

struct surface *
surface_create(struct client *client)
{
	assert(client->state.wl_compositor);
	struct surface *surface = calloc(1, sizeof(*surface));
	assert(surface);
	wl_array_init(&surface->callbacks);
	surface->client = client;
	surface->add_handler = surface_add_handler;
	surface->set_buffer = surface_set_buffer;
	surface->request_frame = surface_request_frame;
	surface->set_render_func = surface_set_render_func;
	surface->render_frame = surface_render_frame;
	surface->unmap = surface_unmap;
	surface->destroy = surface_destroy;
	surface->emit_pointer_enter = surface_emit_pointer_enter;
	surface->emit_pointer_motion = surface_emit_pointer_motion;
	surface->emit_pointer_button = surface_emit_pointer_button;
	surface->emit_pointer_axis = surface_emit_pointer_axis;
	surface->emit_pointer_leave = surface_emit_pointer_leave;
	surface->surface = wl_compositor_create_surface(client->state.wl_compositor);
	surface->render_func = renderer_shm_checkerboard;
	if (client->seat) {
		client->seat->register_surface(client->seat, surface);
	}
	return surface;
}
