/* names.h -- per-game track names and corrections, kept on SD
 *
 * /cubemusic/names/<GAMEID>.json, or for every region of a game
 * /cubemusic/names/<GAM>_<MK>.json (the region letter replaced by '_').
 * The exact file wins; the region-less one is used when there isn't one.
 *
 *   { "title": "007: Agent Under Fire",
 *     "tracks": [
 *       { "path": "Music/Menu/TJBTheme.asf", "name": "Main Theme" },
 *       { "path": "Music/Extra/new.asf",     "name": "Added track" },
 *       { "path": "Music/Misc/silent.asf",   "hidden": true } ] }
 *
 * A path meta.c doesn't list is added to the game's tracks, so the built-in
 * list can be corrected without a new .dol.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __NAMES_H
#define __NAMES_H

#include <stdbool.h>

#define NAMES_DIR "/cubemusic/names"

typedef struct {
	char *path;
	char *name;      /* NULL: keep the default name */
	bool hidden;
	bool keep;       /* saved even without a name: a track meta.c doesn't list (the file had it, or a disc search found it) */
} NameEntry;

typedef struct {
	char game_id[7];
	char title[128];          /* "" if the file doesn't set one */
	NameEntry *entries;
	int count, cap;
	bool from_regionless;    /* loaded from <GAM>_<MK>.json */
} NameTable;

/* Loads the names for a game ID (exact file, else region-less).
 * Returns false if neither exists; t is still usable (empty). */
bool names_load(NameTable *t, const char *game_id);
void names_free(NameTable *t);

/* By full disc path; an entry whose path is the end of it (on a folder
 * boundary, as rips list them: "x.dsp" for "Audio/x.dsp") also matches. */
const NameEntry *names_find(const NameTable *t, const char *path);

/* For the name editor: name NULL clears a custom name. */
void names_set(NameTable *t, const char *path, const char *name, bool hidden);

/* Forgets path's entry (exactly that path) altogether. */
void names_remove(NameTable *t, const char *path);

/* Adds a track meta.c doesn't list (from a disc search). */
void names_add(NameTable *t, const char *path);

/* Writes the table to the exact-ID file (so edits never touch the shared
 * region-less file). */
bool names_save(const NameTable *t);

/* "Export a names file": every track of a game, with its current name, written
 * to /cubemusic/export/<GAM>_<MK>.json (region letter as '_', as db/ names
 * them) for naming on a computer. The extra "length" and "loops" fields help
 * tell tracks apart; loading a names file ignores them. */
#define EXPORT_DIR "/cubemusic/export"

typedef struct {
	const char *path;        /* the track's key: disc path, "#<n>" for a stream of a bank */
	const char *name;
	bool hidden;
	float seconds;           /* one play through the file; 0 if not known */
	bool loops;
} ExportTrack;

/* out (may be NULL) receives the file's path. False if it couldn't be written. */
bool names_export(const char *game_id, const char *title, const ExportTrack *tracks, int count,
                  char *out, size_t out_size);

#endif
