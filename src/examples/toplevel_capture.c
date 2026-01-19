#include <assert.h>
#include "base.h"
#include "buffer.h"
#include "ext_capture.h"
#include "fourcc.h"
#include "log.h"
#include "toplevel_handle.h"

/*
// TODO: not used at the moment
struct toplevel_capture {
	struct toplevel *toplevel;
	struct ext_capture_sesion *session;
};
*/

// TODO: annoying global vars
static struct base_allocator *allocator = NULL;
static struct toplevel *toplevel = NULL;
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
static struct base_buffer *
shm_allocator(struct ext_capture_session *session)
{
	uint32_t *shm_format = session->shm.formats.data;
	return allocator->create_buffer(allocator,
		session->width, session->height,
		fourcc_from_shm_format(*shm_format), DRM_FORMAT_MOD_LINEAR
	);
}

static struct base_buffer *
dmabuf_allocator(struct ext_capture_session *session)
{
	struct ext_capture_dmabuf_format *drm_fmt = session->drm.formats.data;
	return allocator->create_buffer(allocator,
		session->width, session->height,
		drm_fmt->fourcc, drm_fmt->modifier
	);
}

static void
handle_capture_buffer_ready(struct ext_capture_session *session, void *data, struct base_buffer *buffer)
{
	//log("capture frame ready");
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

	struct client *client = data;
	ext_capture_allocator_func_t alloc_func = NULL;
	if (!allocator) {
		allocator = gbm_allocator_create(client->drm_fd);
		if (allocator) {
			alloc_func = dmabuf_allocator;
			log("Using gbm allocator");
		} else {
			log("Falling back to SHM allocator");
			allocator = shm_allocator_create();
			alloc_func = shm_allocator;
		}
	}
	assert(alloc_func);

	capture_session = capture_manager->capture_toplevel(
		capture_manager, capture_handle, alloc_func);
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
		.data = client,
	});

	capture_manager = ext_capture_manager_create(client);

	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
