#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "log.h"
#include "toplevel_handle.h"

#include "ext-foreign-toplevel-list-v1.xml.h"

#define MANAGER_CALLBACK(manager, name, ...) do {                \
	struct toplevel_handle_manager_handler *handler;         \
	wl_array_for_each(handler, &(manager)->callbacks) {      \
		if (handler->name) {                             \
			handler->name((manager), handler->data,  \
				##__VA_ARGS__);                  \
		}                                                \
	}                                                        \
} while (0)

static void
toplevel_handle_closed(void *data, struct ext_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel_handle *toplevel = data;
	wl_list_remove(&toplevel->link);
	MANAGER_CALLBACK(toplevel->manager, synced);
	free(toplevel->id);
	free(toplevel->app_id);
	free(toplevel->title);
	free(toplevel);
	ext_foreign_toplevel_handle_v1_destroy(handle);
}

static void
toplevel_handle_done(void *data, struct ext_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel_handle *toplevel = data;
	MANAGER_CALLBACK(toplevel->manager, synced);
}

static void
toplevel_handle_title(void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *title)
{
	struct toplevel_handle *toplevel = data;
	free(toplevel->title);
	toplevel->title = strdup(title);
}

static void
toplevel_handle_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *app_id)
{
	struct toplevel_handle *toplevel = data;
	free(toplevel->app_id);
	toplevel->app_id = strdup(app_id);
}

static void
toplevel_handle_identifier(void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *id)
{
	struct toplevel_handle *toplevel = data;
	free(toplevel->id);
	toplevel->id = strdup(id);
}

static const struct ext_foreign_toplevel_handle_v1_listener toplevel_listener = {
	.closed = toplevel_handle_closed,
	.done = toplevel_handle_done,
	.title = toplevel_handle_title,
	.app_id = toplevel_handle_app_id,
	.identifier = toplevel_handle_identifier,
};


static void
manager_handle_toplevel(void *data,
		struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1,
		struct ext_foreign_toplevel_handle_v1 *handle)
{
	struct toplevel_handle_manager *manager = data;
	struct toplevel_handle *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->manager = manager;
	toplevel->handle = handle;
	ext_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_listener, toplevel);
	wl_list_insert(manager->handles.prev, &toplevel->link);
}

static void
manager_handle_finished(void *data,
		struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1)
{
	log("finished");
}

static const struct ext_foreign_toplevel_list_v1_listener manager_listener = {
	.toplevel = manager_handle_toplevel,
	.finished = manager_handle_finished,
};

static void
client_handle_global(struct client *client, void *data, struct wl_registry *registry,
		const char *iface_name, uint32_t global, uint32_t version)
{
	struct toplevel_handle_manager *manager = data;
	if (manager->manager) {
		return;
	}
	if (!strcmp(iface_name, ext_foreign_toplevel_list_v1_interface.name)) {
		manager->manager = wl_registry_bind(registry, global,
			&ext_foreign_toplevel_list_v1_interface, version);
		ext_foreign_toplevel_list_v1_add_listener(manager->manager,
			&manager_listener, manager);
	}
}

static void
manager_add_handler(struct toplevel_handle_manager *manager, struct toplevel_handle_manager_handler handler)
{
	struct toplevel_handle_manager_handler *data = wl_array_add(&manager->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

struct toplevel_handle_manager *
toplevel_handle_manager_create(struct client *client)
{
	struct toplevel_handle_manager *manager = calloc(1, sizeof(*manager));
	assert(manager);

	client->add_handler(client, (struct client_handler) {
		.registry = client_handle_global,
		.data = manager,
	});
	wl_array_init(&manager->callbacks);
	wl_list_init(&manager->handles);
	manager->client = client;
	manager->add_handler = manager_add_handler;
	return manager;
}
