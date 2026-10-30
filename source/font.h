// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FONT_H
#define FONT_H

#include <gccore.h>

#define FONT_PAGES 16

/* A size of FOT-Rodin, rasterized by tools/make_font.py. */
typedef struct {
	int size, line, ascent;
	u32 nglyphs;
	const u8 *glyphs;
	int npages, page_size;
	void *pixels[FONT_PAGES];
	GXTexObj tex[FONT_PAGES];
} font;

int font_load(font *f, const u8 *data, u32 size);

/* UTF-8 text with its baseline at y; returns its width. */
float font_draw(const font *f, float x, float y, const char *text, GXColor c);
float font_width(const font *f, const char *text);

#endif
