#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "base.h"
#include "buffer.h"
#include "fourcc.h"
#include "log.h"

#include "linux-dmabuf-v1.xml.h"

struct base_dmabuf_format {
	uint32_t fourcc;
	uint32_t padding;
	uint64_t modifier;
};

struct wl_buffer_manager {
	struct base_wl_buffer_manager base;
	struct wl_buffer_manager_dmabuf {
		struct zwp_linux_dmabuf_v1 *global;
		struct zwp_linux_dmabuf_feedback_v1 *feedback;
		struct base_dmabuf_format *format_table;
		uint32_t format_table_size;
		uint32_t format_table_count;
	} dmabuf;
	struct {
		struct wl_shm *global;
	} shm;
};

static void
set_drm_fd(struct client *client, dev_t dev_id)
{
	if (client->drm_fd >= 0) {
		return;
	}

	drmDevice *device;
	if (drmGetDeviceFromDevId(dev_id, /*flags*/ 0, &device) < 0) {
		perror("Failed fetching device information");
		return;
	}
	if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
		client->drm_fd = open(device->nodes[DRM_NODE_RENDER], O_RDWR);
		if (client->drm_fd < 0) {
			perror("Failed opening drm render device");
		} else {
			log("Opened drm render device %s", device->nodes[DRM_NODE_RENDER]);
		}
	}
	drmFreeDevice(&device);
}

static void
dump_dev_t(dev_t dev_id)
{
#if 0
	drmDevice *device;
	if (drmGetDeviceFromDevId(dev_id, /*flags*/ 0, &device) < 0) {
		perror("Failed fetching device information");
		return;
	}
	if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
		log("   - %s", device->nodes[DRM_NODE_RENDER]);
	} else {
		log("   - no render node found");
	}
	drmFreeDevice(&device);
#endif
}

static void
feedback_handle_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback)
{
	struct wl_buffer_manager *manager = data;
	struct wl_buffer_manager_dmabuf *dmabuf = &manager->dmabuf;

	zwp_linux_dmabuf_feedback_v1_destroy(dmabuf->feedback);
	dmabuf->feedback = NULL;

	if (dmabuf->format_table) {
		munmap(dmabuf->format_table, dmabuf->format_table_size);
		dmabuf->format_table = NULL;
		dmabuf->format_table_size = 0;
		dmabuf->format_table_count = 0;
	}
}

static void
feedback_handle_format_table(void *data,
		struct zwp_linux_dmabuf_feedback_v1 *feedback, int32_t fd, uint32_t size)
{
	struct wl_buffer_manager *manager = data;
	struct wl_buffer_manager_dmabuf *dmabuf = &manager->dmabuf;

	if (dmabuf->format_table) {
		munmap(dmabuf->format_table, dmabuf->format_table_size);
	}
	dmabuf->format_table = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, /*offset*/0);
	if (dmabuf->format_table == MAP_FAILED) {
		dmabuf->format_table = NULL;
		perror("Failed to map dmabuf format table");
		return;
	}
	dmabuf->format_table_size = size;
	dmabuf->format_table_count = size / sizeof(*dmabuf->format_table);
}

static void
feedback_handle_main_device(void *data,
		struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device)
{
	struct wl_buffer_manager *manager = data;
	dev_t dev_id;
	if (sizeof(dev_id) != device->size) {
		log("Invalid main device received from compositor");
		return;
	}
	memcpy(&dev_id, device->data, device->size);
	dump_dev_t(dev_id);
	set_drm_fd(manager->base.client, dev_id);
}

static void
feedback_handle_tranche_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *feedback)
{
	//log("Feedback tranche done");
}


static void
feedback_handle_tranche_target_device(void *data,
		struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device)
{
	//log("Feedback tranche target device");
	dev_t dev_id;
	if (sizeof(dev_id) != device->size) {
		log("Invalid main device received from compositor");
		return;
	}
	memcpy(&dev_id, device->data, device->size);
	dump_dev_t(dev_id);
}

static void
feedback_handle_tranche_formats(void *data,
		struct zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *indices)
{
	struct wl_buffer_manager *manager = data;
	struct wl_buffer_manager_dmabuf *dmabuf = &manager->dmabuf;
	if (!manager->dmabuf.format_table) {
		log("Tranche without earlier format_table");
		return;
	}
	return;
	log("Got tranche:");
	uint16_t *index;
	struct base_dmabuf_format *format;
	wl_array_for_each(index, indices) {
		if (*index >= dmabuf->format_table_count) {
			log("Invalid format index received: %u >= %u", *index, dmabuf->format_table_count)
			continue;
		}
		format = &dmabuf->format_table[*index];
		log(" - 0x%08x (modifier 0x%016lx)", format->fourcc, format->modifier);
	}
}

static void
feedback_handle_tranche_flags(void *data,
		struct zwp_linux_dmabuf_feedback_v1 *feedback, uint32_t flags)
{
	//log("Feedback tranche flags");
}

static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
	.done = feedback_handle_done,
	.format_table = feedback_handle_format_table,
	.main_device = feedback_handle_main_device,
	.tranche_done = feedback_handle_tranche_done,
	.tranche_target_device = feedback_handle_tranche_target_device,
	.tranche_formats = feedback_handle_tranche_formats,
	.tranche_flags = feedback_handle_tranche_flags,
};

