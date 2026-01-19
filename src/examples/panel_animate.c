#include "base.h"
#include "buffer.h"

#include "wlr-layer-shell-unstable-v1.xml.h"

static void
render_frame(struct layershell *layer, uint32_t color)
{
	struct base_allocator *pool = layer->surface->client->shm_pool;
	struct base_buffer *buffer = pool->create_buffer(pool,
		layer->current.width, layer->current.height,
		DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR
	);
	render_solid(buffer, color);
	layer->surface->set_buffer(layer->surface, buffer);
}

static void
handle_frame_callback(struct surface *surface, uint32_t time_msec, void *data)
{
	surface->request_frame(surface, handle_frame_callback, data);
	uint32_t opacity = (time_msec >> 3) & 0xffff;
	opacity = (opacity ^ ((opacity >> 8) & 1) * 255) & 0xff;
	const float opacity_f = opacity / 255.0;
	uint32_t color = opacity << 24;
	color |= ((uint8_t)((float)opacity * opacity_f)) << 16;
	color |= ((uint8_t)((float)(255 - opacity) * opacity_f)) << 0;
	render_frame(data, color);
}

static void
handle_layershell_reconfigure(struct layershell *layershell, void *data, int width, int height)
{
	layershell->surface->request_frame(layershell->surface, handle_frame_callback, layershell);
	render_frame(layershell, 0x80000000);
}

static void
handle_initial_sync(struct client *client, void *data)
{
	struct layershell *layershell = layershell_create(
		client,
		NULL /*output*/, 0 /*width*/, 50 /*height*/,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
	);

	layershell->add_handler(layershell, (struct layershell_handler) {
		.reconfigure = handle_layershell_reconfigure,
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
