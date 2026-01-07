#include "base.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "cursor-shape-v1.xml.h"

static struct surface *
get_surface(struct seat *seat, struct wl_surface *wl_surface)
{
	struct surface **surface_ptr;
	wl_array_for_each(surface_ptr, &seat->surfaces) {
		struct surface *surface = *surface_ptr;
		if (surface && surface->surface == wl_surface) {
			return surface;
		}
	}
	return NULL;
}


static void
handle_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
		struct wl_surface *wl_surface, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct seat *seat = data;
	struct surface *surface = get_surface(seat, wl_surface);
	seat->focused_surface.surface = surface;
	seat->focused_surface.enter_serial = serial;
	if (surface) {
		surface->emit_pointer_enter(surface, surface_x, surface_y);
	}
}

static void
handle_pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *wl_surface)
{
	struct seat *seat = data;
	struct surface *surface = get_surface(seat, wl_surface);
	if (seat->focused_surface.surface == surface) {
		seat->focused_surface.surface = NULL;
		seat->focused_surface.enter_serial = 0;
	}
	if (surface) {
		surface->emit_pointer_leave(surface);
	}
}

static void
handle_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct seat *seat = data;
	struct surface *surface = seat->focused_surface.surface;
	if (surface) {
		surface->emit_pointer_motion(surface, surface_x, surface_y);
	}
}

static void
handle_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	struct seat *seat = data;
	struct surface *surface = seat->focused_surface.surface;
	if (surface) {
		surface->emit_pointer_button(surface, button, state);
	}
}

static void
handle_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value)
{
	struct seat *seat = data;
	struct surface *surface = seat->focused_surface.surface;
	if (surface) {
		surface->emit_pointer_axis(surface, axis, value);
	}
}

static void
handle_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
}

static void
handle_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source)
{
}

static void
handle_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis)
{
}

static void
handle_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete)
{
}

static void
handle_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t value120)
{
}

static void
handle_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, uint32_t direction)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = handle_pointer_enter,
	.leave = handle_pointer_leave,
	.motion = handle_pointer_motion,
	.button = handle_pointer_button,
	.axis = handle_pointer_axis,
	.frame = handle_pointer_frame,
	.axis_source = handle_pointer_axis_source,
	.axis_stop = handle_pointer_axis_stop,
	.axis_discrete = handle_pointer_axis_discrete,
	.axis_value120 = handle_pointer_axis_value120,
	.axis_relative_direction = handle_pointer_axis_relative_direction,
};

static void
seat_register_surface(struct seat *seat, struct surface *surface)
{
	/* TODO: fill NULL pointers */
	struct surface **surface_ptr;
	wl_array_for_each(surface_ptr, &seat->surfaces) {
		assert(*surface_ptr != surface);
	}
	surface_ptr = wl_array_add(&seat->surfaces, sizeof(*surface_ptr));
	assert(surface_ptr);
	*surface_ptr = surface;
}

static void
seat_unregister_surface(struct seat *seat, struct surface *surface)
{
	struct surface **surface_ptr;
	wl_array_for_each(surface_ptr, &seat->surfaces) {
		if (*surface_ptr == surface) {
			*surface_ptr = NULL;
			return;
		}
	}
	assert(false && "surface was never registered");
}

static void
seat_pointer_set_shape(struct seat *seat, uint32_t shape)
{
	if (!seat->pointer_shape) {
		return;
	}
	wp_cursor_shape_device_v1_set_shape(seat->pointer_shape,
		seat->focused_surface.enter_serial, shape);
}

struct seat *
seat_create(struct client *client)
{
	if (!client->state.wl_seat) {
		return NULL;
	}
	struct seat *seat = calloc(1, sizeof(*seat));
	assert(seat);
	wl_array_init(&seat->surfaces);
	seat->register_surface = seat_register_surface;
	seat->unregister_surface = seat_unregister_surface;
	seat->pointer_set_shape = seat_pointer_set_shape;
	seat->pointer = wl_seat_get_pointer(client->state.wl_seat);
	wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
	struct wp_cursor_shape_manager_v1 *shape_manager = client->state.cursor_shape_manager;
	if (shape_manager) {
		seat->pointer_shape = wp_cursor_shape_manager_v1_get_pointer(
			shape_manager, seat->pointer);
	}
	return seat;
}
