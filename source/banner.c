/* banner.c -- a game's opening.bnr (name and 96x32 picture)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "banner.h"
#include "gcm_fst.h"
#include "dvd.h"
#include "text.h"

#define BNR_IMG_OFS     0x20
#define BNR_TEXT_OFS    0x1820
#define BNR_MIN_SIZE    (BNR_TEXT_OFS + 0x140)

static void copy_text(char *dst, size_t dst_size, const u8 *src, size_t src_size) {
	game_text_to_utf8(dst, dst_size, src, src_size);
}

bool banner_parse(Banner *b, const u8 *data, u32 size) {
	memset(b, 0, sizeof(*b));
	if (size < BNR_MIN_SIZE || memcmp(data, "BNR", 3))
		return false;
	memcpy(b->image, data + BNR_IMG_OFS, BANNER_IMG_SIZE);
	DCFlushRange(b->image, BANNER_IMG_SIZE);
	const u8 *t = data + BNR_TEXT_OFS;
	copy_text(b->title, sizeof(b->title), t + 0x40, 64);
	if (!b->title[0])
		copy_text(b->title, sizeof(b->title), t, 32);
	copy_text(b->maker, sizeof(b->maker), t + 0x80, 64);
	if (!b->maker[0])
		copy_text(b->maker, sizeof(b->maker), t + 0x20, 32);
	b->valid = true;
	return true;
}

bool banner_load_disc(Banner *b) {
	const DiscFile *f = fst_find(BANNER_FILE);
	if (!f || f->size < BNR_MIN_SIZE || f->size > 64 * 1024)
		return false;
	u8 *buf = malloc(f->size);
	bool ok = buf && dvd_read(buf, f->offset, f->size) >= 0 && banner_parse(b, buf, f->size);
	free(buf);
	return ok;
}

bool banner_load_file(Banner *b, const char *path) {
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return false;
	u8 *buf = malloc(64 * 1024);
	u32 size = buf ? fread(buf, 1, 64 * 1024, fp) : 0;
	fclose(fp);
	bool ok = buf && banner_parse(b, buf, size);
	free(buf);
	return ok;
}
