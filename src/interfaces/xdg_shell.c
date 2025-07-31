#include <assert.h>
#include <stdlib.h>
#include "base.h"

#include "xdg-shell.xml.h"
#include "xdg-decoration-unstable-v1.xml.h"

#define TOPLEVEL_CALLBACK(toplevel, name, ...) do {               \
	struct toplevel_handler *handler;                         \
	wl_array_for_each(handler, &(toplevel)->callbacks) {      \
		if (handler->name) {                              \
			handler->name((toplevel), handler->data,  \
				##__VA_ARGS__);                   \
		}                                                 \
	}                                                         \
} while (0)

static void
toplevel_add_handler(struct toplevel *toplevel, struct toplevel_handler handler)
{
	struct toplevel_handler *data = wl_array_add(&toplevel->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

static void
toplevel_destroy(struct toplevel *toplevel)
{
	toplevel->surface->unmap(toplevel->surface);
	if (toplevel->deco) {
		zxdg_toplevel_decoration_v1_destroy(toplevel->deco);
	}
	xdg_toplevel_destroy(toplevel->xdg_toplevel);
	xdg_surface_destroy(toplevel->xdg_surface);
	toplevel->surface->destroy(toplevel->surface);
	wl_array_release(&toplevel->callbacks);
	free(toplevel);
}

static void
toplevel_decorate(struct toplevel *toplevel)
{
	assert(!toplevel->deco);
	struct zxdg_decoration_manager_v1 *deco_manager =
		toplevel->surface->client->state.deco_manager;
	assert(deco_manager);

	toplevel->deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
		deco_manager, toplevel->xdg_toplevel);
	zxdg_toplevel_decoration_v1_set_mode(
		toplevel->deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
toplevel_set_title(struct toplevel *toplevel, const char *title)
{
	xdg_toplevel_set_title(toplevel->xdg_toplevel, title);
}

static void
toplevel_set_app_id(struct toplevel *toplevel, const char *app_id)
{
	xdg_toplevel_set_app_id(toplevel->xdg_toplevel, app_id);
}

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states)
{
	struct toplevel *toplevel = data;
	toplevel->pending.width = width;
	toplevel->pending.height = height;
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct toplevel *toplevel = data;
	TOPLEVEL_CALLBACK(toplevel, close);
}

static void
handle_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height)
{

}

static void
handle_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel,
		struct wl_array *capabilities)
{

}

struct xdg_toplevel_listener toplevel_listener = {
	.configure = handle_toplevel_configure,
	.close = handle_toplevel_close,
	.configure_bounds = handle_toplevel_configure_bounds,
	.wm_capabilities = handle_toplevel_wm_capabilities,
};

static void
handle_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct toplevel *toplevel = data;
	if (!toplevel->configured) {
		xdg_surface_ack_configure(xdg_surface, serial);
		TOPLEVEL_CALLBACK(toplevel, reconfigure,
			toplevel->pending.width, toplevel->pending.height);
		toplevel->configured = true;
	} else {
		if (toplevel->pending.width != toplevel->current.width
				|| toplevel->pending.height != toplevel->current.height) {
			TOPLEVEL_CALLBACK(toplevel, reconfigure,
				toplevel->pending.width, toplevel->pending.height);
		}
		xdg_surface_ack_configure(xdg_surface, serial);
	}
	toplevel->current = toplevel->pending;
}


struct xdg_surface_listener surface_listener = {
	.configure = handle_surface_configure,
};

struct toplevel *
toplevel_create(struct client *client)
{
	assert(client->state.xdg_base);
	struct toplevel *toplevel = calloc(1, sizeof(*toplevel));
	assert(toplevel);
	wl_array_init(&toplevel->callbacks);
	toplevel->add_handler = toplevel_add_handler;
	toplevel->decorate = toplevel_decorate;
	toplevel->set_title = toplevel_set_title;
	toplevel->set_app_id = toplevel_set_app_id;
	toplevel->destroy = toplevel_destroy;
	toplevel->surface = surface_create(client);
	toplevel->xdg_surface = xdg_wm_base_get_xdg_surface(
		client->state.xdg_base, toplevel->surface->surface);
	xdg_surface_add_listener(toplevel->xdg_surface, &surface_listener, toplevel);
	toplevel->xdg_toplevel = xdg_surface_get_toplevel(toplevel->xdg_surface);
	xdg_toplevel_add_listener(toplevel->xdg_toplevel, &toplevel_listener, toplevel);
	wl_surface_commit(toplevel->surface->surface);
	return toplevel;
}
