#include <assert.h>
#include <gbm.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-util.h>

#include "base.h"
#include "buffer.h"
#include "log.h"

/* Defined in src/allocators/common.c */
void base_buffer_common_init(struct base_buffer *buffer);

struct gbm_bo_allocator {
	struct base_allocator base;
	struct gbm_device *device;
	struct wl_list buffers;
};

struct gbm_bo_allocator_buffer {
	struct base_buffer base;

	int fd;
	struct gbm_bo *bo;
	uint32_t byte_size;
	struct gbm_bo_allocator *allocator;
	void *map_data;
};

static void *
buffer_get_pixels(struct base_buffer *buffer, uint32_t access)
{
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	assert(!gbm_buffer->map_data);

	uint32_t flags = 0;
	if (access & BASE_ALLOCATOR_REQ_READ) {
		flags |= GBM_BO_TRANSFER_READ;
	}
	if (access & BASE_ALLOCATOR_REQ_WRITE) {
		flags |= GBM_BO_TRANSFER_WRITE;
	}
	uint32_t stride;
	void *pixels = gbm_bo_map(gbm_buffer->bo, /*x*/0, /*y*/0,
		buffer->width, buffer->height,
		flags, &stride, &gbm_buffer->map_data
	);
	if (!pixels || pixels == MAP_FAILED) {
		log("Failed to mmap gbm_bo");
		return NULL;
	}
	if (flags & GBM_BO_TRANSFER_WRITE) {
		buffer->serial++;
	}
	return pixels;
}

static void
buffer_get_pixels_end(struct base_buffer *buffer, void *pixels)
{
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	assert(gbm_buffer->map_data);
	gbm_bo_unmap(gbm_buffer->bo, gbm_buffer->map_data);
	gbm_buffer->map_data = NULL;
}

static void
buffer_lock(struct base_buffer *buffer)
{
	buffer->locks++;
}

static void
buffer_destroy(struct base_buffer *buffer)
{
	assert(!buffer->locks);
	buffer->destroy_attachments(buffer);
	wl_list_remove(&buffer->link);
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	close(gbm_buffer->fd);
	gbm_bo_destroy(gbm_buffer->bo);
	free(gbm_buffer);
}

static void
buffer_unlock(struct base_buffer *buffer)
{
	assert(buffer->locks >= 1);
	buffer->locks--;

	//BUFFER_LOG(true, buffer, "unlock buffer, now at %u", buffer->locks);
	if (buffer->locks) {
		return;
	}

	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	base_buffer_pool_cleanup(&gbm_buffer->allocator->buffers);
}

static bool
buffer_is_exact_match(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	return buffer->width == width && buffer->height == height
		&& buffer->fourcc == fourcc && buffer->modifier == modifier;
}

static bool
buffer_is_close_match(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	return false;
}

static uint32_t
buffer_get_byte_size(struct base_buffer *buffer)
{
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	return gbm_buffer->byte_size;
}

static int
buffer_get_fd(struct base_buffer *buffer)
{
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)buffer;
	return gbm_buffer->fd;
}

struct base_buffer *
gbm_allocator_wrap_gbm_bo(struct base_allocator *allocator, struct gbm_bo *bo)
{
	if (!bo) {
		return NULL;
	}

	struct gbm_bo_allocator *alloc = (void *)allocator;
	struct gbm_bo_allocator_buffer *gbm_buffer = calloc(1, sizeof(*gbm_buffer));
	assert(gbm_buffer);

	*gbm_buffer = (struct gbm_bo_allocator_buffer) {
		.base = {
			/* API */
			.get_pixels = buffer_get_pixels,
			.get_pixels_end = buffer_get_pixels_end,
			.lock = buffer_lock,
			.unlock = buffer_unlock,

			/* Props */
			.caps = allocator->capabilities,
			.width = gbm_bo_get_width(bo),
			.height = gbm_bo_get_height(bo),
			.stride = gbm_bo_get_stride(bo),
			.fourcc = gbm_bo_get_format(bo),
			.modifier = gbm_bo_get_modifier(bo),

			/* Internal export helpers */
			.get_fd = buffer_get_fd,
			.get_byte_size = buffer_get_byte_size,

			/* Pool support */
			.is_exact_match = buffer_is_exact_match,
			.is_close_match = buffer_is_close_match,
			.destroy = buffer_destroy,
		},
		.bo = bo,
		.fd = gbm_bo_get_fd(bo),
		.allocator = alloc,
		.byte_size = gbm_bo_get_stride(bo) * gbm_bo_get_height(bo),
	};
	base_buffer_common_init(&gbm_buffer->base);

	BUFFER_LOG(true, gbm_buffer, "Wrapped gbm buffer with format 0x%08x (modifier 0x%016lx)", gbm_buffer->base.fourcc, gbm_buffer->base.modifier);

	return &gbm_buffer->base;
}

static struct base_buffer *
allocator_create_buffer(struct base_allocator *allocator, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	struct gbm_bo_allocator *alloc = (void *)allocator;
	struct gbm_bo_allocator_buffer *gbm_buffer = (void *)base_buffer_pool_get_buffer(
		&alloc->buffers, width, height, fourcc, modifier);

	if (gbm_buffer) {
		/* Ensure recently used buffers are at the end of the list */
		struct base_buffer *buffer = &gbm_buffer->base;
		wl_list_remove(&buffer->link);
		wl_list_insert(alloc->buffers.prev, &buffer->link);
		return buffer;
	}

	uint32_t flags = 0;
	struct gbm_bo *bo = gbm_bo_create_with_modifiers2(alloc->device, width, height, fourcc, &modifier, 1, flags);
	if (!bo) {
		perror("Failed to create gbm buffer");
		return NULL;
	}

	struct base_buffer *buffer = gbm_allocator_wrap_gbm_bo(allocator, bo);
	BUFFER_LOG(true, buffer, "Created new gbm buffer with format 0x%08x (modifier 0x%016lx)", fourcc, modifier);
	wl_list_insert(alloc->buffers.prev, &buffer->link);
	return buffer;
}

static void
allocator_destroy(struct base_allocator *allocator)
{
	struct gbm_bo_allocator *alloc = (void *)allocator;

	struct base_buffer *buffer, *tmp;
	wl_list_for_each_safe(buffer, tmp, &alloc->buffers, link) {
		if (buffer->locks) {
			log("Warning: locked buffer found in destroying gbm_allocator");
		} else {
			buffer->destroy(buffer);
		}
	}
	gbm_device_destroy(alloc->device);
	free(allocator);
}

struct base_allocator *
gbm_allocator_create(int drm_fd)
{
	struct gbm_bo_allocator *alloc = calloc(1, sizeof(*alloc));
	assert(alloc);
	*alloc = (struct gbm_bo_allocator) {
		.base = {
			.create_buffer = allocator_create_buffer,
			.destroy = allocator_destroy,
			.capabilities = BASE_ALLOCATOR_CAP_EXPORT_DMABUF | BASE_ALLOCATOR_CAP_CPU_ACCESS,
		},
		.device = gbm_create_device(drm_fd),
	};
	if (!alloc->device) {
		perror("Failed to create gbm device");
		free(alloc);
		return NULL;
	}
	wl_list_init(&alloc->buffers);
	return &alloc->base;
}
