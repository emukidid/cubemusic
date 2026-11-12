/* trackinfo.h -- what vgmstream reported about each track, cached on SD
 *
 * /cubemusic/cache/<GAMEID>.json. Filled in whenever a track is played or
 * backed up, so lists can show lengths without opening every file.
 * Raw numbers are stored (not a duration), so the shown length follows the
 * current loop and fade settings.
 *
 * Thread safe: the backup thread records into the same cache the menu reads.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __TRACKINFO_H
#define __TRACKINFO_H

#include <stdbool.h>
#include <stdint.h>
#include <ogc/mutex.h>
#include "libvgmstream.h"
#include "audio.h"

#define CACHE_DIR "/cubemusic/cache"

typedef struct {
	char *path;
	int sample_rate;
	int channels;
	int64_t samples;
	bool loop;
	int64_t loop_start, loop_end;
	char codec[48];
	bool playable;           /* false: vgmstream couldn't open it */
} TrackInfo;

typedef struct {
	char game_id[7];
	TrackInfo *items;
	int count, cap;
	bool dirty;
	mutex_t lock;
} InfoCache;

void info_load(InfoCache *c, const char *game_id);
void info_free(InfoCache *c);
bool info_save(InfoCache *c);    /* only writes if something changed */

/* Copies the entry for path into out. False if it isn't known yet. */
bool info_get(InfoCache *c, const char *path, TrackInfo *out);

/* Records what vgmstream reported (fmt NULL: it couldn't open the file). */
void info_record(InfoCache *c, const char *path, const libvgmstream_format_t *fmt);

/* Opens sf (stream subsong, 0 for the first) with vgmstream just to read its
 * header, and records it under key. */
void info_probe(InfoCache *c, const char *key, libstreamfile_t *sf, int subsong);

/* Seconds a track plays for with these settings; 0 if it loops forever. */
float info_duration(const TrackInfo *t, const AudioSettings *s);

#endif
