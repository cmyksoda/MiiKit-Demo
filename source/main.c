// SPDX-License-Identifier: GPL-3.0-or-later
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "font.h"
#include "gfx.h"
#include "miikit/mii.h"
#include "miikit/miidraw.h"
#include "music.h"
#include "music_mp3.h"
#include "rodin20_bin.h"
#include "rodin32_bin.h"
#include "cursor_bin.h"
#include "tour.h"

#define MUSIC_VOLUME 150

#define GRID_TOP 78
#define ROW_H 178
#define ROWS_SHOWN 2
#define FEET_GAP 30
#define MII_SPAN (ROW_H - FEET_GAP - 10)
#define FOVY 12.0f
#define FADE_OUT_S 0.16f
#define FADE_IN_S 0.26f
#define ANIM_FADE_S 0.35f
#define BUST_H 128.0f

static const GXColor BG = { 250, 250, 251, 255 };
static const GXColor INK = { 38, 41, 46, 255 };
static const GXColor INK_SOFT = { 96, 101, 110, 255 };
static const GXColor LINE = { 222, 225, 230, 255 };
static const GXColor SHADOW = { 20, 24, 30, 30 };
static const GXColor SHADOW_FOCUS = { 20, 24, 30, 60 };

enum { IN_A = 1, IN_B = 2, IN_UP = 4, IN_DOWN = 8, IN_LEFT = 16, IN_RIGHT = 32, IN_HOME = 64 };
enum { SCREEN_LOADING, SCREEN_GRID, SCREEN_DETAIL, SCREEN_NO_MIIS, SCREEN_NO_DB, SCREEN_NO_FACES, SCREEN_NO_MEMORY };

typedef struct {
	int pointer;
	float x, y, roll;
	u32 down;
} input;

typedef struct {
	mii m;
	mii_model model;
	char name[MII_NAME_UTF8_MAX], creator[MII_NAME_UTF8_MAX];
	float top, phase;
	float look_yaw, look_pitch;
	float wave_at, blink_at, blink_until;
	int last_anim, from_anim;
	float last_frame, from_frame, from_t;
} entry;

static entry *miis;
static int nmiis;
static mii_db db;
static mii_res res;
static mii_body bodies[2];
static int anim_wait[2], anim_greet[2], greet_frames[2];
static mii_cache *cache;
static font font20, font32;
static gfx_image cursor;
static float top_max = 1;

static volatile int loaded, load_done, load_total, load_result;
static lwp_t load_thread = LWP_THREAD_NULL;
static u8 load_stack[64 * 1024] ATTRIBUTE_ALIGN(32);

static int screen = SCREEN_LOADING, next_screen = -1;
static float fade = 1, now;
static int cols;
static float scroll, scroll_to;
static int focus = -1, focus_by_pad, sel, next_sel = -1;
static mii_model detail;
static int detail_ok, expression;
static volatile int quit, power_off, pair;

/* ---- Loading ---------------------------------------------------------------- */

static void set_body(mii_model *model, const mii *m) {
	int k = m->sex == MII_FEMALE;

	if (bodies[k].data)
		mii_model_set_body(model, &bodies[k]);
}

