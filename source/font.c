// SPDX-License-Identifier: GPL-3.0-or-later
#include <malloc.h>
#include <math.h>
#include <string.h>

#include "font.h"
#include "gfx.h"

#define HEADER 20
#define GLYPH 20

static u32 be16(const u8 *p) {
	return p[0] << 8 | p[1];
}

static u32 be32(const u8 *p) {
	return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static s16 bes16(const u8 *p) {
	return (s16) be16(p);
}

int font_load(font *f, const u8 *data, u32 size) {
	u32 at, page_bytes;
	int i;

	memset(f, 0, sizeof(*f));
	if (size < HEADER || memcmp(data, "RFNT", 4))
		return -1;
	f->size = be16(data + 4);
	f->line = be16(data + 6);
	f->ascent = bes16(data + 8);
	f->npages = be16(data + 12);
	f->page_size = be16(data + 14);
	f->nglyphs = be32(data + 16);
	f->glyphs = data + HEADER;
	at = (HEADER + f->nglyphs * GLYPH + 31) & ~31;
	page_bytes = f->page_size * f->page_size / 2;
	if (f->npages > FONT_PAGES || at + f->npages * page_bytes > size)
		return -1;
	for (i = 0; i < f->npages; ++i) {
		/* The texture needs 32-byte alignment, which the linked-in data doesn't promise. */
		if (!(f->pixels[i] = memalign(32, page_bytes)))
			return -1;
		memcpy(f->pixels[i], data + at + i * page_bytes, page_bytes);
		DCFlushRange(f->pixels[i], page_bytes);
		GX_InitTexObj(&f->tex[i], f->pixels[i], f->page_size, f->page_size, GX_TF_I4, GX_CLAMP, GX_CLAMP,
					  GX_FALSE);
		GX_InitTexObjLOD(&f->tex[i], GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
	}
	return 0;
}

static u32 next_char(const char **s) {
	const u8 *p = (const u8 *) *s;
	u32 c = *p++;
	int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;

	if (more)
		c &= 0x3f >> more;
	while (more-- && (*p & 0xc0) == 0x80)
		c = c << 6 | (*p++ & 0x3f);
	*s = (const char *) p;
	return c;
}

static const u8 *find(const font *f, u32 c) {
	u32 lo = 0, hi = f->nglyphs;

	while (lo < hi) {
		u32 mid = (lo + hi) / 2, at = be32(f->glyphs + mid * GLYPH);

		if (at == c)
			return f->glyphs + mid * GLYPH;
		if (at < c)
			lo = mid + 1;
		else
			hi = mid;
	}
	return c == '?' ? NULL : find(f, '?');
}

float font_width(const font *f, const char *text) {
	float w = 0;

	while (*text) {
		const u8 *g = find(f, next_char(&text));

		if (g)
			w += be16(g + 12) / 64.0f;
	}
	return w;
}

float font_draw(const font *f, float x, float y, const char *text, GXColor c) {
	float pen = x, scale = 1.0f / f->page_size;
	int page = -1;

	gfx_textured(1);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	y = roundf(y);
	while (*text) {
		const u8 *g = find(f, next_char(&text));
		float gx, gy, w, h, u, v;

		if (!g)
			continue;
		w = be16(g + 8);
		h = be16(g + 10);
		if (w && h) {
			if ((int) be16(g + 14) != page) {
				page = be16(g + 14);
				GX_LoadTexObj((GXTexObj *) &f->tex[page], GX_TEXMAP0);
			}
			gx = roundf(pen) + bes16(g + 4);
			gy = y + bes16(g + 6);
			u = be16(g + 16) * scale;
			v = be16(g + 18) * scale;
			GX_Begin(GX_QUADS, GX_VTXFMT2, 4);
			GX_Position3f32(gx, gy, 0);
			GX_Color4u8(c.r, c.g, c.b, c.a);
			GX_TexCoord2f32(u, v);
			GX_Position3f32(gx + w, gy, 0);
			GX_Color4u8(c.r, c.g, c.b, c.a);
			GX_TexCoord2f32(u + w * scale, v);
			GX_Position3f32(gx + w, gy + h, 0);
			GX_Color4u8(c.r, c.g, c.b, c.a);
			GX_TexCoord2f32(u + w * scale, v + h * scale);
			GX_Position3f32(gx, gy + h, 0);
			GX_Color4u8(c.r, c.g, c.b, c.a);
			GX_TexCoord2f32(u, v + h * scale);
			GX_End();
		}
		pen += be16(g + 12) / 64.0f;
	}
	gfx_textured(0);
	return pen - x;
}
