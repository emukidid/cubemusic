/* banner.h -- a game's opening.bnr (name and 96x32 picture)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __BANNER_H
#define __BANNER_H

#include <gccore.h>
#include <stdbool.h>

#define BANNER_FILE      "opening.bnr"
#define BANNER_W         96
#define BANNER_H         32
#define BANNER_IMG_SIZE  (BANNER_W * BANNER_H * 2)   /* RGB5A3, already in GX tile order */

typedef struct {
	bool valid;
	char title[128];          /* long title, or the short one if that's empty */
	char maker[128];
	u8 image[BANNER_IMG_SIZE] __attribute__((aligned(32)));
} Banner;

/* Reads the banner from a buffer holding the whole file. */
bool banner_parse(Banner *b, const u8 *data, u32 size);

/* From the disc in the drive (fst_load must have run). */
bool banner_load_disc(Banner *b);

/* From a backed-up copy on SD. */
bool banner_load_file(Banner *b, const char *path);

#endif