static void *load(void *arg) {
	mii_model_opts opts = { 64, MII_EXPR_BIT(MII_EXPR_NORMAL) | MII_EXPR_BIT(MII_EXPR_SMILE) |
								MII_EXPR_BIT(MII_EXPR_BLINK), NULL, 0, 0 };
	mii m;
	int slot, k, r;

	(void) arg;
	if ((r = mii_db_load(&db)) < 0) {
		load_result = r == MII_E_NO_FILE ? SCREEN_NO_MIIS : SCREEN_NO_DB;
		return NULL;
	}
	for (slot = 0; slot < MII_DB_SLOTS; ++slot)
		load_total += mii_db_get(&db, slot, &m) == MII_OK;
	if (!load_total) {
		load_result = SCREEN_NO_MIIS;
		return NULL;
	}
	if (mii_res_load_nand(&res) < 0 && mii_res_load(&res, NULL) < 0) {
		load_result = SCREEN_NO_FACES;
		return NULL;
	}
	if (mii_bodies_load_nand(&bodies[0], &bodies[1]) < 0) {
		mii_body_load(&bodies[0], MII_BODY_MALE_PATH);
		mii_body_load(&bodies[1], MII_BODY_FEMALE_PATH);
	}
	for (k = 0; k < 2; ++k)
		if (bodies[k].data) {
			anim_wait[k] = 1 + mii_body_anim(&bodies[k], "bodyWait");
			anim_greet[k] = 1 + mii_body_anim(&bodies[k], "bodyGreeting");
			greet_frames[k] = anim_greet[k] ? bodies[k].anim[anim_greet[k] - 1].frames : 0;
		}
	if (!(miis = calloc(load_total, sizeof(*miis))) || !(cache = mii_cache_new())) {
		load_result = SCREEN_NO_MEMORY;
		return NULL;
	}
	opts.cache = cache;
	for (slot = 0; slot < MII_DB_SLOTS; ++slot) {
		entry *e = &miis[nmiis];
		float half;

		if (mii_db_get(&db, slot, &e->m) < 0)
			continue;
		if (mii_model_build(&res, &e->m, &opts, &e->model) == MII_OK) {
			set_body(&e->model, &e->m);
			mii_get_name(e->m.name, e->name, sizeof(e->name));
			mii_get_name(e->m.creator, e->creator, sizeof(e->creator));
			mii_model_bounds(&e->model, 0, &e->top, &half);
			if (e->top > top_max)
				top_max = e->top;
			e->phase = (nmiis % 5) * 0.83f;
			e->blink_at = 1 + (nmiis % 7) * 0.61f;
			e->wave_at = -10;
			++nmiis;
		}
		++load_done;
	}
	load_result = nmiis ? SCREEN_GRID : SCREEN_NO_MEMORY;
	return NULL;
}

/* ---- Words ------------------------------------------------------------------ */

static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July",
										"August", "September", "October", "November", "December" };

static int leap(int y) {
	return (y % 4 == 0 && y % 100) || y % 400 == 0;
}

/* The ID's time counts from 2006-01-01, the Wii's own clock on the day it was made. */
static void created(const mii *m, char *out, size_t n) {
	static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	uint32_t days = mii_id_time(m->id) / 86400;
	int y = 2006, mo = 0;

	if (!mii_id_time(m->id)) {
		snprintf(out, n, "Unknown");
		return;
	}
	while (days >= (uint32_t) (365 + leap(y)))
		days -= 365 + leap(y++);
	while (days >= (uint32_t) (mdays[mo] + (mo == 1 && leap(y))))
		days -= mdays[mo] + (mo == 1 && leap(y)), ++mo;
	snprintf(out, n, "%.3s %u, %d", months[mo], (unsigned) days + 1, y);
}

static const char *made_on(const mii *m) {
	uint32_t home = (uint32_t) m->id[4] << 24 | m->id[5] << 16 | m->id[6] << 8 | m->id[7];

	if (mii_id_flags(m->id) & MII_ID_DS)
		return "A Nintendo DS";
	if (!db.home)
		return "Unknown";
	return home == db.home ? "This Wii" : "Another Wii";
}

static const char *const expression_names[MII_EXPR_COUNT] = { "Normal", "Smile", "Anger", "Sorrow",
															   "Surprise", "Blink", "Open mouth" };

static GXColor accent(const entry *e) {
	u32 c = e->model.shirt;

	return RGBA(c >> 24, c >> 16 & 0xff, c >> 8 & 0xff, 255);
}

static GXColor mix(GXColor a, GXColor b, float k) {
	return RGBA(a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, 255);
}

/* ---- Posing ----------------------------------------------------------------- */

