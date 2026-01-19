#pragma once

#include <drm/drm_fourcc.h>
#include <GLES2/gl2.h>
#include <wayland-client-protocol.h>

#define format_for_each(var, formats) for(const struct fourcc_details *(var) = (formats); (var)->fourcc != DRM_FORMAT_INVALID; (var)++)

static const struct fourcc_details {
	uint32_t bytes_per_pixel;
	uint32_t fourcc;
	struct {
		uint32_t internal;
		uint32_t format;
		uint32_t component_type;
	} gl_fmt;
} formats[] = {
	{ 4, DRM_FORMAT_XRGB8888, { GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE } },
	{ 4, DRM_FORMAT_ARGB8888, { GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE } },
	{ 4, DRM_FORMAT_ABGR8888, { GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE } }, /* FIXME: verify gl */
	{ 4, DRM_FORMAT_XBGR8888, { GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE } }, /* FIXME: verify gl */
	{ 3, DRM_FORMAT_RGB888,   { GL_RGB,  GL_RGB,  GL_UNSIGNED_BYTE } },
	{ 2, DRM_FORMAT_RGB565,   { GL_RGB,  GL_RGB,  GL_UNSIGNED_SHORT_5_6_5 } },
	/* sentinel */
	{ 0, DRM_FORMAT_INVALID }
};


static inline uint32_t
fourcc_get_stride(uint32_t fourcc, uint32_t width)
{
	format_for_each(format, formats) {
		if (format->fourcc == fourcc) {
			return format->bytes_per_pixel * width;
		}
	}
	return 0;
}

static inline uint32_t
fourcc_get_bytes_per_pixel(uint32_t fourcc)
{
	format_for_each(format, formats) {
		if (format->fourcc == fourcc) {
			return format->bytes_per_pixel;
		}
	}
	return 0;
}

static inline bool
fourcc_to_gl_format(uint32_t fourcc, uint32_t *gl_internal, uint32_t *gl_format, uint32_t *gl_component_type)
{
	format_for_each(format, formats) {
		if (format->fourcc == fourcc) {
			*gl_internal = format->gl_fmt.internal;
			*gl_format = format->gl_fmt.format;
			*gl_component_type = format->gl_fmt.component_type;
			return true;
		}
	}
	return false;
}

static inline uint32_t
fourcc_to_shm_format(uint32_t fourcc)
{
	/* SHM formats are synced with drm_fourcc, exceptions are the first two */
	switch(fourcc) {
	case DRM_FORMAT_ARGB8888:
		return WL_SHM_FORMAT_ARGB8888;
	case DRM_FORMAT_XRGB8888:
		return WL_SHM_FORMAT_XRGB8888;
	default:
		return fourcc;
	}
}

static inline uint32_t
fourcc_from_shm_format(uint32_t shm_format)
{
	/* SHM formats are synced with drm_fourcc, exceptions are the first two */
	switch(shm_format) {
	case WL_SHM_FORMAT_ARGB8888:
		return DRM_FORMAT_ARGB8888;
	case WL_SHM_FORMAT_XRGB8888:
		return DRM_FORMAT_XRGB8888;
	default:
		return shm_format;
	}
}
