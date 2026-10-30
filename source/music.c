// SPDX-License-Identifier: GPL-3.0-or-later
#include <malloc.h>
#include <math.h>
#include <string.h>

#include <asndlib.h>
#include <gccore.h>
#include <mad.h>
#include <ogc/machine/processor.h>

#include "music.h"

/* data/music.mp3 is 96 s of music after its encoder's 1105 samples of delay;
 * the rest of its last frame is padding. */
#define LOOP_START 1105
#define LOOP_SAMPLES (96 * 44100)
/* The loop's last samples aren't silent: a short fade stops a click at the seam. */
#define FADE 128
#define NBUF 6
#define BUF_FRAMES 4608
#define BUF_BYTES (BUF_FRAMES * 4)
/* 44.1 kHz to the DSP's own 48 kHz (147 in, 160 out): ASND's linear
 * interpolation dulled and gritted everything above 4 kHz. */
#define UP 160
#define DOWN 147
#define TAPS 16

static u8 *mp3;
static u32 mp3_size;
static s16 *buf[NBUF];
static volatile u32 decoded, handed;
static volatile int running;
static s32 voice = -1;
static lwp_t thread = LWP_THREAD_NULL;
static lwpq_t queue = LWP_TQUEUE_NULL;
static u8 stack[16 * 1024] ATTRIBUTE_ALIGN(32);

static struct mad_stream stream;
static struct mad_frame frame;
static struct mad_synth synth;
static u32 synth_at, pos, emitted;
static float taps[UP][TAPS], ring[TAPS][2];
static u32 ring_count, phase, base;

static void restart(void) {
	mad_synth_finish(&synth);
	mad_frame_finish(&frame);
	mad_stream_finish(&stream);
	mad_stream_init(&stream);
	mad_frame_init(&frame);
	mad_synth_init(&synth);
	mad_stream_buffer(&stream, mp3, mp3_size + MAD_BUFFER_GUARD);
	synth_at = synth.pcm.length = 0;
	pos = emitted = 0;
}

static int next_frame(void) {
	while (mad_frame_decode(&frame, &stream))
		if (!MAD_RECOVERABLE(stream.error))
			return 0;
	mad_synth_frame(&synth, &frame);
	synth_at = 0;
	return 1;
}

static s16 sample(mad_fixed_t s) {
	s += 1L << (MAD_F_FRACBITS - 16);
	if (s >= MAD_F_ONE)
		s = MAD_F_ONE - 1;
	else if (s < -MAD_F_ONE)
		s = -MAD_F_ONE;
	return s >> (MAD_F_FRACBITS + 1 - 16);
}

/* The loop's next 44.1 kHz sample. */
static void next_input(float *l, float *r) {
	for (;;) {
		s32 a, b;
		u32 left;

		if (synth_at >= synth.pcm.length && !next_frame()) {
			restart();
			continue;
		}
		a = sample(synth.pcm.samples[0][synth_at]);
		b = synth.pcm.channels == 2 ? sample(synth.pcm.samples[1][synth_at]) : a;
		++synth_at;
		if (++pos <= LOOP_START)
			continue;
		left = LOOP_SAMPLES - emitted;
		*l = a;
		*r = b;
		if (left < FADE) {
			*l *= (float) left / FADE;
			*r *= (float) left / FADE;
		}
		if (++emitted == LOOP_SAMPLES)
			restart();
		return;
	}
}

/* A Blackman-windowed sinc cut at 19.8 kHz, one row of taps per output phase. */
static void make_taps(void) {
	int p, k;

	for (p = 0; p < UP; ++p) {
		float frac = (float) p / UP, sum = 0;

		for (k = 0; k < TAPS; ++k) {
			float x = k - (TAPS / 2 - 1) - frac, y = 0.9f * x * (float) M_PI;
			float w = 0.42f + 0.5f * cosf((float) M_PI * x / (TAPS / 2))
					  + 0.08f * cosf(2 * (float) M_PI * x / (TAPS / 2));

			taps[p][k] = (fabsf(y) < 1e-6f ? 1 : sinf(y) / y) * w;
			sum += taps[p][k];
		}
		for (k = 0; k < TAPS; ++k)
			taps[p][k] /= sum;
	}
}

static s16 clamp16(float v) {
	return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16) lrintf(v);
}

static void fill(s16 *out) {
	int n, k;

	for (n = 0; n < BUF_FRAMES; ++n) {
		const float *t = taps[phase];
		float l = 0, r = 0;

		while (ring_count < base + TAPS) {
			float *slot = ring[ring_count++ % TAPS];

			next_input(&slot[0], &slot[1]);
		}
		for (k = 0; k < TAPS; ++k) {
			const float *v = ring[(base + k) % TAPS];

			l += v[0] * t[k];
			r += v[1] * t[k];
		}
		*out++ = clamp16(l);
		*out++ = clamp16(r);
		phase += DOWN;
		base += phase / UP;
		phase %= UP;
	}
}

/* From the audio IRQ, when ASND's second buffer is empty. */
static void feed(s32 v) {
	if (decoded > handed && ASND_AddVoice(v, buf[handed % NBUF], BUF_BYTES) == SND_OK)
		++handed;
	LWP_ThreadSignal(queue);
}

static void *decode(void *arg) {
	(void) arg;
	while (running) {
		u32 level;

		/* Two buffers are ASND's: the one playing and the one waiting. */
		while (running && decoded < handed + NBUF - 2) {
			fill(buf[decoded % NBUF]);
			DCFlushRange(buf[decoded % NBUF], BUF_BYTES);
			++decoded;
		}
		/* After a late buffer the voice waits for one, so hand it over here. */
		_CPU_ISR_Disable(level);
		if (decoded > handed && ASND_TestVoiceBufferReady(voice) == 1 &&
				ASND_AddVoice(voice, buf[handed % NBUF], BUF_BYTES) == SND_OK)
			++handed;
		_CPU_ISR_Restore(level);
		if (running)
			LWP_ThreadSleep(queue);
	}
	return NULL;
}

int music_start(const u8 *data, u32 size, int volume) {
	int i;

	/* libmad decodes the last frame only with zeroes after it. */
	if (!(mp3 = malloc(size + MAD_BUFFER_GUARD)))
		return -1;
	memcpy(mp3, data, size);
	memset(mp3 + size, 0, MAD_BUFFER_GUARD);
	mp3_size = size;
	for (i = 0; i < NBUF; ++i)
		if (!(buf[i] = memalign(32, BUF_BYTES)))
			return -1;
	mad_stream_init(&stream);
	mad_frame_init(&frame);
	mad_synth_init(&synth);
	restart();
	make_taps();
	/* The first taps reach before the music: silence there. */
	ring_count = TAPS / 2 - 1;
	fill(buf[0]);
	DCFlushRange(buf[0], BUF_BYTES);
	decoded = 1;
	handed = 1;
	running = 1;

	ASND_Init();
	ASND_Pause(0);
	voice = ASND_GetFirstUnusedVoice();
	LWP_InitQueue(&queue);
	ASND_SetVoice(voice, VOICE_STEREO_16BIT, 48000, 0, buf[0], BUF_BYTES, volume, volume, feed);
	LWP_CreateThread(&thread, decode, NULL, stack, sizeof(stack), 80);
	return 0;
}

void music_stop(void) {
	if (voice < 0)
		return;
	ASND_StopVoice(voice);
	running = 0;
	LWP_ThreadSignal(queue);
	LWP_JoinThread(thread, NULL);
	LWP_CloseQueue(queue);
	ASND_End();
	voice = -1;
}
