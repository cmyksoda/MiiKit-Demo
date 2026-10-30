// SPDX-License-Identifier: GPL-3.0-or-later
#include <malloc.h>
#include <math.h>
#include <string.h>

#include "gfx.h"

#define FIFO_SIZE (256 * 1024)
#define MAX_RIM 160

float gfx_w = 640, gfx_h = 480;
int gfx_widescreen;

/* A GX FIFO in MEM2 stalls a real Wii at random (sdk/mii README), so it stays in MEM1's .bss. */
static u8 fifo[FIFO_SIZE] ATTRIBUTE_ALIGN(32);
static GXRModeObj *rmode;
static void *xfb[2];
static int fb;
static Mtx44 ortho;

void gfx_init(void) {
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	gfx_widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
	gfx_w = gfx_widescreen ? 640.0f * 4 / 3 : 640;
	VIDEO_Configure(rmode);
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_WHITE);
	VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_WHITE);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	GX_Init(fifo, FIFO_SIZE);
	GX_SetDispCopyYScale(GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight));
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
	GX_SetFieldMode(rmode->field_rendering, rmode->viHeight == 2 * rmode->xfbHeight ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	guOrtho(ortho, 0, gfx_h, 0, gfx_w, 0, 1);
	GX_InvVtxCache();
	GX_InvalidateTexAll();
}

void gfx_frame_begin(GXColor clear) {
	GX_SetCopyClear(clear, GX_MAX_Z24);
	gfx_viewport_full();
	gfx_2d();
}

void gfx_frame_end(void) {
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_CopyDisp(xfb[fb], GX_TRUE);
	GX_DrawDone();
	VIDEO_SetNextFramebuffer(xfb[fb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	fb ^= 1;
}

void gfx_scissor(float x, float y, float w, float h) {
	float sx = rmode->fbWidth / gfx_w, sy = rmode->efbHeight / gfx_h;
	float x0 = x * sx, y0 = y * sy, x1 = (x + w) * sx, y1 = (y + h) * sy;

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > rmode->fbWidth)
		x1 = rmode->fbWidth;
	if (y1 > rmode->efbHeight)
		y1 = rmode->efbHeight;
	if (x1 <= x0 || y1 <= y0)
		x1 = x0 + 1, y1 = y0 + 1;
	GX_SetScissor((u32) x0, (u32) y0, (u32) ceilf(x1 - x0), (u32) ceilf(y1 - y0));
}

void gfx_viewport(float x, float y, float w, float h) {
	float sx = rmode->fbWidth / gfx_w, sy = rmode->efbHeight / gfx_h;

	GX_SetViewport(x * sx, y * sy, w * sx, h * sy, 0, 1);
	gfx_scissor(x, y, w, h);
}

void gfx_viewport_full(void) {
	gfx_viewport(0, 0, gfx_w, gfx_h);
}

void gfx_2d(void) {
	Mtx identity;

	/* A Wii body's bones fill the matrix slots, so none of them keeps ours. */
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTevStages(1);
	gfx_textured(0);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
}

void gfx_textured(int on) {
	GX_SetVtxDesc(GX_VA_TEX0, on ? GX_DIRECT : GX_NONE);
	GX_SetNumTexGens(on);
	if (on)
		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOrder(GX_TEVSTAGE0, on ? GX_TEXCOORD0 : GX_TEXCOORDNULL, on ? GX_TEXMAP0 : GX_TEXMAP_NULL, GX_COLOR0A0);
	if (!on)
		GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void vertex(float x, float y, GXColor c) {
	GX_Position3f32(x, y, 0);
	GX_Color4u8(c.r, c.g, c.b, c.a);
}

/* A convex outline filled from its center, with a one-pixel fade outside it
 * so its edge doesn't stair-step on a TV. */
static void shape(const float *px, const float *py, const float *nx, const float *ny, int n, float cx, float cy,
				  GXColor c) {
	GXColor clear = { c.r, c.g, c.b, 0 };
	int i;

	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT2, n + 2);
	vertex(cx, cy, c);
	for (i = 0; i <= n; ++i)
		vertex(px[i % n], py[i % n], c);
	GX_End();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT2, 2 * n + 2);
	for (i = 0; i <= n; ++i) {
		vertex(px[i % n], py[i % n], c);
		vertex(px[i % n] + nx[i % n], py[i % n] + ny[i % n], clear);
	}
	GX_End();
}

static int segments(float r) {
	int n = (int) (r * 0.9f);

	return n < 16 ? 16 : n > MAX_RIM ? MAX_RIM : n;
}

void gfx_rect(float x, float y, float w, float h, GXColor c) {
	GX_Begin(GX_QUADS, GX_VTXFMT2, 4);
	vertex(x, y, c);
	vertex(x + w, y, c);
	vertex(x + w, y + h, c);
	vertex(x, y + h, c);
	GX_End();
}

