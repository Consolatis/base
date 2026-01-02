#include "base.h"
#include "log.h"
#include "toplevel_handle.h"
#include "ext_capture.h"

/*
// TODO: not used at the moment
struct toplevel_capture {
	struct toplevel *toplevel;
	struct ext_capture_sesion *session;
};
*/

// TODO: annoying global vars
struct toplevel *toplevel = NULL;
static struct ext_capture_manager *capture_manager = NULL;
static struct ext_capture_session *capture_session = NULL;

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
	toplevel = toplevel_create(client);
	toplevel->set_title(toplevel, "random window title");
	toplevel->set_app_id(toplevel, "base.window");
	toplevel->decorate(toplevel);
	toplevel->add_handler(toplevel, (struct toplevel_handler) {
		.close = handle_toplevel_close_request,
	});
}

static struct buffer *
shm_buffer_allocator(struct ext_capture_session *session)
{
	struct shm_pool *pool = session->manager->client->shm_pool;
	uint32_t *shm_format = session->shm.formats.data;
	return pool->get_buffer_with_format(pool, session->width, session->height, *shm_format);
}

static void
handle_capture_buffer_ready(struct ext_capture_session *session, void *data, struct buffer *buffer)
{
	log("capture frame ready");
	if (!toplevel) {
		return;
	}
	toplevel->surface->set_buffer(toplevel->surface, buffer);
}

/* TODO: add handle_toplevels_initial_sync to handler and use that one instead */
static void
handle_toplevels_sync(struct toplevel_handle_manager *toplevels, void *data)
{
	if (capture_session) {
		return;
	}
	struct toplevel_handle *handle;
	struct toplevel_handle *capture_handle = NULL;
	wl_list_for_each(handle, &toplevels->handles, link) {
		/* TODO: add some command line arg like --list and --id to match handle->id */
		capture_handle = handle;
		break;
	}
	if (!capture_handle) {
		return;
	}
	capture_session = capture_manager->capture_toplevel(
		capture_manager, capture_handle, shm_buffer_allocator);
	capture_session->add_handler(capture_session, (struct ext_capture_session_handler) {
		.buffer_ready = handle_capture_buffer_ready,
	});
}

int
main(int argc, const char *argv[])
{
	struct client *client = client_create();
	struct toplevel_handle_manager *toplevels = toplevel_handle_manager_create(client);

	client->add_handler(client, (struct client_handler) {
		.initial_sync = handle_initial_sync,
		.data = toplevels,
	});

	toplevels->add_handler(toplevels, (struct toplevel_handle_manager_handler) {
		.synced = handle_toplevels_sync,
		.data = toplevels,
	});

	capture_manager = ext_capture_manager_create(client);

	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
