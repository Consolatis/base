#include <assert.h>
#include <stdlib.h>
#include "base.h"

#include "wlr-layer-shell-unstable-v1.xml.h"

#define LAYERSHELL_CALLBACK(layershell, name, ...) do {             \
	struct layershell_handler *handler;                         \
	wl_array_for_each(handler, &(layershell)->callbacks) {      \
		if (handler->name) {                                \
			handler->name((layershell), handler->data,  \
				##__VA_ARGS__);                     \
		}                                                   \
	}                                                           \
} while (0)

static void
layershell_add_handler(struct layershell *layershell, struct layershell_handler handler)
{
	struct layershell_handler *data = wl_array_add(&layershell->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

static void
layershell_destroy(struct layershell *layershell)
{
	zwlr_layer_surface_v1_destroy(layershell->layer_surface);
	layershell->surface->destroy(layershell->surface);
	wl_array_release(&layershell->callbacks);
	free(layershell);
}

static void
handle_layershell_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	struct layershell *layershell = data;
	if (layershell->current.width == width && layershell->current.height == height) {
		return;
	}
	layershell->current.width = width;
	layershell->current.height = height;
	LAYERSHELL_CALLBACK(layershell, reconfigure, width, height);
}

static void
handle_layershell_closed(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1)
{
}

struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = handle_layershell_configure,
	.closed = handle_layershell_closed,
};

struct layershell *
layershell_create(struct client *client, struct wl_output *output,
		uint32_t width, uint32_t height, uint32_t layer, uint32_t anchors)
{
	assert(client->state.layershell_manager);
	struct layershell *layershell = calloc(1, sizeof(*layershell));
	assert(layershell);
	wl_array_init(&layershell->callbacks);
	layershell->add_handler = layershell_add_handler;
	layershell->destroy = layershell_destroy;
	layershell->surface = surface_create(client);
	layershell->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		client->state.layershell_manager,
		layershell->surface->surface, output, layer, "namespace");
	zwlr_layer_surface_v1_add_listener(layershell->layer_surface, &layer_listener, layershell);
	zwlr_layer_surface_v1_set_anchor(layershell->layer_surface, anchors);
	zwlr_layer_surface_v1_set_size(layershell->layer_surface, width, height);
	wl_surface_commit(layershell->surface->surface);
	return layershell;
}
