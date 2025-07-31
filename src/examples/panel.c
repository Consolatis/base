#include "base.h"
#include "wlr-layer-shell-unstable-v1.xml.h"

static void
handle_layershell_reconfigure(struct layershell *layershell, void *data, int width, int height)
{
	layershell->surface->render_frame(layershell->surface, width, height);
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