void gfx_round_rect(float x, float y, float w, float h, float r, GXColor c) {
	static float px[MAX_RIM], py[MAX_RIM], nx[MAX_RIM], ny[MAX_RIM];
	int per = segments(r) / 4, corner, i, n = 0;

	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	if (per < 4)
		per = 4;
	for (corner = 0; corner < 4; ++corner) {
		float ccx = corner == 0 || corner == 3 ? x + w - r : x + r;
		float ccy = corner < 2 ? y + h - r : y + r;

		for (i = 0; i < per; ++i) {
			float a = (corner + (float) i / (per - 1)) * (float) M_PI / 2;

			nx[n] = cosf(a);
			ny[n] = sinf(a);
			px[n] = ccx + r * nx[n];
			py[n] = ccy + r * ny[n];
			++n;
		}
	}
	shape(px, py, nx, ny, n, x + w / 2, y + h / 2, c);
}

void gfx_ellipse(float cx, float cy, float rx, float ry, GXColor c) {
	static float px[MAX_RIM], py[MAX_RIM], nx[MAX_RIM], ny[MAX_RIM];
	int n = segments(rx > ry ? rx : ry), i;

	for (i = 0; i < n; ++i) {
		float a = 2 * (float) M_PI * i / n;

		nx[i] = cosf(a);
		ny[i] = sinf(a);
		px[i] = cx + rx * nx[i];
		py[i] = cy + ry * ny[i];
	}
	shape(px, py, nx, ny, n, cx, cy, c);
}

void gfx_soft_ellipse(float cx, float cy, float rx, float ry, GXColor c) {
	GXColor clear = { c.r, c.g, c.b, 0 };
	int n = segments(rx > ry ? rx : ry), i;

	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT2, n + 2);
	vertex(cx, cy, c);
	for (i = 0; i <= n; ++i) {
		float a = 2 * (float) M_PI * i / n;

		vertex(cx + rx * cosf(a), cy + ry * sinf(a), clear);
	}
	GX_End();
}

void gfx_ring(float cx, float cy, float r, float outer, GXColor c) {
	GXColor clear = { c.r, c.g, c.b, 0 };
	float radii[3] = { r, r + 1, outer };
	int n = segments(r), band, i;

	for (band = 0; band < 2; ++band) {
		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT2, 2 * n + 2);
		for (i = 0; i <= n; ++i) {
			float a = 2 * (float) M_PI * i / n, x = cosf(a), y = sinf(a);

			vertex(cx + radii[band] * x, cy + radii[band] * y, band ? c : clear);
			vertex(cx + radii[band + 1] * x, cy + radii[band + 1] * y, c);
		}
		GX_End();
	}
}

void gfx_star(float cx, float cy, float r, GXColor c) {
	int i;

	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT2, 12);
	vertex(cx, cy, c);
	for (i = 0; i <= 10; ++i) {
		float a = (float) M_PI * (i / 5.0f - 0.5f), k = i & 1 ? 0.45f : 1;

		vertex(cx + r * k * cosf(a), cy + r * k * sinf(a), c);
	}
	GX_End();
}

int gfx_image_load(gfx_image *img, const u8 *data, u32 size) {
	u32 bytes;

	memset(img, 0, sizeof(*img));
	if (size < 32 || memcmp(data, "CURS", 4))
		return -1;
	img->w = data[4] << 8 | data[5];
	img->h = data[6] << 8 | data[7];
	img->hot_x = data[8] << 8 | data[9];
	img->hot_y = data[10] << 8 | data[11];
	bytes = img->w * img->h * 4;
	if (32 + bytes > size || !(img->pixels = memalign(32, bytes)))
		return -1;
	memcpy(img->pixels, data + 32, bytes);
	DCFlushRange(img->pixels, bytes);
	GX_InitTexObj(&img->tex, img->pixels, img->w, img->h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&img->tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
	return 0;
}

void gfx_image_draw(const gfx_image *img, float x, float y, float scale, float angle) {
	static const float corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	float c = cosf(angle) * scale, s = sinf(angle) * scale;
	int i;

	if (!img->pixels)
		return;
	GX_LoadTexObj((GXTexObj *) &img->tex, GX_TEXMAP0);
	gfx_textured(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GX_Begin(GX_QUADS, GX_VTXFMT2, 4);
	for (i = 0; i < 4; ++i) {
		float px = corner[i][0] * img->w - img->hot_x, py = corner[i][1] * img->h - img->hot_y;

		GX_Position3f32(x + px * c - py * s, y + px * s + py * c, 0);
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(corner[i][0], corner[i][1]);
	}
	GX_End();
	gfx_textured(0);
}
