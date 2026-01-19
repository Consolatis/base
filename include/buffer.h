#pragma once

#include <stdbool.h>
#include <wayland-util.h>
#include <drm/drm_fourcc.h>

#define AVAILABLE_DROP_THRESHOLD 4

#ifdef LOG_BUFFERS
	#include "log.h"
	#define BUFFER_LOG(cond, buf, msg, ...) if (cond) { log("[%p] " msg, buf, ##__VA_ARGS__); }
#else
	#define BUFFER_LOG(cond, buf, msg, ...)
#endif

enum base_allocator_caps {
	BASE_ALLOCATOR_CAP_CPU_ACCESS    = 1u << 0,
	BASE_ALLOCATOR_CAP_EXPORT_DMABUF = 1u << 1,
	BASE_ALLOCATOR_CAP_EXPORT_SHM    = 1u << 2,
};

enum base_allocator_access_flags {
	BASE_ALLOCATOR_REQ_READ  = 1u << 0,
	BASE_ALLOCATOR_REQ_WRITE = 1u << 1,
	BASE_ALLOCATOR_REQ_RDWR  = (BASE_ALLOCATOR_REQ_READ | BASE_ALLOCATOR_REQ_WRITE),
};

/*
 * Reference counted buffer abstraction
 *
 * After creation by the allocator the buffer has a ref count of 0.
 * Callers should call buffer->lock(buffer) as soon as they receive
 * the buffer to prevent potential re-use on most allocator backends.
 *
 * buffer->unlock(buffer) reduces the ref count and if it reaches 0,
 * the buffer might either be destroyed or added back to a pool of
 * available buffers, depending on the allocator backend.
 *
 * When requesting a wl_buffer via buffer->get_wl_buffer(), a lock is
 * added and will be released automatically when the compositor sends
 * the release event of the wl_buffer.
 *
 * The serial increments each time buffer->get_pixels() with REQ_WRITE
 * is called and can be used by consumers to get notified about changes.
 * An external modification may be marked with buffer->mark_dirty().
 */

struct client;
struct base_buffer;
typedef void (*attachment_destroy_func_t)(struct base_buffer *buffer, void *key, void *value);
struct base_buffer {

	void *(*get_pixels)(struct base_buffer *buffer, enum base_allocator_access_flags access);
	void (*get_pixels_end)(struct base_buffer *buffer, void *pixels);
	struct wl_buffer *(*get_wl_buffer)(struct base_buffer *buffer, struct client *client);
	void (*lock)(struct base_buffer *buffer);
	void (*unlock)(struct base_buffer *buffer);
	void (*mark_dirty)(struct base_buffer *buffer);

	void *(*get_attachment)(struct base_buffer *buffer, void *key);
	void (*set_attachment)(struct base_buffer *buffer, void *key, void *data, attachment_destroy_func_t destroy_cb);

	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t fourcc;
	uint64_t modifier;

	uint32_t caps;
	/* Increments each time get_pixels() with REQ_WRITE is called and can be used by consumers to see changes */
	uint32_t serial;

	/* Private */
	int locks;
	struct wl_list link;
	struct attachment {
		void *key;
		void *value;
		attachment_destroy_func_t destroy_cb;
		struct attachment *next;
	} *attachments;

	/* Internal export helpers */
	int (*get_fd)(struct base_buffer *buffer);
	uint32_t (*get_byte_size)(struct base_buffer *buffer);
	/* Pool support */
	bool (*is_exact_match)(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier);
	bool (*is_close_match)(struct base_buffer *buffer, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier);
	void (*destroy)(struct base_buffer *buffer);
	/* Internal helpers */
	void (*destroy_attachments)(struct base_buffer *buffer);
	// fences?
};

struct base_allocator {
	struct base_buffer *(*create_buffer)(struct base_allocator *allocator, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier);
	void (*destroy)(struct base_allocator *allocator);
	uint32_t capabilities;
};

struct base_allocator *drm_allocator_create(int drm_fd);
struct base_allocator *gbm_allocator_create(int drm_fd);
struct base_allocator *shm_allocator_create(void);

struct gbm_bo;
struct base_buffer *gbm_allocator_wrap_gbm_bo(struct base_allocator *allocator, struct gbm_bo *bo);

/* Internal pool helpers */
struct base_buffer *base_buffer_pool_get_buffer(struct wl_list *buffers, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier);
void base_buffer_pool_cleanup(struct wl_list *buffers);
