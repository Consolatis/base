#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "buffer.h"
#include "log.h"

#include "cursor-shape-v1.xml.h"
#include "wlr-layer-shell-unstable-v1.xml.h"
#include "xdg-decoration-unstable-v1.xml.h"
#include "xdg-shell.xml.h"

#define CLIENT_CALLBACK(client, name, ...) do {                 \
	struct client_handler *handler;                         \
	wl_array_for_each(handler, &(client)->callbacks) {      \
		if (handler->name) {                            \
			handler->name((client), handler->data,  \
				##__VA_ARGS__);                 \
		}                                               \
	}                                                       \
} while (0)

static void
client_add_handler(struct client *client, struct client_handler handler)
{
	struct client_handler *data = wl_array_add(&client->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

static void
handle_registry_global(void *data, struct wl_registry *wl_registry,
		uint32_t global, const char *interface, uint32_t version)
{
	struct client *client = data;
	CLIENT_CALLBACK(client, registry, wl_registry, interface, global, version);

	if (!client->state.wl_compositor && !strcmp(interface, wl_compositor_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.wl_compositor = wl_registry_bind(
			wl_registry, global, &wl_compositor_interface, version);
	}

	if (!client->state.wl_shm && !strcmp(interface, wl_shm_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.wl_shm = wl_registry_bind(
			wl_registry, global, &wl_shm_interface, version);
	}

	if (!client->state.xdg_base && !strcmp(interface, xdg_wm_base_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.xdg_base = wl_registry_bind(
			wl_registry, global, &xdg_wm_base_interface, version);
	}

	if (!client->state.layershell_manager && !strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.layershell_manager = wl_registry_bind(
			wl_registry, global, &zwlr_layer_shell_v1_interface, version);
	}

	if (!client->state.deco_manager && !strcmp(interface, zxdg_decoration_manager_v1_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.deco_manager = wl_registry_bind(
			wl_registry, global, &zxdg_decoration_manager_v1_interface, version);
	}

	if (!client->state.wl_seat && !strcmp(interface, wl_seat_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.wl_seat = wl_registry_bind(
			wl_registry, global, &wl_seat_interface, version);
	}

	if (!client->state.cursor_shape_manager && !strcmp(interface, wp_cursor_shape_manager_v1_interface.name)) {
		// FIXME: MAX(whatever, version);
		client->state.cursor_shape_manager = wl_registry_bind(
			wl_registry, global, &wp_cursor_shape_manager_v1_interface, version);
	}
}

static void
handle_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
	/* This space deliberately left blank */
}


static const struct wl_registry_listener wl_registry_listener = {
	.global = handle_registry_global,
	.global_remove = handle_registry_global_remove,
};

static void
client_connect(struct client *client)
{
	struct client_state *state = &client->state;
	state->wl_display = wl_display_connect(NULL);
	if (!state->wl_display) {
		log("Failed to connect to compositor");
		client->should_terminate = true;
		return;
	}
	client->buffer_manager = base_wl_buffer_manager_create(client);
	state->wl_registry = wl_display_get_registry(state->wl_display);
	wl_registry_add_listener(state->wl_registry, &wl_registry_listener, client);
	wl_display_roundtrip(state->wl_display);

	client->seat = seat_create(client);
	CLIENT_CALLBACK(client, initial_sync);
}

static void
client_loop(struct client *client)
{
	while (!client->should_terminate) {
		errno = 0;
		while (wl_display_dispatch(client->state.wl_display)) {
			/* This space deliberately left blank */
			if (client->should_terminate) {
				break;
			}
		}
		if (client->should_terminate) {
			break;
		} else if (errno == EAGAIN) {
			//log("EAGAIN");
		} else if (errno) {
			perror("something wrong with the loop");
			break;
		}
	}

	CLIENT_CALLBACK(client, disconnected);
	if (client->state.wl_display) {
		wl_display_disconnect(client->state.wl_display);
	}
}

static void
client_terminate(struct client *client)
{
	assert(!client->should_terminate);

	/* FIXME: clean up remaining managers */

	if (client->state.wl_compositor) {
		wl_compositor_destroy(client->state.wl_compositor);
		client->state.wl_compositor = NULL;
	}
	if (client->state.wl_shm) {
		wl_shm_destroy(client->state.wl_shm);
		client->state.wl_shm = NULL;
	}
	if (client->state.wl_registry) {
		wl_registry_destroy(client->state.wl_registry);
		client->state.wl_registry = NULL;
	}
	if (client->state.wl_display) {
		wl_display_flush(client->state.wl_display);
	}
	client->should_terminate = true;
}

static void
client_destroy(struct client *client)
{
	CLIENT_CALLBACK(client, destroy);
	wl_array_release(&client->callbacks);
	free(client);
}

struct client *
client_create(void)
{
	struct client *client = calloc(1, sizeof(*client));
	assert(client);
	*client = (struct client) {
		.add_handler = client_add_handler,
		.connect = client_connect,
		.loop = client_loop,
		.terminate = client_terminate,
		.destroy = client_destroy,
		.shm_pool = shm_allocator_create(),
		.drm_fd = -1,
	};
	wl_array_init(&client->callbacks);
	return client;
}