static float approach(float at, float want, float rate, float dt) {
	return at + (want - at) * (1 - expf(-rate * dt));
}

static void pose_body(entry *e, float t, int waving, mii_pose *p) {
	int k = e->m.sex == MII_FEMALE, anim;
	float frame;

	if (!e->model.body || !anim_wait[k]) {
		float wave = t - e->wave_at;

		p->arm_raise[0] = waving ? 2.35f + 0.28f * sinf(wave * 13.8f) : 0.1f;
		p->arm_raise[1] = 0.1f;
		p->breathe = 0.5f + 0.5f * sinf((t + e->phase) * 1.85f);
		return;
	}
	anim = waving && anim_greet[k] ? anim_greet[k] : anim_wait[k];
	frame = (anim == anim_wait[k] ? t + e->phase : t - e->wave_at) * 60;
	if (e->last_anim && anim != e->last_anim) {
		e->from_anim = e->last_anim;
		e->from_frame = e->last_frame;
		e->from_t = t;
	}
	e->last_anim = anim;
	e->last_frame = frame;
	p->anim = anim;
	p->frame = frame;
	p->from_weight = e->from_anim ? mii_fade_weight(t - e->from_t, ANIM_FADE_S) : 0;
	if (p->from_weight > 0) {
		p->from_anim = e->from_anim;
		/* Wait loops on as it fades; a finished wave holds its last pose. */
		p->from_frame = e->from_anim == anim_greet[k] ? e->from_frame : e->from_frame + (t - e->from_t) * 60;
	}
}

static void pose_look(entry *e, const input *in, float hx, float hy, float t, float dt, mii_pose *p) {
	float want_yaw, want_pitch;

	if (in->pointer) {
		want_yaw = fmaxf(-0.75f, fminf(0.75f, (in->x - hx) / 320));
		want_pitch = fmaxf(-0.35f, fminf(0.35f, -(in->y - hy) / 420));
	} else {
		want_yaw = 0.16f * sinf(t * 0.37f + e->phase * 2);
		want_pitch = 0;
	}
	e->look_yaw = approach(e->look_yaw, want_yaw, 6, dt);
	e->look_pitch = approach(e->look_pitch, want_pitch, 6, dt);
	p->yaw = e->look_yaw * 0.25f;
	p->head_yaw = e->look_yaw * 0.75f;
	p->head_pitch = e->look_pitch;
}

static float wave_seconds(const entry *e) {
	int frames = greet_frames[e->m.sex == MII_FEMALE];

	return (frames ? frames : 140) / 60.0f;
}

static int blinking(entry *e, float t) {
	if (t >= e->blink_at) {
		e->blink_until = t + 0.13f;
		e->blink_at = t + 2.5f + (float) (rand() % 1000) / 300;
	}
	return t < e->blink_until;
}

/* ---- Pieces ----------------------------------------------------------------- */

static float hint(float x, float y, int icon, const char *label) {
	const char *letter = icon == 'A' ? "A" : "B";

	if (icon == '+') {
		gfx_round_rect(x, y - 10, 21, 7, 2, INK);
		gfx_round_rect(x + 7, y - 17, 7, 21, 2, INK);
	} else {
		if (icon == 'A')
			gfx_ellipse(x + 11, y - 7, 11, 11, INK);
		else
			gfx_round_rect(x, y - 18, 22, 22, 5, INK);
		font_draw(&font20, x + 11 - font_width(&font20, letter) / 2, y, letter, BG);
	}
	return 30 + font_draw(&font20, x + 30, y, label, INK_SOFT) + 26;
}

/* The hand tilts with the Wii Remote, as the Wii Menu's does. */
static void draw_pointer(const input *in, float dt) {
	static float roll;

	if (!in->pointer)
		return;
	roll = approach(roll, in->roll, 14, dt);
	gfx_image_draw(&cursor, in->x, in->y, 1, roll);
}

