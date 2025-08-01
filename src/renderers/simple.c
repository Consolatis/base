#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include "base.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define REPEAT_4(x) x, x, x, x
#define REPEAT_8(x)   REPEAT_4(x),   REPEAT_4(x)
#define REPEAT_16(x)  REPEAT_8(x),   REPEAT_8(x)
#define REPEAT_32(x)  REPEAT_16(x),  REPEAT_16(x)
#define REPEAT_64(x)  REPEAT_32(x),  REPEAT_32(x)
#define REPEAT_128(x) REPEAT_64(x),  REPEAT_64(x)
#define REPEAT_256(x) REPEAT_128(x), REPEAT_128(x)

void
renderer_shm_solid(struct buffer *buffer, uint32_t pixel_value)
{
	assert(buffer->stride >= buffer->width * 4);
	assert(buffer->shm_format == WL_SHM_FORMAT_ARGB8888);

	void *data = mmap(NULL, buffer->size,
		PROT_WRITE, MAP_SHARED, buffer->fd, 0);
	assert(data && data != MAP_FAILED);

	const uint8_t channel = pixel_value & 0xff;
	if (((pixel_value >> 8) & 0xff) == channel
			&& ((pixel_value >> 16) & 0xff) == channel
			&& ((pixel_value >> 24) & 0xff) == channel) {
		memset(data, channel, buffer->height * buffer->stride);
		goto clean_up;
	}

	const uint32_t pattern[16] = {
		REPEAT_16(pixel_value),
	};

	const void *end_ptr = data + buffer->size;
	for (void *p = data; p < end_ptr; p += sizeof(pattern)) {
		memcpy(p, pattern, MIN(sizeof(pattern), end_ptr - p));
	}

clean_up:
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
	#define RECT_SIZE 8
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

	assert(buffer->stride >= buffer->width * 4);
	assert(buffer->shm_format == WL_SHM_FORMAT_ARGB8888);

	void *data = mmap(NULL, buffer->size,
		PROT_WRITE, MAP_SHARED, buffer->fd, 0);
	assert(data && data != MAP_FAILED);

	const void *byte_pattern = pattern;
	const uint32_t stride = buffer->stride;
	const uint32_t height = buffer->height;
	const uint32_t byte_width = buffer->width * 4;
	const uint32_t pattern_half = sizeof(pattern) >> 1;
	for (uint32_t y = 0; y < height; y++) {
		void *row = data + y * stride;
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

	munmap(data, buffer->size);
}
