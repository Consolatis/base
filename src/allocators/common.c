#include <assert.h>
#include <stdlib.h>

#include "base.h"
#include "buffer.h"

static void *
base_buffer_common_get_attachment(struct base_buffer *buffer, void *key)
{
	for (struct attachment *att = buffer->attachments; att; att = att->next) {
		if (att->key == key) {
			return att->value;
		}
	}
	return NULL;
}

static void
base_buffer_common_set_attachment(struct base_buffer *buffer, void *key, void *value, attachment_destroy_func_t destroy_cb)
{
	struct attachment *att;
	for (att = buffer->attachments; att; att = att->next) {
		if (att->key == key) {
			att->value = value;
			att->destroy_cb = destroy_cb;
			return;
		}
	}
	att = calloc(1, sizeof(*att));
	assert(att);
	*att = (struct attachment) {
		.key = key,
		.value = value,
		.destroy_cb = destroy_cb,
	};
	if (!buffer->attachments) {
		buffer->attachments = att;
	} else {
		struct attachment *a;
		for (a = buffer->attachments; a->next; a = a->next);
		a->next = att;
	}
}

static void
_destroy_attachment(struct base_buffer *buffer, struct attachment *att)
{
	if (att->next) {
		_destroy_attachment(buffer, att->next);
	}
	if (att->destroy_cb) {
		att->destroy_cb(buffer, att->key, att->value);
	}
	free(att);
}

static void
base_buffer_common_destroy_attachments(struct base_buffer *buffer)
{
	if (buffer->attachments) {
		BUFFER_LOG(true, buffer, "Destroying attachments");
		_destroy_attachment(buffer, buffer->attachments);
	}
	buffer->attachments = NULL;
}

static struct wl_buffer *
base_buffer_common_get_wl_buffer(struct base_buffer *buffer, struct client *client)
{
	struct base_wl_buffer_manager *manager = client->buffer_manager;
	return manager->create_wl_buffer(manager, buffer);
}

static void
base_buffer_common_mark_dirty(struct base_buffer *buffer)
{
	buffer->serial++;
}

void
base_buffer_common_init(struct base_buffer *buffer)
{
	buffer->get_wl_buffer = base_buffer_common_get_wl_buffer;
	buffer->get_attachment = base_buffer_common_get_attachment;
	buffer->set_attachment = base_buffer_common_set_attachment;
	buffer->mark_dirty = base_buffer_common_mark_dirty;
	buffer->destroy_attachments = base_buffer_common_destroy_attachments;
}
