#include <stdio.h>

#include "base.h"
#include "buffer.h"

static void
render_frame(struct toplevel *toplevel, uint32_t color)
{
	struct base_allocator *pool = toplevel->surface->client->shm_pool;
	struct base_buffer *buffer = pool->create_buffer(pool,
		toplevel->pending.width, toplevel->pending.height,
		DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR
	);
	render_solid(buffer, color);
	toplevel->surface->set_buffer(toplevel->surface, buffer);
}

static void
handle_frame_callback(struct surface *surface, uint32_t time_msec, void *data)
{
	surface->request_frame(surface, handle_frame_callback, data);
	uint32_t opacity = (time_msec >> 3) & 0xffff;
	opacity = (opacity ^ ((opacity >> 8) & 1) * 255) & 0xff;
	const float opacity_f = opacity / 255.0;
	uint32_t color = 0xffu << 24;
	color |= ((uint8_t)((float)opacity * opacity_f)) << 0;
	color |= ((uint8_t)((float)(255 - opacity) * opacity_f)) << 16;

	render_frame(data, color);
}

static void
handle_toplevel_reconfigure(struct toplevel *toplevel, void *data, int width, int height)
{
	toplevel->pending.width = width > 0 ? width : 800;
	toplevel->pending.height = height > 0 ? height : 600;
	toplevel->surface->request_frame(toplevel->surface, handle_frame_callback, toplevel);
	if (!toplevel->configured) {
		render_frame(toplevel, 0x80000000);
	}
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
	toplevel->set_app_id(toplevel, "base.window.animate");
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
