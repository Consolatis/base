#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base.h"
#include "drm_lease.h"
#include "log.h"

#include "drm-lease-v1.xml.h"

#define MANAGER_CALLBACK(manager, name, ...) do {                \
	struct drm_lease_manager_handler *handler;               \
	wl_array_for_each(handler, &(manager)->callbacks) {      \
		if (handler->name) {                             \
			handler->name((manager), handler->data,  \
				##__VA_ARGS__);                  \
		}                                                \
	}                                                        \
} while (0)

/* Wayland lease handlers */
static void
lease_handle_fd(void *data,  struct wp_drm_lease_v1 *lease, int32_t leased_fd)
{
	struct drm_lease_connector *connector = data;
	connector->lease_fd = leased_fd;
	connector->lease_callback(connector);
}

static void
lease_handle_finished(void *data,  struct wp_drm_lease_v1 *lease)
{
	struct drm_lease_connector *connector = data;
	connector->lease_fd = -1;
	connector->lease_callback(connector);
}

static const struct wp_drm_lease_v1_listener lease_listener = {
	.lease_fd = lease_handle_fd,
	.finished = lease_handle_finished,
};

/* API */
static void
connector_handle_terminate(struct drm_lease_connector *connector)
{
	if (connector->lease_handle) {
		wp_drm_lease_v1_destroy(connector->lease_handle);
	}
	if (connector->lease_fd) {
		close(connector->lease_fd);
		connector->lease_fd = -1;
	}
}

static void
connector_handle_request(struct drm_lease_connector *connector, lease_callback_func_t lease_callback)
{
	assert(!connector->lease_callback);
	assert(!connector->lease_handle);
	connector->lease_callback = lease_callback;

	struct wp_drm_lease_request_v1 *req =
		wp_drm_lease_device_v1_create_lease_request(connector->device->handle);
	wp_drm_lease_request_v1_request_connector(req, connector->handle);
	struct wp_drm_lease_v1 *lease = wp_drm_lease_request_v1_submit(req);
	connector->lease_handle = lease;
	wp_drm_lease_v1_add_listener(lease, &lease_listener, connector);
}

/* Wayland connector handlers */
static void
connector_handle_name(void *data, struct wp_drm_lease_connector_v1 *connector_handle,
		const char *name)
{
	struct drm_lease_connector *connector = data;
	free(connector->name);
	connector->name = strdup(name);
}

static void
connector_handle_description(void *data, struct wp_drm_lease_connector_v1 *connector_handle,
		const char *description)
{
	struct drm_lease_connector *connector = data;
	free(connector->description);
	connector->description = strdup(description);
}

static void
connector_handle_id(void *data, struct wp_drm_lease_connector_v1 *connector_handle, uint32_t id)
{
	struct drm_lease_connector *connector = data;
	connector->id = id;
}

static void
connector_handle_done(void *data, struct wp_drm_lease_connector_v1 *connector_handle)
{
}

static void
connector_handle_withdrawn(void *data, struct wp_drm_lease_connector_v1 *connector_handle)
{
}

static const struct wp_drm_lease_connector_v1_listener connector_listener = {
	.name = connector_handle_name,
	.description = connector_handle_description,
	.connector_id = connector_handle_id,
	.done = connector_handle_done,
	.withdrawn = connector_handle_withdrawn,
};

/* Wayland device handlers */
static void
device_handle_drm_fd(void *data, struct wp_drm_lease_device_v1 *device_handle, int32_t fd)
{
	struct drm_lease_device *device = data;
	device->drm_fd = fd;
}

static void
device_handle_connector(void *data, struct wp_drm_lease_device_v1 *device_handle,
		struct wp_drm_lease_connector_v1 *connector_handle)
{
	struct drm_lease_device *device = data;
	struct drm_lease_connector *connector = calloc(1, sizeof(*connector));
	assert(connector);
	connector->lease_fd = -1;
	connector->device = device;
	connector->handle = connector_handle;
	connector->request = connector_handle_request;
	connector->terminate = connector_handle_terminate;
	wl_list_insert(device->connectors.prev, &connector->link);
	wp_drm_lease_connector_v1_add_listener(connector->handle, &connector_listener, connector);
}

static void
device_handle_done(void *data, struct wp_drm_lease_device_v1 *device_handle)
{
}

static void
device_handle_released(void *data, struct wp_drm_lease_device_v1 *device_handle)
{
}

static const struct wp_drm_lease_device_v1_listener device_listener = {
	.drm_fd = device_handle_drm_fd,
	.connector = device_handle_connector,
	.done = device_handle_done,
	.released = device_handle_released,
};

/* Client handlers */
static void
client_handle_initial_sync(struct client *client, void *data)
{
	struct drm_lease_manager *manager = data;
	wl_display_roundtrip(client->state.wl_display);
	MANAGER_CALLBACK(manager, initial_sync);
}

static void
client_handle_global(struct client *client, void *data, struct wl_registry *registry,
		const char *iface_name, uint32_t global, uint32_t version)
{
	struct drm_lease_manager *manager = data;
	if (!strcmp(iface_name, wp_drm_lease_device_v1_interface.name)) {
		struct drm_lease_device *device = calloc(1, sizeof(*device));
		assert(device);
		device->drm_fd = -1;
		device->manager = manager;
		wl_list_init(&device->connectors);
		device->handle = wl_registry_bind(registry, global,
			&wp_drm_lease_device_v1_interface, version);
		wp_drm_lease_device_v1_add_listener(device->handle, &device_listener, device);
		wl_list_insert(manager->devices.prev, &device->link);
	}
}

static void
manager_add_handler(struct drm_lease_manager *manager, struct drm_lease_manager_handler handler)
{
	struct drm_lease_manager_handler *data = wl_array_add(&manager->callbacks, sizeof(*data));
	assert(data);
	*data = handler;
}

struct drm_lease_manager *
drm_lease_manager_create(struct client *client)
{
	struct drm_lease_manager *manager = calloc(1, sizeof(*manager));
	assert(manager);
	client->add_handler(client, (struct client_handler) {
		.registry = client_handle_global,
		.initial_sync = client_handle_initial_sync,
		.data = manager,
	});
	wl_list_init(&manager->devices);
	wl_array_init(&manager->callbacks);
	manager->client = client;
	manager->add_handler = manager_add_handler;
	return manager;
}
