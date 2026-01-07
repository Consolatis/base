#pragma once

#include <wayland-util.h>

// TODO: completely lacks ->destroy() functions

struct drm_lease_manager;
struct drm_lease_manager_handler {
	void (*initial_sync)(struct drm_lease_manager *manager, void *data);
	void *data;
};

struct drm_lease_manager {
	struct client *client;
	struct wl_list devices;

	/* Functions */
	void (*add_handler)(struct drm_lease_manager *manager, struct drm_lease_manager_handler handler);

	/* Private */
	struct wl_array callbacks;
};

struct drm_lease_device {
	struct drm_lease_manager *manager;
	int drm_fd;
	struct wl_list connectors;

	/* Private */
	struct wp_drm_lease_device_v1 *handle;
	struct wl_list link;
};

struct drm_lease_connector;
typedef void (*lease_callback_func_t)(struct drm_lease_connector *connector);

struct drm_lease_connector {
	int lease_fd;
	struct drm_lease_device *device;

	/* Properties */
	uint32_t id;
	char *name;
	char *description;

	/* Functions */
	void (*request)(struct drm_lease_connector *connector, lease_callback_func_t lease_callback);
	void (*terminate)(struct drm_lease_connector *connector);

	/* Private */
	struct wp_drm_lease_connector_v1 *handle;
	struct wp_drm_lease_v1 *lease_handle;
	struct wl_list link;
	lease_callback_func_t lease_callback;
};

struct drm_lease_manager *drm_lease_manager_create(struct client *client);
