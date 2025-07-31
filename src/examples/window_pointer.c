#include "base.h"
#include <stdio.h>

static void
handle_toplevel_reconfigure(struct toplevel *toplevel, void *data, int width, int height)
{
	toplevel->surface->render_frame(toplevel->surface,
		width > 0 ? width : 800, height > 0 ? height: 600);
}

static void
handle_toplevel_close_request(struct toplevel *toplevel, void *data)
{
	struct client *client = toplevel->surface->client;
	toplevel->destroy(toplevel);
	client->terminate(client);
}

static void
handle_pointer_enter(struct surface *surface, void *data, wl_fixed_t sx, wl_fixed_t sy)
{
	fprintf(stderr, "pointer enter at %d,%d\n", wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

static void
handle_pointer_motion(struct surface *surface, void *data, wl_fixed_t sx, wl_fixed_t sy)
{
	fprintf(stderr, "pointer motion at %d,%d\n", wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

static void
handle_pointer_button(struct surface *surface, void *data, uint32_t button, uint32_t state)
{
	fprintf(stderr, "pointer button %u %s\n", button, state ? "pressed" : "released");
}

static void
handle_pointer_leave(struct surface *surface, void *data)
{
	fprintf(stderr, "pointer leave\n");
}

static void
handle_initial_sync(struct client *client, void *data)
{
	struct toplevel *toplevel = toplevel_create(client);
	toplevel->set_title(toplevel, "random window title");
	toplevel->set_app_id(toplevel, "base.window");
	toplevel->decorate(toplevel);
	toplevel->add_handler(toplevel, (struct toplevel_handler) {
		.reconfigure = handle_toplevel_reconfigure,
		.close = handle_toplevel_close_request,
	});
	toplevel->surface->add_handler(toplevel->surface, (struct surface_handler) {
		.pointer_enter = handle_pointer_enter,
		.pointer_motion = handle_pointer_motion,
		.pointer_button = handle_pointer_button,
		.pointer_leave = handle_pointer_leave,
		.data = toplevel,
	});
}

int
main(int argc, const char *argv[])
{
	struct client *client = client_create();
	client->add_handler(client, (struct client_handler) {
		.initial_sync = handle_initial_sync,
	});
	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
