#include "base.h"
#include "log.h"
#include "toplevel_handle.h"

static void
handle_toplevels_sync(struct toplevel_handle_manager *toplevels, void *data)
{
	log("New sync received");
	struct toplevel_handle *handle;
	wl_list_for_each(handle, &toplevels->handles, link) {
		log("%s   %-15s   %s", handle->id, handle->app_id, handle->title);
	}
}

int
main(int argc, const char *argv[])
{
	struct client *client = client_create();
	struct toplevel_handle_manager *toplevels = toplevel_handle_manager_create(client);
	toplevels->add_handler(toplevels, (struct toplevel_handle_manager_handler) {
		.synced = handle_toplevels_sync,
	});

	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
