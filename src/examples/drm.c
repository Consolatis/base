#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "drm.h"
#include "log.h"
#include "render.h"

#define BUFFERS 3

struct fancy_output {
	struct drm_output *output;
	struct drm_dumb_buffer *buffers[BUFFERS];
	uint32_t render_buffer;
	/* FPS counter */
	uint32_t fps_start;
	uint32_t frames;
	/* Animation */
	uint32_t center_x;
};

static uint32_t
get_time_msec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
dump_fps(struct fancy_output *fancy, uint32_t now, uint32_t delta)
{
	if (fancy->frames < 20) {
		return;
	}
	log("Connector %u FPS: %.4f", fancy->output->connector_id,
		(double)fancy->frames / (double)delta * 1000.0f);
	fancy->fps_start = now;
	fancy->frames = 0;
}

static bool
show_frame(struct fancy_output *output)
{
	struct drm_dumb_buffer *dumb_buffer = output->buffers[output->render_buffer];
	struct drm_buffer *buffer = &dumb_buffer->base;

	if (!output->output->set_buffer(output->output, buffer, /*block*/false)) {
		log("output commit failed");
		return false;
	}
	return true;
}

static void
render_frame(struct fancy_output *output) {
	struct drm_dumb_buffer *dumb_buffer = output->buffers[output->render_buffer];
	struct drm_buffer *buffer = &dumb_buffer->base;

	//raw_render_gradient(dumb_buffer->pixels, buffer->width, buffer->height, buffer->stride, 0x80u);
	//raw_render_solid(dumb_buffer->pixels, buffer->width, buffer->height, buffer->stride, 0xff0000ffu);
	raw_render_checkerboard(dumb_buffer->pixels, buffer->width, buffer->height, buffer->stride);
	raw_render_y_line(dumb_buffer->pixels, buffer->width, buffer->height,
			buffer->stride, output->center_x, 10, 0x00ff0000u);
	output->center_x = (output->center_x + 5) % buffer->width;
}

static void
on_frame_presented(struct drm_output *output)
{
	struct fancy_output *fancy = output->data;
	fancy->frames++;
	fancy->render_buffer = (fancy->render_buffer + 1) % BUFFERS;
	render_frame(fancy);
	show_frame(fancy);
}


int
main(int argc, const char *argv[])
{
	if (argc < 2) {
		log("Usage: %s <drm-device-path>", argv[0]);
		return 1;
	}
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("Open failed");
		return 1;
	}
	struct drm *drm = drm_create(fd);
	if (!drm) {
		return 1;
	}

	struct fancy_output *outputs = calloc(wl_list_length(&drm->outputs), sizeof(*outputs));
	uint32_t now = get_time_msec();

	/* Setup */
	uint32_t i = 0;
	struct drm_output *output;
	struct drm_output_mode *mode;
	wl_list_for_each_reverse(output, &drm->outputs, link) {
		struct fancy_output *fancy = &outputs[i];
		output->data = fancy;
		output->on_frame_presented = on_frame_presented;
		wl_list_for_each(mode, &output->modes, link) {
			if (mode->preferred) {
				log("connector %u using %ux%u@%u", output->connector_id,
					mode->width, mode->height, mode->refresh);

				fancy->output = output;
				fancy->fps_start = now;
				fancy->render_buffer = 0;
				fancy->center_x = mode->width / 2;
				for (int b = 0; b < BUFFERS; b++) {
					/* FIXME: needs a output->formats lookup */
					fancy->buffers[b] = drm->allocator.create_dumb_buffer(
						drm, mode->width, mode->height, DRM_FORMAT_XRGB8888);
				}
				render_frame(fancy);
				output->set_mode(output, mode, &fancy->buffers[0]->base);

				fancy->render_buffer = 1 % BUFFERS;
				fancy->center_x = mode->width / 2 - 50;
				render_frame(fancy);
				show_frame(fancy);
				output->mode = mode;
				break;
			}
		}
		i++;
	}

	/* Main loop */
	const uint32_t runtime_s = 30;
	uint32_t start = now;
	while (now - start < runtime_s * 1000) {
		now = get_time_msec();
		for (int i = 0; i < wl_list_length(&drm->outputs); i++) {
			struct fancy_output *fancy = &outputs[i];
			if (!fancy->output) {
				continue;
			}
			const uint32_t delta = now - fancy->fps_start;
			if (delta >= 5 * 1000) {
				dump_fps(fancy, now, delta);
			}

		}
		if (!drm->read_events(drm, /*block*/true)) {
			log("Failed to wait for drm page flip");
			break;
		}
	}

	/* Cleanup */
	for (int i = 0; i < wl_list_length(&drm->outputs); i++) {
		struct fancy_output *fancy = &outputs[i];
		if (!fancy->output) {
			continue;
		}
		if (fancy->frames > 10) {
			const uint32_t delta = now - fancy->fps_start;
			dump_fps(fancy, now, delta);
		}

		for (int b = 0; b < BUFFERS; b++) {
			fancy->buffers[b]->base.destroy(&fancy->buffers[b]->base);
		}
	}

	free(outputs);
	drm->destroy(drm);
	return 0;
}
