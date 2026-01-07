#pragma once

void raw_render_gradient(void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint8_t base_color);
void raw_render_checkerboard(void *pixels, uint32_t width, uint32_t height, uint32_t stride);
void raw_render_solid(void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t color);
void raw_render_y_line(void *pixels, uint32_t width, uint32_t height, uint32_t stride,
	uint32_t line_pos, uint32_t line_thickness, uint32_t line_color);
void raw_render_x_line(void *pixels, uint32_t width, uint32_t height, uint32_t stride,
	uint32_t line_pos, uint32_t line_thickness, uint32_t line_color);
