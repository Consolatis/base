#include "base.h"
#include "log.h"
#include "toplevel_handle.h"

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

static void
handle_toplevels_sync(struct toplevel_handle_manager *toplevels, void *data)
{
	log("New sync received");
	struct toplevel_handle *handle;
	wl_list_for_each(handle, &toplevels->handles, link) {
		log("%s %-15s  %s", handle->id, handle->app_id, handle->title);
	}
}

int
main(int argc, const char *argv[])
{
	struct client *client = client_create();
	client->add_handler(client, (struct client_handler) {
		.initial_sync = handle_initial_sync,
	});

	struct toplevel_handle_manager *toplevels = toplevel_handle_manager_create(client);
	toplevels->add_handler(toplevels, (struct toplevel_handle_manager_handler) {
		.synced = handle_toplevels_sync,
		.data = toplevels,
	});

	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