static float margin(void) {
	return gfx_widescreen ? 48 : 40;
}

static void message(const char *title, const char *line1, const char *line2) {
	float x = margin();

	font_draw(&font32, x, 200, title, INK);
	font_draw(&font20, x, 240, line1, INK_SOFT);
	if (line2)
		font_draw(&font20, x, 268, line2, INK_SOFT);
	font_draw(&font20, x, 462, "Press HOME to go back.", INK_SOFT);
}

/* ---- The line-up --------------------------------------------------------------- */

static int total_rows(void) {
	return (nmiis + cols - 1) / cols;
}

static void keep_visible(int i) {
	int row = i / cols;

	if (row < scroll_to)
		scroll_to = row;
	else if (row >= scroll_to + ROWS_SHOWN)
		scroll_to = row - ROWS_SHOWN + 1;
}

static void grid_input(const input *in) {
	float x0 = margin(), cw = (gfx_w - 2 * x0) / cols;
	int max_scroll = total_rows() > ROWS_SHOWN ? total_rows() - ROWS_SHOWN : 0;

	if (in->pointer && !focus_by_pad) {
		int c = (int) floorf((in->x - x0) / cw), r = (int) floorf((in->y - GRID_TOP) / ROW_H + scroll);

		focus = -1;
		if (in->x >= x0 && c < cols && in->y >= GRID_TOP && in->y < GRID_TOP + ROWS_SHOWN * ROW_H &&
				r * cols + c < nmiis)
			focus = r * cols + c;
	}
	if (in->down & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) {
		/* While pointing, up and down scroll: the pointer keeps choosing the Mii. */
		if ((focus < 0 || in->pointer) && in->down & (IN_UP | IN_DOWN))
			scroll_to += in->down & IN_UP ? -1 : 1;
		else if (in->pointer)
			;
		else {
			if (focus < 0)
				focus = (int) scroll_to * cols;
			else if (in->down & IN_LEFT && focus > 0)
				--focus;
			else if (in->down & IN_RIGHT && focus < nmiis - 1)
				++focus;
			else if (in->down & IN_UP && focus >= cols)
				focus -= cols;
			else if (in->down & IN_DOWN && focus + cols < nmiis)
				focus += cols;
			focus_by_pad = 1;
			keep_visible(focus);
		}
	}
	if (scroll_to < 0)
		scroll_to = 0;
	if (scroll_to > max_scroll)
		scroll_to = max_scroll;
	if (in->down & IN_A && focus >= 0 && next_screen < 0) {
		sel = focus;
		next_screen = SCREEN_DETAIL;
	}
}

static void grid_overlay(float x0, float cw) {
	if (focus >= 0) {
		entry *e = &miis[focus];
		float cx = x0 + (focus % cols + 0.5f) * cw;
		float feet = GRID_TOP + (focus / cols - scroll + 1) * ROW_H - FEET_GAP;

		gfx_scissor(0, GRID_TOP, gfx_w, ROWS_SHOWN * ROW_H);
		font_draw(&font20, cx - font_width(&font20, e->name) / 2, feet + 24, e->name, INK);
		gfx_scissor(0, 0, gfx_w, gfx_h);
	}
	if (total_rows() > ROWS_SHOWN) {
		float track = ROWS_SHOWN * ROW_H - 16, thumb = track * ROWS_SHOWN / total_rows();
		float at = (track - thumb) * scroll / (total_rows() - ROWS_SHOWN);

		gfx_round_rect(gfx_w - x0 / 2 - 2, GRID_TOP + 8, 4, track, 2, LINE);
		gfx_round_rect(gfx_w - x0 / 2 - 2, GRID_TOP + 8 + at, 4, thumb, 2, INK_SOFT);
	}
	{
		float x = x0;

		x += hint(x, 462, 'A', "Look closer");
		if (total_rows() > ROWS_SHOWN)
			hint(x, 462, '+', "Scroll");
	}
}

