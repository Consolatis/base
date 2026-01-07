#pragma once

#include <drm/drm_fourcc.h>
#include <stdbool.h>
#include <wayland-util.h>
#include <xf86drmMode.h>

struct drm;
struct gbm_bo;

enum buffer_type {
	DRM_DUMB_BUFFER,
	DRM_GBM_BUFFER,
};

struct drm_buffer {
	enum buffer_type type;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t fourcc;
	void (*destroy)(struct drm_buffer *buffer);
	/* Private */
	struct drm *drm;
	//uint32_t handle;
	uint32_t fb_id;
};

struct drm_dumb_buffer {
	struct drm_buffer base;
	void *pixels;
	/* Private */
	struct drm_mode_create_dumb req;
};

struct drm_output_mode {
	struct wl_list link;
	uint32_t width;
	uint32_t height;
	uint32_t refresh;
	bool preferred;
	/* Private */
	drmModeModeInfo mode;
};
struct drm_output {
	struct wl_list link;
	struct drm *drm;
	struct wl_list modes;
	struct drm_output_mode *mode;
	void *data;
	bool (*set_mode)(struct drm_output *output, struct drm_output_mode *mode, struct drm_buffer *buffer);
	bool (*set_buffer)(struct drm_output *output, struct drm_buffer *buffer, bool block);

	// FIXME: Move over to usual callback infrastructure
	void (*on_frame_presented)(struct drm_output *output);

	/* Private */
	uint32_t requested_pageflip_fb_id;
	uint32_t connector_id;
	uint32_t encoder_id;
	uint32_t crtc_id;
	uint32_t plane_id;
	struct {
		struct {
			uint32_t mode;
			uint32_t active;
		} crtc;
		struct {
			uint32_t crtc;
		} connector;
		struct {
			uint32_t fb;
			uint32_t crtc;
			uint32_t src_x;
			uint32_t src_y;
			uint32_t src_w;
			uint32_t src_h;
			uint32_t crtc_x;
			uint32_t crtc_y;
			uint32_t crtc_w;
			uint32_t crtc_h;
		} plane;
	} props;
};

struct drm {
	int fd;
	struct wl_list outputs;

	struct drm_buffer *(*import_gbm_bo)(struct drm *drm, struct gbm_bo *gbm_bo);
	bool (*read_events)(struct drm *drm, bool block);
	void (*destroy)(struct drm *drm);

	struct {
		struct drm_dumb_buffer *(*create_dumb_buffer)(struct drm *drm, uint32_t width, uint32_t height, uint32_t format);
	} allocator;

};

struct drm *drm_create(int fd);
