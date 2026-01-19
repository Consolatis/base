#include <wayland-util.h>
#include "buffer.h"
#include "log.h"

static struct base_buffer *
find_close_match(struct wl_list *buffers, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	/* Tries to find last recently used close match from the back of the list */
	struct base_buffer *buffer;
	wl_list_for_each_reverse(buffer, buffers, link) {
		if (buffer->locks) {
			continue;
		}
		if (buffer->is_close_match(buffer, width, height, fourcc, modifier)) {
			BUFFER_LOG(true, buffer, "Reusing existing buffer due to close match");
			buffer->destroy_attachments(buffer);
			return buffer;
		}
	}
	return NULL;
}

static struct base_buffer *
find_exact_match(struct wl_list *buffers, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	/* Tries to find last recently used exact match from the back of the list */
	struct base_buffer *buffer;
	wl_list_for_each_reverse(buffer, buffers, link) {
		if (buffer->locks) {
			continue;
		}
		if (buffer->width == width && buffer->height == height
			&& buffer->fourcc == fourcc && buffer->modifier == modifier
			&& buffer->is_exact_match(buffer, width, height, fourcc, modifier)
		) {
			BUFFER_LOG(true, buffer, "Reusing existing buffer due to exact match");
			return buffer;
		}
	}
	return NULL;
}

struct base_buffer *
base_buffer_pool_get_buffer(struct wl_list *buffers, uint32_t width, uint32_t height, uint32_t fourcc, uint64_t modifier)
{
	struct base_buffer *buffer = find_exact_match(buffers, width, height, fourcc, modifier);
	if (!buffer) {
		buffer = find_close_match(buffers, width, height, fourcc, modifier);
	}
	return buffer;
}

void
base_buffer_pool_cleanup(struct wl_list *buffers)
{
	uint32_t available_count = 0;
	struct base_buffer *buffer, *tmp;

	wl_list_for_each(buffer, buffers, link) {
		if (!buffer->locks) {
			available_count++;
		}
	}
	if (available_count < AVAILABLE_DROP_THRESHOLD) {
		return;
	}
	uint32_t total_count = wl_list_length(buffers);

	/* Ensure we are destroying the oldest unused buffer from the front of the list */
	wl_list_for_each_safe(buffer, tmp, buffers, link) {
		if (buffer->locks) {
			continue;
		}
		total_count--;
		available_count--;
		BUFFER_LOG(true, buffer,
			"destroying buffer, now at %u/%u available buffers",
			available_count, total_count
		);
		buffer->destroy(buffer);
		if (available_count < AVAILABLE_DROP_THRESHOLD) {
			break;
		};
	}
}