static void draw_grid(const input *in, float dt) {
	float x0 = margin(), gw = gfx_w - 2 * x0, cw = gw / cols, k = top_max / MII_SPAN;
	float d = ROW_H * k / 2 / tanf(FOVY * (float) M_PI / 360);
	int first = (int) floorf(scroll), last = first + ROWS_SHOWN, r, c, i;
	char count[16];

	font_draw(&font32, x0, 56, "Your Miis", INK);
	snprintf(count, sizeof(count), "%d", nmiis);
	font_draw(&font20, x0 + font_width(&font32, "Your Miis") + 14, 56, count, INK_SOFT);

	gfx_scissor(0, GRID_TOP, gfx_w, ROWS_SHOWN * ROW_H);
	for (r = first; r <= last; ++r)
		for (c = 0; c < cols && (i = r * cols + c) < nmiis; ++c)
			gfx_soft_ellipse(x0 + (c + 0.5f) * cw, GRID_TOP + (r - scroll + 1) * ROW_H - FEET_GAP, 32, 7,
							 i == focus ? SHADOW_FOCUS : SHADOW);

	for (r = first; r <= last && r * cols < nmiis; ++r) {
		float y0 = GRID_TOP + (r - scroll) * ROW_H, feet = y0 + ROW_H - FEET_GAP;
		float yc = (feet - (y0 + ROW_H / 2)) * k;
		guVector eye = { 0, yc, d }, up = { 0, 1, 0 }, at = { 0, yc, 0 };
		Mtx44 proj;
		Mtx view, place;

		gfx_viewport(x0, y0, gw, ROW_H);
		gfx_scissor(x0, fmaxf(y0, GRID_TOP), gw,
					fminf(y0 + ROW_H, GRID_TOP + ROWS_SHOWN * ROW_H) - fmaxf(y0, GRID_TOP));
		guPerspective(proj, FOVY, gw / ROW_H, d - top_max, d + top_max);
		GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
		guLookAt(view, &eye, &up, &at);
		for (c = 0; c < cols && (i = r * cols + c) < nmiis; ++c) {
			entry *e = &miis[i];
			mii_pose pose;
			float cx = x0 + (c + 0.5f) * cw;
			int focused = i == focus;

			memset(&pose, 0, sizeof(pose));
			if (focused && now - e->wave_at > 3.5f)
				e->wave_at = now;
			pose_look(e, in, cx, feet - e->top / k * 0.82f, now, dt, &pose);
			pose_body(e, now, now - e->wave_at < wave_seconds(e), &pose);
			pose.expression = focused ? MII_EXPR_SMILE : blinking(e, now) ? MII_EXPR_BLINK : MII_EXPR_NORMAL;
			guMtxTrans(place, (cx - (x0 + gw / 2)) * k, 0, 0);
			mii_draw(&e->model, &pose, view, place);
		}
	}

	gfx_viewport_full();
	gfx_2d();
	grid_overlay(x0, cw);
}

/* ---- One Mii -------------------------------------------------------------------- */

static void enter_detail(void) {
	mii_model_opts opts = { 256, MII_EXPRS_ALL, NULL, 0, 1 };

	if (detail_ok)
		mii_model_free(&detail);
	detail_ok = mii_model_build(&res, &miis[sel].m, &opts, &detail) == MII_OK;
	if (detail_ok)
		set_body(&detail, &miis[sel].m);
	expression = MII_EXPR_NORMAL;
}

static void detail_input(const input *in) {
	if (next_screen >= 0)
		return;
	if (in->down & IN_A)
		expression = (expression + 1) % MII_EXPR_COUNT;
	if (in->down & IN_B) {
		focus = sel;
		focus_by_pad = !in->pointer;
		keep_visible(sel);
		next_screen = SCREEN_GRID;
	}
	if (in->down & (IN_LEFT | IN_RIGHT) && nmiis > 1) {
		next_sel = (sel + (in->down & IN_LEFT ? nmiis - 1 : 1)) % nmiis;
		next_screen = SCREEN_DETAIL;
	}
}