static void
handle_global(struct client *client, void *data, struct wl_registry *registry,
		const char *iface_name, uint32_t global, uint32_t version)
{
	struct wl_buffer_manager *manager = data;
	if (!manager->dmabuf.global && !strcmp(iface_name, zwp_linux_dmabuf_v1_interface.name)) {
		struct wl_buffer_manager_dmabuf *dmabuf = &manager->dmabuf;
		dmabuf->global = wl_registry_bind(registry, global, &zwp_linux_dmabuf_v1_interface, version);
		dmabuf->feedback = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf->global);
		zwp_linux_dmabuf_feedback_v1_add_listener(dmabuf->feedback, &feedback_listener, manager);
	}
	if (!manager->shm.global && !strcmp(iface_name, wl_shm_interface.name)) {
		manager->shm.global = wl_registry_bind(registry, global, &wl_shm_interface, version);
	}
}

static struct wl_buffer *
shm_create_wl_buffer(struct wl_buffer_manager *manager, struct base_buffer *buffer)
{
	if (!manager->shm.global) {
		log("Compositor does not provide SHM support");
		return NULL;
	}
	const int fd = buffer->get_fd(buffer);
	const uint32_t byte_size  = buffer->get_byte_size(buffer);
	const int offset = 0;

	struct wl_shm_pool *shm_pool = wl_shm_create_pool(manager->shm.global, fd, byte_size);
	struct wl_buffer * wl_buffer = wl_shm_pool_create_buffer(shm_pool, offset,
		buffer->width, buffer->height, buffer->stride,
		fourcc_to_shm_format(buffer->fourcc)
	);
	wl_shm_pool_destroy(shm_pool);
	return wl_buffer;
}

static struct wl_buffer *
dmabuf_create_wl_buffer(struct wl_buffer_manager *manager, struct base_buffer *buffer)
{
	if (!manager->dmabuf.global) {
		log("Compositor does not provide dmabuf support");
		return NULL;
	}

	const int fd = buffer->get_fd(buffer);
	const uint32_t plane_idx = 0; // TODO: maybe support multi-planar formats
	const uint32_t offset = 0;
	const uint32_t mod_hi = buffer->modifier >> 32;
	const uint32_t mod_low = buffer->modifier & UINT32_MAX;
	const uint32_t flags = 0;

	struct zwp_linux_buffer_params_v1 *params =
		zwp_linux_dmabuf_v1_create_params(manager->dmabuf.global);
	zwp_linux_buffer_params_v1_add(params, fd, plane_idx, offset, buffer->stride, mod_hi, mod_low);
	struct wl_buffer *wl_buffer = zwp_linux_buffer_params_v1_create_immed(
		params, buffer->width, buffer->height, buffer->fourcc, flags
	);
	zwp_linux_buffer_params_v1_destroy(params);
	return wl_buffer;
}

static void
cb_attachment_destroy(struct base_buffer *buffer, void *key, void *value)
{
	BUFFER_LOG(true, buffer, "Destroying wl_buffer");
	struct wl_buffer *wl_buffer = value;
	wl_buffer_destroy(wl_buffer);
}

static void
handle_wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct base_buffer *buffer = data;
	buffer->unlock(buffer);
}

static struct wl_buffer_listener wl_buffer_listener = {
	.release = handle_wl_buffer_release,
};

static struct wl_buffer *
buffer_manager_create_wl_buffer(struct base_wl_buffer_manager *_manager, struct base_buffer *buffer)
{
	struct wl_buffer_manager *manager = (void *)_manager;
	struct wl_buffer *wl_buffer = buffer->get_attachment(buffer, manager);
	if (wl_buffer) {
		goto buffer_reuse;
	}

	BUFFER_LOG(true, buffer, "Creating new wl_buffer");
	if (buffer->caps & BASE_ALLOCATOR_CAP_EXPORT_DMABUF) {
		wl_buffer = dmabuf_create_wl_buffer(manager, buffer);
		if (wl_buffer) {
			goto buffer_created;
		} else {
			log("Failed to import dmabuf buffer into compositor, falling back to SHM");
		}
	}
	if (buffer->caps & BASE_ALLOCATOR_CAP_EXPORT_SHM) {
		wl_buffer = shm_create_wl_buffer(manager, buffer);
		if (wl_buffer) {
			goto buffer_created;
		} else {
			log("Failed to import shm buffer into compositor");
		}
	}
	if (buffer->caps & BASE_ALLOCATOR_CAP_CPU_ACCESS) {
		/*
		 * TODO: Create own GBM buffer and memcpy the original buffer into it.
		 *       Not quite sure how to synchronize updates though. So for now
		 *       just do nothing and bail out.
		 */
	}

	return NULL;

buffer_created:
	wl_buffer_add_listener(wl_buffer, &wl_buffer_listener, buffer);
	buffer->set_attachment(buffer, manager, wl_buffer, cb_attachment_destroy);

buffer_reuse:
	buffer->lock(buffer);
	return wl_buffer;
}

struct base_wl_buffer_manager *
base_wl_buffer_manager_create(struct client *client)
{
	struct wl_buffer_manager *manager = calloc(1, sizeof(*manager));
	assert(manager);
	*manager = (struct wl_buffer_manager) {
		.base = {
			.client = client,
			.create_wl_buffer = buffer_manager_create_wl_buffer,
		},
	};
	client->add_handler(client, (struct client_handler){
		.registry = handle_global,
		.data = manager,
	});
	return &manager->base;
}
