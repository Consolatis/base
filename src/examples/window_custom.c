#include "base.h"

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
handle_surface_render(struct buffer *buffer)
{
	//renderer_shm_solid(buffer, 0xaa000000);
	renderer_shm_solid(buffer, 0x33333333);
}

static void
handle_initial_sync(struct client *client, void *data)
{
	struct toplevel *toplevel = toplevel_create(client);
	toplevel->set_title(toplevel, "random window title");
	toplevel->set_app_id(toplevel, "base.window.custom");
	toplevel->decorate(toplevel);
	toplevel->add_handler(toplevel, (struct toplevel_handler) {
		.reconfigure = handle_toplevel_reconfigure,
		.close = handle_toplevel_close_request,
	});
	toplevel->surface->set_render_func(toplevel->surface, handle_surface_render);
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