static void slider(float x, float y, int value, GXColor c) {
	float at = x + 124.0f * value / 128;

	gfx_round_rect(x, y - 9, 124, 6, 3, LINE);
	gfx_ellipse(at, y - 6, 9, 9, INK_SOFT);
	gfx_ellipse(at, y - 6, 7, 7, c);
}

static void detail_mii(entry *e, const input *in, float dt, float hx, float hy, float hr) {
	const mii_model *model = detail_ok ? &detail : &e->model;
	mii_pose pose;
	Mtx44 proj;
	Mtx view;

	memset(&pose, 0, sizeof(pose));
	pose_look(e, in, hx, hy - hr * 0.2f, now, dt, &pose);
	pose_body(e, now, 0, &pose);
	pose.yaw = 0;
	pose.expression = expression;
	/* Head and shoulders, the Wii body's breathing kept. */
	{
		float top, half, yc, d = BUST_H / 2 / tanf(FOVY * (float) M_PI / 360);
		guVector eye, up = { 0, 1, 0 }, at;

		mii_model_bounds(model, 0, &top, &half);
		yc = top - BUST_H * 0.42f;
		eye = (guVector) { 0, yc, d };
		at = (guVector) { 0, yc, 0 };
		gfx_viewport(hx - hr, hy - hr, hr * 2, hr * 2);
		gfx_scissor(hx - hr, hy - hr, hr * 2, hr * 2);
		guPerspective(proj, FOVY, 1, d - top, d + top);
		GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
		guLookAt(view, &eye, &up, &at);
		mii_draw(model, &pose, view, NULL);
	}
	gfx_viewport_full();
	gfx_2d();
	gfx_ring(hx, hy, hr, hr * 1.42f + 2, BG);
	gfx_scissor(0, 0, gfx_w, gfx_h);
}

static void draw_detail(const input *in, float dt) {
	entry *e = &miis[sel];
	GXColor ac = accent(e);
	float hx = gfx_widescreen ? gfx_w * 0.25f : 156, hy = 232, hr = gfx_widescreen ? 146 : 128;
	float x0 = (gfx_widescreen ? gfx_w * 0.5f : 316) + fade * 18, vx = x0 + 146, y;
	const font *name_font = font_width(&font32, e->name) <= gfx_w - x0 - margin() ? &font32 : &font20;
	char text[48];

	gfx_ellipse(hx, hy, hr, hr, mix(BG, ac, 0.16f));
	detail_mii(e, in, dt, hx, hy, hr);
	gfx_viewport_full();
	gfx_2d();
	font_draw(&font20, hx - font_width(&font20, expression_names[expression]) / 2, hy + hr + 36,
			  expression_names[expression], INK_SOFT);

	y = name_font == &font32 ? 104 : 98;
	{
		float w = font_draw(name_font, x0, y, e->name, INK);

		if (e->m.favorite)
			gfx_star(x0 + w + 18, y - 11, 11, ac);
	}
	if (e->creator[0]) {
		snprintf(text, sizeof(text), "Made by %s", e->creator);
		font_draw(&font20, x0, 136, text, INK_SOFT);
	}
	y = 186;
	font_draw(&font20, x0, y, "Birthday", INK_SOFT);
	if (e->m.birth_month)
		snprintf(text, sizeof(text), "%s %d", months[e->m.birth_month - 1], e->m.birth_day);
	else
		snprintf(text, sizeof(text), "Not set");
	font_draw(&font20, vx, y, text, INK);
	y += 30;
	font_draw(&font20, x0, y, "Gender", INK_SOFT);
	font_draw(&font20, vx, y, e->m.sex == MII_FEMALE ? "Female" : "Male", INK);
	y += 30;
	font_draw(&font20, x0, y, "Favorite color", INK_SOFT);
	snprintf(text, sizeof(text), "%s", mii_color_name(e->m.fav_color));
	text[0] = text[0] - 'a' + 'A';
	{
		float w = font_draw(&font20, vx, y, text, INK);

		gfx_ellipse(vx + w + 17, y - 7, 10, 10, INK_SOFT);
		gfx_ellipse(vx + w + 17, y - 7, 8, 8, ac);
	}
	y += 30;
	font_draw(&font20, x0, y, "Height", INK_SOFT);
	slider(vx, y, e->m.height, ac);
	y += 30;
	font_draw(&font20, x0, y, "Weight", INK_SOFT);
	slider(vx, y, e->m.build, ac);
	y += 30;
	font_draw(&font20, x0, y, "Created", INK_SOFT);
	created(&e->m, text, sizeof(text));
	font_draw(&font20, vx, y, text, INK);
	y += 30;
	font_draw(&font20, x0, y, "Made on", INK_SOFT);
	font_draw(&font20, vx, y, made_on(&e->m), INK);
	y += 30;
	font_draw(&font20, x0, y, "Mingle", INK_SOFT);
	font_draw(&font20, vx, y, e->m.local_only ? "Off" : "On", INK);

	{
		float x = margin();

		x += hint(x, 462, 'A', "Change expression");
		x += hint(x, 462, 'B', "Back");
		if (nmiis > 1)
			hint(x, 462, '+', "Previous / Next");
	}
}

