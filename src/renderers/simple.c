#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include "base.h"
#include "render.h"
#include "log.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define REPEAT_4(x) x, x, x, x
#define REPEAT_8(x)   REPEAT_4(x),   REPEAT_4(x)
#define REPEAT_16(x)  REPEAT_8(x),   REPEAT_8(x)
#define REPEAT_32(x)  REPEAT_16(x),  REPEAT_16(x)
#define REPEAT_64(x)  REPEAT_32(x),  REPEAT_32(x)
#define REPEAT_128(x) REPEAT_64(x),  REPEAT_64(x)
#define REPEAT_256(x) REPEAT_128(x), REPEAT_128(x)

void
raw_render_gradient(void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint8_t base_color)
{
	assert(stride >= width * 4);
	for (uint32_t y = 0; y < height; y++) {
		void *row = (uint8_t*)pixels + y * stride;
		for (uint32_t x = 0; x < width; x++) {
			uint8_t r = (x * 255) / width;
			uint8_t g = (y * 255) / height;
			//uint8_t b = 0x80;
			uint8_t b = base_color;
			uint32_t px = (0xffu << 24) | (r << 16) | (g << 8) | b;
			((uint32_t*)row)[x] = px;
		}
	}
}

void
raw_render_y_line(void *pixels, uint32_t width, uint32_t height, uint32_t stride,
		uint32_t line_pos, uint32_t line_thickness, uint32_t line_color)
{
	assert(stride >= width * 4);
	const uint32_t line_start = MAX((int)line_pos - (int)line_thickness / 2, 0);
	const uint32_t line_end = MIN(width, line_pos + line_thickness / 2);
	//log("rendering from %u to %u", line_start, line_end);
	// FIXME: doesn't render the right-most line_thickness pixels for whatever reason

	for (uint32_t y = 0; y < height; y++) {
		void *row = pixels + y * stride;
		for (uint32_t x = line_start; x < line_end; x++) {
			((uint32_t *)row)[x] = line_color;
		}
	}
}

void
raw_render_x_line(void *pixels, uint32_t width, uint32_t height, uint32_t stride,
		uint32_t line_pos, uint32_t line_thickness, uint32_t line_color)
{
	assert(stride >= width * 4);
	const uint32_t line_start = MAX((int)line_pos - (int)line_thickness / 2, 0);
	const uint32_t line_end = MIN(height, line_pos + line_thickness / 2);

	const uint32_t pattern[16] = {
		REPEAT_16(line_color),
	};

	for (uint32_t y = line_start; y < line_end; y++) {
		void *row = pixels + y * stride;
		for (uint32_t bx = 0; bx < stride; bx += sizeof(pattern)) {
			memcpy(row + bx, pattern, MIN(sizeof(pattern), stride - bx));
		}
	}
}

void
raw_render_solid(void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t color)
{
	assert(stride >= width * 4);

	/* All channels the same? use memset() */
	const uint8_t channel = color & 0xff;
	if (((color >> 8) & 0xff) == channel
			&& ((color >> 16) & 0xff) == channel
			&& ((color >> 24) & 0xff) == channel) {
		memset(pixels, channel, height * stride);
		return;
	}

	/* Otherwise memcpy 16 pixels / 64 byte at a time */
	const uint32_t pattern[16] = { REPEAT_16(color) };
	const void *end_ptr = pixels + height * stride;
	for (void *p = pixels; p < end_ptr; p += sizeof(pattern)) {
		memcpy(p, pattern, MIN(sizeof(pattern), end_ptr - p));
	}
	return;

	/* Naive implementation, dead code */
	for (uint32_t y = 0; y < height; y++) {
		void *row = (uint8_t *)pixels + y * stride;
		for (uint32_t x = 0; x < width; x++) {
			((uint32_t *)row)[x] = color;
		}
	}
}

void
raw_render_checkerboard(void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
	assert(stride >= width * 4);

	#define RECT_SIZE 32
	const uint32_t color1 = 0xFFEEEEEE;
	const uint32_t color2 = 0xFF666666;

	const uint32_t pattern[RECT_SIZE * 2] = {
	#if RECT_SIZE == 1
		color1, color2,
		#define RECT_ENABLED(y) (y & 1)
	#elif RECT_SIZE == 2
		color1, color1,
		color2, color2,
		#define RECT_ENABLED(y) ((y >> 1) & 1)
	#elif RECT_SIZE == 4
		REPEAT_4(color1),
		REPEAT_4(color2),
		#define RECT_ENABLED(y) ((y >> 2) & 1)
	#elif RECT_SIZE == 8
		REPEAT_8(color1),
		REPEAT_8(color2),
		#define RECT_ENABLED(y) ((y >> 3) & 1)
	#elif RECT_SIZE == 16
		REPEAT_16(color1),
		REPEAT_16(color2),
		#define RECT_ENABLED(y) ((y >> 4) & 1)
	#elif RECT_SIZE == 32
		REPEAT_32(color1),
		REPEAT_32(color2),
		#define RECT_ENABLED(y) ((y >> 5) & 1)
	#elif RECT_SIZE == 64
		REPEAT_64(color1),
		REPEAT_64(color2),
		#define RECT_ENABLED(y) ((y >> 6) & 1)
	#elif RECT_SIZE == 128
		REPEAT_128(color1),
		REPEAT_128(color2),
		#define RECT_ENABLED(y) ((y >> 7) & 1)
	#elif RECT_SIZE == 256
		REPEAT_256(color1),
		REPEAT_256(color2),
		#define RECT_ENABLED(y) ((y >> 8) & 1)
	#else
		#error "Unsupported rect size"
	#endif
	};

	const void *byte_pattern = pattern;
	const uint32_t byte_width = width * 4;
	const uint32_t pattern_half = sizeof(pattern) >> 1;
	for (uint32_t y = 0; y < height; y++) {
		void *row = pixels + y * stride;
		const uint32_t shift = RECT_ENABLED(y) * pattern_half;
		/* Write half of the pattern */
		if (shift) {
			memcpy(row, byte_pattern + shift, MIN(shift, byte_width));
		}
		/* Write alternating pattern */
		for (uint32_t bx = shift; bx < byte_width; bx += sizeof(pattern)) {
			memcpy(row + bx, pattern, MIN(sizeof(pattern), byte_width - bx));
		}
	}
}

void
renderer_shm_solid(struct buffer *buffer, uint32_t pixel_value)
{
	assert(buffer->shm_format == WL_SHM_FORMAT_ARGB8888);

	void *data = mmap(NULL, buffer->size,
		PROT_WRITE, MAP_SHARED, buffer->fd, 0);
	assert(data && data != MAP_FAILED);
	raw_render_solid(data, buffer->width, buffer->height, buffer->stride, pixel_value);
	munmap(data, buffer->size);
}

void
renderer_shm_solid_black(struct buffer *buffer)
{
	renderer_shm_solid(buffer, 0xff000000);
}

void
renderer_shm_checkerboard(struct buffer *buffer)
{
	assert(buffer->shm_format == WL_SHM_FORMAT_ARGB8888);

	void *data = mmap(NULL, buffer->size,
		PROT_WRITE, MAP_SHARED, buffer->fd, 0);
	assert(data && data != MAP_FAILED);
	raw_render_checkerboard(data, buffer->width, buffer->height, buffer->stride);
	munmap(data, buffer->size);
}
