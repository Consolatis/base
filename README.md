Geniusly named wayland framework for simple wayland clients.

Somehow ended up also supporting direct DRM rendering.

Example usage:
```c
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
```

See the `src/examples` directory for more fun.