/* ---- Running ---------------------------------------------------------------------- */

static void on_power(void) {
	power_off = quit = 1;
}

static void on_wpad_power(s32 chan) {
	(void) chan;
	power_off = quit = 1;
}

/* The console's SYNC button: pair a new Wii Remote, as the Wii Menu does. */
static void on_sync(u32 held) {
	(void) held;
	pair = 1;
}

static void on_reset(u32 irq, void *ctx) {
	(void) irq;
	(void) ctx;
	quit = 1;
}

static void read_input(input *in) {
	static const struct {
		u32 wpad, pad, in;
	} map[] = {
		{ WPAD_BUTTON_A, PAD_BUTTON_A, IN_A },
		{ WPAD_BUTTON_B, PAD_BUTTON_B, IN_B },
		{ WPAD_BUTTON_UP, PAD_BUTTON_UP, IN_UP },
		{ WPAD_BUTTON_DOWN, PAD_BUTTON_DOWN, IN_DOWN },
		{ WPAD_BUTTON_LEFT, PAD_BUTTON_LEFT, IN_LEFT },
		{ WPAD_BUTTON_RIGHT, PAD_BUTTON_RIGHT, IN_RIGHT },
		{ WPAD_BUTTON_HOME, PAD_BUTTON_START, IN_HOME },
	};
	static float last_x, last_y;
	WPADData *w;
	u32 wd = 0, pd;
	int chan, i;

	WPAD_ScanPads();
	PAD_ScanPads();
	for (chan = 0; chan < 4; ++chan)
		wd |= WPAD_ButtonsDown(chan);
	pd = PAD_ButtonsDown(0);
	in->down = 0;
	for (i = 0; i < (int) (sizeof(map) / sizeof(map[0])); ++i)
		if (wd & map[i].wpad || pd & map[i].pad)
			in->down |= map[i].in;
	w = WPAD_Data(0);
	in->pointer = w && w->ir.valid;
	if (in->pointer) {
		in->x = w->ir.x;
		in->y = w->ir.y;
		in->roll = w->orient.roll * (float) M_PI / 180;
		/* A pointer that moves takes over from the +Control Pad. */
		if (fabsf(in->x - last_x) + fabsf(in->y - last_y) > 6) {
			focus_by_pad = 0;
			last_x = in->x;
			last_y = in->y;
		}
	}
}

static void switch_screen(void) {
	screen = next_screen;
	next_screen = -1;
	if (next_sel >= 0)
		sel = next_sel;
	next_sel = -1;
	if (screen == SCREEN_DETAIL)
		enter_detail();
}

