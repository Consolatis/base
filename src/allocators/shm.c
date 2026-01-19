#define _GNU_SOURCE /* required for memfd_create() */

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-util.h>

#include "base.h"
#include "buffer.h"
#include "fourcc.h"
#include "log.h"

/* Defined in src/allocators/common.c */
void base_buffer_common_init(struct base_buffer *buffer);

struct shm_allocator {
	struct base_allocator base;
	struct wl_list buffers;
};

struct shm_allocator_buffer {
	struct base_buffer base;

	int fd;
	uint32_t byte_size;
	struct shm_allocator *allocator;
};

static void *
buffer_get_pixels(struct base_buffer *buffer, uint32_t access)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	int flags = 0;
	if (access & BASE_ALLOCATOR_REQ_READ) {
		flags |= PROT_READ;
	}
	if (access & BASE_ALLOCATOR_REQ_WRITE) {
		flags |= PROT_WRITE;
	}
	void *pixels = mmap(NULL, shm_buffer->byte_size, flags,
		MAP_SHARED, shm_buffer->fd, /*offset*/0);
	if (!pixels || pixels == MAP_FAILED) {
		perror("Failed to map SHM buffer");
		return NULL;
	}
	if (flags & PROT_WRITE) {
		buffer->serial++;
	}
	return pixels;
}

static void
buffer_get_pixels_end(struct base_buffer *buffer, void *pixels)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	munmap(pixels, shm_buffer->byte_size);
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
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	close(shm_buffer->fd);
	free(shm_buffer);
}

static void
buffer_unlock(struct base_buffer *buffer)
{
	assert(buffer->locks >= 1);
	buffer->locks--;

	if (buffer->locks) {
		return;
	}

	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	base_buffer_pool_cleanup(&shm_buffer->allocator->buffers);
}

static bool
buffer_is_exact_match(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	const uint32_t b_size = fourcc_get_stride(fourcc, width) * height;

	return buffer->width == width && buffer->height == height
		&& buffer->fourcc == fourcc && buffer->modifier == modifier
		&& shm_buffer->byte_size == b_size;
}

static bool
buffer_is_close_match(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	const uint32_t b_size = fourcc_get_stride(fourcc, width) * height;

	/* Hrm. modifying the buffer here is kind of a weird side effect */
	if (shm_buffer->byte_size >= b_size) {
		buffer->width = width;
		buffer->height = height;
		buffer->fourcc = fourcc;
		buffer->stride = fourcc_get_stride(fourcc, width);
		return true;
	}
	return false;
}

static uint32_t
buffer_get_byte_size(struct base_buffer *buffer)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	return shm_buffer->byte_size;
}

static int
buffer_get_fd(struct base_buffer *buffer)
{
	struct shm_allocator_buffer *shm_buffer = (void *)buffer;
	return shm_buffer->fd;
}

static struct base_buffer *
alloc_create_buffer(struct base_allocator *allocator, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	struct shm_allocator *alloc = (void *)allocator;
	struct shm_allocator_buffer *shm_buffer = NULL;

	if (!fourcc_get_bytes_per_pixel(fourcc)) {
		log("Failed to parse fourcc format 0x%x", fourcc);
		return NULL;
	}

	if (modifier != DRM_FORMAT_MOD_LINEAR) {
		log("SHM allocator only supports DRM_FORMAT_MOD_LINEAR modifier");
		return NULL;
	}

	shm_buffer = (void *)base_buffer_pool_get_buffer(
		&alloc->buffers, width, height, fourcc, modifier);
	if (shm_buffer) {
		/* Ensure recently used buffers are at the end of the list */
		struct base_buffer *buffer = &shm_buffer->base;
		wl_list_remove(&buffer->link);
		wl_list_insert(alloc->buffers.prev, &buffer->link);
		return buffer;
	}

	shm_buffer = calloc(1, sizeof(*shm_buffer));
	assert(shm_buffer);
	BUFFER_LOG(true, shm_buffer, "Creating new shm buffer with format 0x%08x (modifier 0x%016lx)", fourcc, modifier);
	*shm_buffer = (struct shm_allocator_buffer) {
		.base = {
			/* API */
			.get_pixels = buffer_get_pixels,
			.get_pixels_end = buffer_get_pixels_end,
			.lock = buffer_lock,
			.unlock = buffer_unlock,

			/* Props */
			.caps = allocator->capabilities,
			.width = width,
			.height = height,
			.fourcc = fourcc,
			.modifier = DRM_FORMAT_MOD_LINEAR,
			.stride = fourcc_get_stride(fourcc, width),

			/* Internal export helpers */
			.get_fd = buffer_get_fd,
			.get_byte_size = buffer_get_byte_size,

			/* Pool support */
			.is_exact_match = buffer_is_exact_match,
			.is_close_match = buffer_is_close_match,
			.destroy = buffer_destroy,
		},
		.allocator = alloc,
		.fd = memfd_create("wayland-buffer", MFD_CLOEXEC),
		.byte_size = fourcc_get_stride(fourcc, width) * height,
	};
	base_buffer_common_init(&shm_buffer->base);

	assert(shm_buffer->fd >= 0);
	ftruncate(shm_buffer->fd, shm_buffer->byte_size);
	wl_list_insert(alloc->buffers.prev, &shm_buffer->base.link);

	return &shm_buffer->base;
}

static void
alloc_destroy(struct base_allocator *allocator)
{
	struct shm_allocator *alloc = (void *)allocator;

	struct base_buffer *buffer, *tmp;
	wl_list_for_each_safe(buffer, tmp, &alloc->buffers, link) {
		if (buffer->locks) {
			log("Warning: locked buffer found in destroying shm_allocator");
		} else {
			buffer->destroy(buffer);
		}
	}
	free(allocator);
}

struct base_allocator *
shm_allocator_create(void)
{
	struct shm_allocator *alloc = calloc(1, sizeof(*alloc));
	assert(alloc);
	alloc->base = (struct base_allocator) {
		.create_buffer = alloc_create_buffer,
		.destroy = alloc_destroy,
		.capabilities = BASE_ALLOCATOR_CAP_CPU_ACCESS | BASE_ALLOCATOR_CAP_EXPORT_SHM,
	};
	wl_list_init(&alloc->buffers);
	return &alloc->base;
}
