// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOUR_H
#define TOUR_H

#include <gctypes.h>

/* A TOUR build plays a scripted visit in Dolphin and saves screenshots to SD. */
#ifdef TOUR
void tour_init(void);
int tour_active(void);
void tour_input(int *pointer, float *x, float *y, u32 *down, float t);
void tour_frame(float t);
#else
static inline void tour_init(void) {}
static inline int tour_active(void) { return 0; }
static inline void tour_input(int *pointer, float *x, float *y, u32 *down, float t) {
	(void) pointer, (void) x, (void) y, (void) down, (void) t;
}
static inline void tour_frame(float t) {
	(void) t;
}
#endif

#endif