int main(int argc, char **argv) {
	input in = { 0 };
	u64 last = gettime();
	int mute = 0, i;

	for (i = 1; i < argc; ++i)
		mute |= !strcmp(argv[i], "mute");
	gfx_init();
	WPAD_Init();
	PAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, gfx_w, gfx_h);
	SYS_SetPowerCallback(on_power);
	WPAD_SetPowerButtonCallback(on_wpad_power);
	SYS_SetResetCallback(on_reset);
	WPAD_SetHostSyncButtonCallback(on_sync);
	font_load(&font20, rodin20_bin, rodin20_bin_size);
	font_load(&font32, rodin32_bin, rodin32_bin_size);
	gfx_image_load(&cursor, cursor_bin, cursor_bin_size);
	cols = gfx_widescreen ? 8 : 6;
	tour_init();
	if (!mute && !tour_active())
		music_start(music_mp3, music_mp3_size, MUSIC_VOLUME);
	LWP_CreateThread(&load_thread, load, NULL, load_stack, sizeof(load_stack), 60);

	while (!quit) {
		u64 t = gettime();
		float dt = ticks_to_microsecs(t - last) / 1e6f;

		last = t;
		if (dt > 0.1f)
			dt = 0.1f;
		now += dt;
		if (pair) {
			pair = 0;
			WPAD_StartPairing();
		}
		read_input(&in);
		tour_input(&in.pointer, &in.x, &in.y, &in.down, now);
		if (in.down & IN_HOME)
			quit = 1;

		if (screen == SCREEN_LOADING && load_result) {
			LWP_JoinThread(load_thread, NULL);
			next_screen = load_result;
		}
		if (screen == SCREEN_GRID)
			grid_input(&in);
		else if (screen == SCREEN_DETAIL)
			detail_input(&in);
		if (next_screen >= 0) {
			fade = fminf(1, fade + dt / FADE_OUT_S);
			if (fade >= 1)
				switch_screen();
		} else
			fade = fmaxf(0, fade - dt / FADE_IN_S);
		scroll = approach(scroll, scroll_to, 12, dt);

		if (screen == SCREEN_DETAIL && detail_ok && detail.faces_pending)
			mii_model_make_faces(&detail, &res, BG);
		gfx_frame_begin(BG);
		switch (screen) {
		case SCREEN_LOADING:
			font_draw(&font32, margin(), 228, "Loading your Miis", INK);
			if (load_total) {
				gfx_round_rect(margin(), 250, 240, 6, 3, LINE);
				gfx_round_rect(margin(), 250, 240.0f * load_done / load_total, 6, 3, INK_SOFT);
			}
			break;
		case SCREEN_GRID:
			draw_grid(&in, dt);
			break;
		case SCREEN_DETAIL:
			draw_detail(&in, dt);
			break;
		case SCREEN_NO_MIIS:
			message("No Miis yet", "Make a Mii in the Mii Channel, then open this demo again.", NULL);
			break;
		case SCREEN_NO_DB:
			message("Can't read your Miis", "The Mii Channel's list of Miis couldn't be read.", NULL);
			break;
		case SCREEN_NO_FACES:
			message("Can't read the Mii faces", "Open this demo from the Homebrew Channel so it can read them,",
					"or put a copy of RFL_Res.dat in sd:/mii.");
			break;
		default:
			message("Out of memory", "This Wii ran out of memory loading the Miis.", NULL);
			break;
		}
		if (fade > 0) {
			float k = 1 - fade;

			gfx_rect(0, 0, gfx_w, gfx_h, RGBA(BG.r, BG.g, BG.b, 255 * (1 - k * k * k)));
		}
		draw_pointer(&in, dt);
		tour_frame(now);
		gfx_frame_end();
	}

	music_stop();
	WPAD_Shutdown();
	if (power_off || tour_active())
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	return 0;
}
