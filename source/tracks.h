/* tracks.h -- the music files known for a game, resolved against the disc
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __TRACKS_H
#define __TRACKS_H

#include <gccore.h>
#include <stdbool.h>
#include "gcm_fst.h"
#include "names.h"
#include "trackinfo.h"

/* Tracks inside a bank (one file holding many streams, like Mario Party's
 * .pdt) are subsongs: their key is "<disc path>#<subsong>". */
#define TRACK_KEY_MAX 272

typedef struct Track {
	const char *path;        /* path as meta.c / the names file give it (may be just the end of the disc path) */
	char key[TRACK_KEY_MAX]; /* see tracks_key */
	int subsong;             /* 1.. for a stream inside a bank, 0 for a whole file */
	char name[96];           /* custom name, else the file name without extension */
	const DiscFile *file;    /* where it is on the disc; NULL if this disc doesn't have it */
	bool companion;          /* data that goes with another track (.spd for .spt, ...): backed up, not listed */
	bool hidden;             /* hidden by the names file */
	bool custom_name;        /* name came from the names file */
	bool on_sd;              /* already backed up (same size) */
	bool selected;           /* ticked for backup */
	bool paired;             /* left half of an L/R pair: vgmstream plays both as stereo */
	struct Track *pair_left; /* set on a right half: the left half it goes with */
} Track;

typedef struct {
	char game_id[7];
	char game_name[128];      /* names file title, else banner, else disc header */
	Track *tracks;
	int count;               /* meta.c entries plus ones the names file adds */
	int found;               /* how many of them are on this disc */
	int meta_count;          /* meta.c's entries... */
	int meta_found;          /* ...and how many of those are on this disc */
	int extra_found;         /* names-file tracks meta.c doesn't list (an earlier search's) on this disc */
	int *order;              /* the listable tracks (indexes into tracks), sorted by name */
	int order_count;
	NameTable names;
	InfoCache info;
} TrackList;

/* Fills list for the disc in the drive (fst_load must have run): meta.c's
 * entries plus the names file's, with names applied and SD status checked.
 * Returns the number of listable tracks. */
int tracks_load_disc(TrackList *list, const char *game_id, const char *header_name);
void tracks_free(TrackList *list);

/* Searching the disc for music meta.c doesn't know about (its rip may have
 * renamed the files): every file vgmstream knows the extension of is opened,
 * and the ones that play and aren't short sound effects are added to the list
 * (and to the names file, saved to SD, so it's only needed once).
 * Runs on a thread; progress and cancel are shared with the UI. */
typedef struct {
	volatile int done, total, found;
	volatile bool cancel;
} ScanProgress;
/* Returns how many tracks it added. */
int tracks_scan_disc(TrackList *list, ScanProgress *progress);

/* The track's real path on the disc (the rip's path can be just the end of
 * it), with "#<subsong>" for a stream inside a bank. Backups, the track info
 * cache and names are all keyed by this. */
const char *tracks_key(const Track *t);

/* Splits a key into the file's path and the subsong (0 if it hasn't one). */
int tracks_key_split(const char *key, char *path, size_t size);

/* Re-checks which tracks are on SD (after a backup). */
void tracks_refresh_sd(TrackList *list);

/* Re-applies names after the names table changed (the name editor). */
void tracks_apply_names(TrackList *list);

/* True for files that belong to another track or aren't audio (by extension). */
bool tracks_is_companion(const char *path);

/* The name shown before track names are loaded: file name without extension
 * (and for a subsong key, "_00012" after it, as rips name them). */
void tracks_default_name(char *out, size_t size, const char *path);

/* Same, and for the left half of an L/R pair without the L ("rui_samusL" -> "rui_samus"). */
void tracks_default_track_name(char *out, size_t size, const char *path, bool paired);

/* If path is named like the left half of a stereo pair (fooL.dsp, foo_0.dsp,
 * foo.L, ...) writes the right half's path ("fooR.dsp") and returns true. */
bool tracks_right_partner(const char *path, char *out, size_t size);

/* Listable tracks (on the disc, not companions, not hidden), by index. */
int tracks_visible_count(const TrackList *list);
Track *tracks_visible(TrackList *list, int index);

#endif
