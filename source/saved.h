/* saved.h -- browsing tracks already backed up to SD
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __SAVED_H
#define __SAVED_H

#include <stdbool.h>
#include "banner.h"
#include "names.h"

typedef struct {
	char game_id[7];
	char title[128];          /* names file title, else the saved banner's, else the game ID */
	long long added;          /* when its folder was made (first backed up), seconds */
} SavedGame;

typedef struct {
	char path[256];          /* relative to the game's folder, i.e. the path on disc ("#<n>" after it for a stream of a bank) */
	char name[96];
	bool selected;           /* ticked (for deleting) */
} SavedTrack;

/* Games with a folder under BACKUP_DIR. Returns the count; free with saved_free_games. */
int saved_list_games(SavedGame **games);
void saved_free_games(SavedGame *games);

/* The playable files backed up for a game, sorted by path, named from names
 * (companions and hidden tracks left out). */
int saved_list_tracks(const char *game_id, const NameTable *names, SavedTrack **tracks);
void saved_free_tracks(SavedTrack *tracks);

/* Deletes a backed-up track from SD: its file and the files that go with it
 * (FooR.dsp with FooL.dsp, Foo.spd with Foo.spt...). For a stream of a bank
 * only its entry in names goes (the caller saves names), and the bank file
 * with the last of them. Folders left empty are removed. Returns the bytes
 * freed. */
long long saved_delete_track(const char *game_id, NameTable *names, const char *key);

/* Deletes a game's whole backup folder (its names and track info stay: they
 * apply to the disc too). Returns the bytes freed. */
long long saved_delete_game(const char *game_id);

/* Whether a game's backup folder still has any track files in it (hidden
 * ones included). */
bool saved_has_tracks(const char *game_id);

#endif
