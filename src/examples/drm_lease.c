#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "base.h"
#include "buffer.h"
#include "drm.h"
#include "drm_lease.h"
#include "log.h"
#include "render.h"

#define BUFFER_COUNT 2

struct fancy_output {
	struct drm_output *output;
	struct base_buffer *buffers[BUFFER_COUNT];
	uint8_t render_buffer;
	/* Animation stuff */
	uint32_t center_x;
	uint32_t center_y;
	/* FPS counter */
	time_t start;
	uint64_t frames;
};

static void
hello_kms(int drm_fd)
{
	struct drm *drm = drm_create(drm_fd);
	if (!drm) {
		log("Failed to create drm");
		return;
	}
	if (wl_list_empty(&drm->outputs)) {
		log("No outputs found");
		drm->destroy(drm);
		return;
	}

	struct fancy_output fancy = {0};

	struct base_allocator *alloc = gbm_allocator_create(drm_fd);

	struct drm_output *output;
	struct drm_output_mode *mode;
	wl_list_for_each_reverse(output, &drm->outputs, link) {
		wl_list_for_each(mode, &output->modes, link) {
			if (mode->preferred) {
				/* FIXME: needs a output->formats lookup */
				uint32_t format = DRM_FORMAT_XRGB8888;

				fancy.output = output;
				fancy.render_buffer = 1;
				for (int i = 0; i < BUFFER_COUNT; i++) {
					fancy.buffers[i] = alloc->create_buffer(alloc,
						mode->width, mode->height,
						format, DRM_FORMAT_MOD_LINEAR
					);
					fancy.buffers[i]->lock(fancy.buffers[i]);
				}
				output->set_mode(output, mode, fancy.buffers[0]);
				log("Using %ux%u@%u", mode->width, mode->height, mode->refresh);
				break;
			}
		}
		if (fancy.output) {
			break;
		}
	}
	if (!fancy.output) {
		log("Failed to initialize output");
		drm->destroy(drm);
		return;
	}

	time_t start = time(NULL);
	time_t now = start;
	while (now - start < 10) {
		if (!fancy.start) {
			fancy.frames = 1;
			fancy.start = start;
		}

		struct base_buffer *buffer = fancy.buffers[fancy.render_buffer];
		void *pixels = buffer->get_pixels(buffer, BASE_ALLOCATOR_REQ_WRITE);

		raw_render_checkerboard(pixels, buffer->width,
			buffer->height, buffer->stride);
		//raw_render_gradient(pixels, buffer->width,
		//	buffer->height, buffer->stride, now & 0xff);
		raw_render_y_line(pixels, buffer->width, buffer->height,
			buffer->stride, fancy.center_x, 10, now & 0xffffffffu);
		buffer->get_pixels_end(buffer, pixels);
		fancy.center_x = (fancy.center_x + 5) % buffer->width;

		fancy.output->set_buffer(output, buffer, /*block*/true);
		fancy.render_buffer = (fancy.render_buffer + 1) % BUFFER_COUNT;

		fancy.frames++;
		now = time(NULL);
		if (now - fancy.start >= 5) {
			log("[%u] FPS: %.2f", fancy.output->connector_id, (double)fancy.frames / (now - fancy.start));
			fancy.frames = 0;
			fancy.start = now;
		}
	}

	for (int i = 0; i < BUFFER_COUNT; i++) {
		struct base_buffer *buffer = fancy.buffers[i];
		if (buffer) {
			buffer->unlock(buffer);
		}
	}
	alloc->destroy(alloc);
	log("Done");
	drm->destroy(drm);
}

static void
handle_lease_callback(struct drm_lease_connector *connector)
{
	if (connector->lease_fd < 0) {
		log("Lease rejected or withdrawn");
		return;
	}

	log("Lease granted, starting kms with fd %d", connector->lease_fd);
	hello_kms(connector->lease_fd);
	connector->terminate(connector); /* closes the lease fd */
	struct client *client = connector->device->manager->client;
	client->terminate(client);
}

static void
handle_initial_drm_lease_sync(struct drm_lease_manager *manager, void *data)
{
	const char *filter = data;
	struct drm_lease_device *device;
	wl_list_for_each(device, &manager->devices, link) {
		struct drm_lease_connector *connector;
		wl_list_for_each(connector, &device->connectors, link) {
			log("Found offered connector %s", connector->name);
			if (filter && strcmp(filter, connector->name) != 0) {
				continue;
			}
			log("Requesting lease");
			connector->request(connector, handle_lease_callback);
			return;
		}
	}
	log("No matching connector found");
	manager->client->terminate(manager->client);
}

#include <fcntl.h>
int
main(int argc, char *argv[])
{
	char *filter = NULL;
	if (argc == 2 && argv[1][0] == '/') {
		/* Open DRM device directly when run on the TTY */
		int drm_fd = open(argv[1], O_RDWR);
		if (drm_fd < 0) {
			perror("Failed to open drm device");
			return 1;
		}
		hello_kms(drm_fd);
		close(drm_fd);
		return 0;
	} else if (argc == 2) {
		/* Filter by connector name */
		filter = argv[1];
	}
	struct client *client = client_create();
	struct drm_lease_manager *lease_manager = drm_lease_manager_create(client);
	lease_manager->add_handler(lease_manager, (struct drm_lease_manager_handler) {
		.initial_sync = handle_initial_drm_lease_sync,
		.data = filter,
	});
	client->connect(client);
	client->loop(client);
	client->destroy(client);

	return 0;
}
