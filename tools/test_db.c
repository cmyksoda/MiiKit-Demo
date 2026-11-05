// SPDX-License-Identifier: GPL-3.0-or-later
/* A Mii database of random Miis for testing in Dolphin.
 *
 * cc -DMII_NO_OGC -Isource/miikit tools/test_db.c source/miikit/mii.c -o test_db && ./test_db COUNT RFL_DB.dat
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"

#define CRC_AT 0x1F1DE
#define TICKS_PER_DAY (86400ull * 60750000)

static const char *const names[] = { "Mio", "Pixel", "Rodin", "Bead", "Juniper", "Noé", "ミオ", "Wren",
									 "Quill", "Sorrel", "Kestrel", "Luma", "Fable", "Beryl", "Hazel",
									 "Tanka", "Moss", "Clover", "Ｗｉｉ", "Pip" };

int main(int argc, char **argv) {
	static uint8_t f[MII_DB_FILE_SIZE];
	static const uint8_t mac[6] = { 0x00, 0x17, 0xab, 0x5d, 0x10, 0x2e };
	int n = argc > 2 ? atoi(argv[1]) : 0, i;
	uint32_t seed = 20261029;
	uint16_t crc;
	mii_db db;
	FILE *out;

	if (n < 1 || n > MII_DB_SLOTS) {
		fprintf(stderr, "usage: test_db COUNT RFL_DB.dat\n");
		return 2;
	}
	/* An empty database as the Wii Menu makes it: no Miis, every hidden node free. */
	memcpy(f, "RNOD", 4);
	memcpy(f + 0x1D00, "RNHD", 4);
	memset(f + 0x1D04, 0xff, 4);
	for (i = 0; i < 10000; ++i)
		memcpy(f + 0x1D08 + i * 12 + 8, "\x7f\xff\x7f\xff", 4);
	crc = mii_crc16(f, CRC_AT);
	f[CRC_AT] = crc >> 8;
	f[CRC_AT + 1] = crc & 0xff;
	if (mii_db_attach(&db, f, sizeof(f)) < 0) {
		fprintf(stderr, "the empty database does not attach\n");
		return 1;
	}
	for (i = 0; i < n; ++i) {
		mii m;
		const char *why = NULL;
		int r;

		mii_random(&m, MII_ANY, MII_ANY, &seed);
		mii_set_name(m.name, names[i % (sizeof(names) / sizeof(names[0]))]);
		mii_set_name(m.creator, i % 3 ? "Mio" : "");
		m.height = (uint8_t) ((i * 37) % 129);
		m.build = (uint8_t) ((i * 53 + 20) % 129);
		m.fav_color = (uint8_t) (i % 12);
		m.favorite = i % 4 == 0;
		m.birth_month = (uint8_t) (i % 5 ? 1 + i % 12 : 0);
		m.birth_day = (uint8_t) (m.birth_month ? 1 + (i * 7) % 28 : 0);
		m.local_only = i % 5 == 0;
		/* 2008-03-03 plus a few weeks a Mii, counted from 2000-01-01 as the Wii's clock is. */
		mii_id_make(m.id, (3005 + i * 23ull) * TICKS_PER_DAY, mac);
		if ((r = mii_check(&m, &why)) != MII_OK || (r = mii_db_put(&db, i, &m)) < 0) {
			fprintf(stderr, "Mii %d: %s (%s)\n", i, mii_strerror(r), why ? why : "");
			return 1;
		}
	}
	crc = mii_crc16(f, CRC_AT);
	f[CRC_AT] = crc >> 8;
	f[CRC_AT + 1] = crc & 0xff;
	if (!(out = fopen(argv[2], "wb")) || fwrite(f, 1, sizeof(f), out) != sizeof(f)) {
		perror(argv[2]);
		return 1;
	}
	fclose(out);
	printf("%s: %d Miis\n", argv[2], n);
	return 0;
}
