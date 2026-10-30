// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef TOUR

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <fat.h>
#include <gccore.h>

#include "gfx.h"
#include "tour.h"

/* main.c's IN_* bits. */
enum { A = 1, B = 2, UP = 4, DOWN = 8, LEFT = 16, RIGHT = 32, HOME = 64 };
enum { POINT, HIDE, PRESS, SHOT, END };

typedef struct {
	float t;
	int what;
	float x, y;
	const char *name;
} step;

#ifdef TOUR_DETAIL
/* One close-up, left alone. */
static const step script[] = {
	{ 3.0f, POINT, 180, 126, NULL },
	{ 3.5f, PRESS, A, 0, NULL },
	{ 4.0f, HIDE, 0, 0, NULL },
	{ 18.0f, END, 0, 0, NULL },
};
#elif defined(TOUR_GRID)
/* The line-up only: the pointer wanders over it, nothing else changes. */
static const step script[] = {
	{ 3.0f, POINT, 180, 126, NULL },
	{ 5.0f, POINT, 400, 300, NULL },
	{ 7.0f, POINT, 520, 140, NULL },
	{ 9.0f, POINT, 90, 320, NULL },
	{ 11.0f, POINT, 320, 230, NULL },
	{ 13.0f, HIDE, 0, 0, NULL },
	{ 15.0f, END, 0, 0, NULL },
};
#else
/* For the 4:3 line-up: the second Mii's head, then the screens past it. */
static const step script[] = {
	{ 3.0f, SHOT, 0, 0, "grid" },
	{ 3.2f, POINT, 180, 126, NULL },
	{ 4.4f, SHOT, 0, 0, "hover" },
	{ 4.8f, PRESS, A, 0, NULL },
	{ 6.2f, SHOT, 0, 0, "detail" },
	{ 6.4f, HIDE, 0, 0, NULL },
	{ 6.5f, PRESS, A, 0, NULL },
	{ 7.2f, SHOT, 0, 0, "detail-smile" },
	{ 7.4f, PRESS, RIGHT, 0, NULL },
	{ 8.8f, SHOT, 0, 0, "detail-next" },
	{ 9.0f, PRESS, B, 0, NULL },
	{ 10.2f, PRESS, DOWN, 0, NULL },
	{ 10.6f, PRESS, DOWN, 0, NULL },
	{ 11.8f, SHOT, 0, 0, "scrolled" },
	{ 12.0f, PRESS, A, 0, NULL },
	{ 13.0f, PRESS, LEFT, 0, NULL },
	{ 14.0f, PRESS, LEFT, 0, NULL },
	{ 15.0f, PRESS, LEFT, 0, NULL },
	{ 16.0f, PRESS, LEFT, 0, NULL },
	{ 17.4f, SHOT, 0, 0, "revisit" },
	{ 17.8f, END, 0, 0, NULL },
};
#endif

static int next, pointing, ended;
static float px, py;
static u8 *efb;

void tour_init(void) {
	fatInitDefault();
	mkdir("sd:/tour", 0777);
	efb = memalign(32, 640 * 480 * 4);
}

int tour_active(void) {
	return 1;
}

void tour_input(int *pointer, float *x, float *y, u32 *down, float t) {
	*down = 0;
	while (next < (int) (sizeof(script) / sizeof(script[0])) && script[next].t <= t && script[next].what != SHOT) {
		const step *s = &script[next++];

		if (s->what == POINT) {
			pointing = 1;
			px = s->x;
			py = s->y;
		} else if (s->what == HIDE)
			pointing = 0;
		else if (s->what == PRESS)
			*down |= (u32) s->x;
		else if (s->what == END)
			ended = 1;
	}
	*pointer = pointing;
	*x = px;
	*y = py;
	if (ended)
		*down |= HOME;
}

/* Copies the EFB before the frame's display copy clears it, as linear RGBA. */
static void save(const char *name) {
	static u8 line[640 * 4];
	char path[64];
	FILE *f;
	int x, y;

	GX_SetTexCopySrc(0, 0, 640, 480);
	GX_SetTexCopyDst(640, 480, GX_TF_RGBA8, GX_FALSE);
	DCInvalidateRange(efb, 640 * 480 * 4);
	GX_CopyTex(efb, GX_FALSE);
	GX_PixModeSync();
	GX_DrawDone();
	snprintf(path, sizeof(path), "sd:/tour/%s.rgba", name);
	if (!(f = fopen(path, "wb")))
		return;
	fprintf(f, "640 480 0 0\n");
	for (y = 0; y < 480; ++y) {
		for (x = 0; x < 640; ++x) {
			const u8 *tile = efb + ((y / 4) * 160 + x / 4) * 64;
			int i = (y % 4) * 4 + x % 4;

			line[x * 4 + 0] = tile[i * 2 + 1];
			line[x * 4 + 1] = tile[32 + i * 2];
			line[x * 4 + 2] = tile[32 + i * 2 + 1];
			line[x * 4 + 3] = tile[i * 2];
		}
		fwrite(line, 1, sizeof(line), f);
	}
	fclose(f);
}

void tour_frame(float t) {
	if (next < (int) (sizeof(script) / sizeof(script[0])) && script[next].what == SHOT && script[next].t <= t)
		save(script[next++].name);
}

#endif
