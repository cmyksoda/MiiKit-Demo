// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MUSIC_H
#define MUSIC_H

#include <gctypes.h>

/* Loops the MP3 without a gap, decoding it on a thread as it plays. */
int music_start(const u8 *mp3, u32 size, int volume);
void music_stop(void);

#endif
