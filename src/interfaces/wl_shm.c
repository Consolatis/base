#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "base.h"

#ifdef BUFFER_LOG
	#include <stdio.h>
#endif

/* shm pool */
static void
handle_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = data;
	buffer->busy = false;
}

static struct wl_buffer_listener buffer_listener = {
	.release = handle_buffer_release,
};

struct shm_buffer {
	struct buffer *buffer;
};

static void
update_buffer(struct buffer *buffer)
{
	if (buffer->buffer) {
		wl_buffer_destroy(buffer->buffer);
	}
	struct wl_shm_pool *shm_pool = wl_shm_create_pool(
		buffer->pool->client->state.wl_shm, buffer->fd, buffer->size);
	buffer->buffer = wl_shm_pool_create_buffer(shm_pool, 0,
		buffer->width, buffer->height, buffer->stride, buffer->shm_format);
	wl_shm_pool_destroy(shm_pool);
	wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
}

#ifdef BUFFER_LOG
static uint32_t
get_buffer_count(struct shm_pool *pool)
{
	uint32_t count = 0;
	struct shm_buffer *shm_buffer;
	wl_array_for_each(shm_buffer, &pool->buffers) {
		if (shm_buffer->buffer) {
			count++;
		}
	}
	return count;
}
#endif

static struct buffer *
shm_pool_get_buffer(struct shm_pool *pool, uint32_t width, uint32_t height)
{
	uint32_t requested_size = width * 4 * height;

	struct buffer *buffer;
	struct shm_buffer *shm_buffer;
	wl_array_for_each(shm_buffer, &pool->buffers) {
		buffer = shm_buffer->buffer;
		if (!buffer || buffer->busy) {
			continue;
		}
		if (buffer->width == width && buffer->height == height) {
			buffer->busy = true;
			return buffer;
		}
		if (buffer->size >= requested_size) {
			// TODO: technically we could try to not render at all here
			//       - width <= buffer->width
			//       - height <= buffer->height
			//       - previous renderer == new renderer
#ifdef BUFFER_LOG
			fprintf(stderr,"Re-using existing shm buffer, %u buffers in total\n",
				get_buffer_count(pool));
#endif
			buffer->busy = true;
			buffer->width = width;
			buffer->height = height;
			buffer->stride = width * 4;
			update_buffer(buffer);
			return buffer;
		}
		if (buffer->buffer) {
			wl_buffer_destroy(buffer->buffer);
		}
		close(buffer->fd);
		shm_buffer->buffer = NULL;
#ifdef BUFFER_LOG
		fprintf(stderr, "Closed buffer, %u buffers in total\n", get_buffer_count(pool));
#endif
	}

	struct shm_buffer *new_shm_buffer = NULL;
	wl_array_for_each(shm_buffer, &pool->buffers) {
		if (!shm_buffer->buffer) {
			new_shm_buffer = shm_buffer;
			break;
		}
	}
	shm_buffer = new_shm_buffer;
	if (!shm_buffer) {
		shm_buffer = wl_array_add(&pool->buffers, sizeof(*shm_buffer));
		assert(shm_buffer);
	}
	shm_buffer->buffer = calloc(1, sizeof(*shm_buffer->buffer));
	assert(shm_buffer->buffer);
	buffer = shm_buffer->buffer;
	buffer->pool = pool;
	buffer->width = width;
	buffer->height = height;
	buffer->shm_format = WL_SHM_FORMAT_ARGB8888;
	buffer->stride = width * 4;
	buffer->size = buffer->stride * height;

#ifdef BUFFER_LOG
	fprintf(stderr, "Creating new buffer of size %d, %u buffers total\n",
		buffer->size, get_buffer_count(pool));
#endif

	buffer->fd = memfd_create("wayland-buffer", MFD_CLOEXEC);
	assert(buffer->fd >= 0);
	ftruncate(buffer->fd, buffer->size);
	update_buffer(buffer);
	buffer->busy = true;
	return buffer;
}

struct shm_pool *
shm_pool_create(struct client *client)
{
	struct shm_pool *pool = calloc(1, sizeof(*pool));
	wl_array_init(&pool->buffers);
	pool->client = client;
	pool->get_buffer = shm_pool_get_buffer;
	return pool;
}
