#pragma once

#include <wayland-util.h>

struct toplevel_handle_manager;
struct toplevel_handle_manager_handler {
	void (*synced)(struct toplevel_handle_manager *manager, void *data);
	void *data;
};

struct toplevel_handle_manager {
	struct client *client;
	struct wl_list handles;

	void (*add_handler)(struct toplevel_handle_manager *manager, struct toplevel_handle_manager_handler handler);

	/* Private */
	struct wl_array callbacks;
	struct ext_foreign_toplevel_list_v1 *manager;
};

/* TODO: destroy event would likely be good */
struct toplevel_handle {
	struct toplevel_handle_manager *manager;
	char *id;
	char *app_id;
	char *title;

	/* Private */
	struct wl_list link;
	struct ext_foreign_toplevel_handle_v1 *handle;
};

struct toplevel_handle_manager *toplevel_handle_manager_create(struct client *client);
