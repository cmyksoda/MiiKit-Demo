// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GFX_H
#define GFX_H

#include <gccore.h>

/* Screen space is 480 tall and 640 wide, or 853 on a 16:9 Wii. */
extern float gfx_w, gfx_h;
extern int gfx_widescreen;

#define RGBA(r, g, b, a) ((GXColor) { r, g, b, a })

void gfx_init(void);
void gfx_frame_begin(GXColor clear);
void gfx_frame_end(void);

/* Back to flat drawing after mii_draw, which changes most GX state. */
void gfx_2d(void);
void gfx_viewport(float x, float y, float w, float h);
void gfx_viewport_full(void);
void gfx_scissor(float x, float y, float w, float h);
/* For the textured draws: their layout, then back to the plain one. */
void gfx_textured(int on);

void gfx_rect(float x, float y, float w, float h, GXColor c);
void gfx_round_rect(float x, float y, float w, float h, float r, GXColor c);
void gfx_ellipse(float cx, float cy, float rx, float ry, GXColor c);
/* Fades from `c` at the center to clear at the rim. */
void gfx_soft_ellipse(float cx, float cy, float rx, float ry, GXColor c);
void gfx_star(float cx, float cy, float r, GXColor c);
/* Covers from radius r (fading in over one pixel, as gfx_ellipse fades out) to outer. */
void gfx_ring(float cx, float cy, float r, float outer, GXColor c);

/* An RGBA picture from tools/make_cursor.py, with the point it is held by. */
typedef struct {
	GXTexObj tex;
	void *pixels;
	int w, h, hot_x, hot_y;
} gfx_image;

int gfx_image_load(gfx_image *img, const u8 *data, u32 size);
/* Its hot point at (x, y), turned by angle radians about it. */
void gfx_image_draw(const gfx_image *img, float x, float y, float scale, float angle);

#endif
