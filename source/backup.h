/* backup.h -- copies tracks from the disc to SD on a worker thread
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __BACKUP_H
#define __BACKUP_H

#include <gccore.h>
#include <stdbool.h>
#include "tracks.h"

#define CUBEMUSIC_DIR  "/cubemusic"
#define BACKUP_DIR     CUBEMUSIC_DIR "/backup"

typedef struct {
	volatile bool running;
	volatile bool cancel;      /* set to stop after the current file */
	volatile int file_index;   /* 1-based, of file_total */
	volatile int file_total;
	int track_total;           /* tracks ticked (a bank's streams share one file, and the banner is a file too) */
	volatile u32 bytes_done;
	volatile u32 bytes_total;
	volatile u32 bytes_written; /* actually written to SD (not skipped) */
	volatile int skipped;      /* already on SD with the same size */
	volatile int errors;
	char current[96];          /* name of the file being copied */
	char error[128];           /* last error, for the status line */
} BackupStatus;

extern BackupStatus backup_status;

/* Starts copying the selected tracks (plus their companion files and the
 * game's banner) to BACKUP_DIR/<game id>/<path on disc>. Returns false if
 * a backup is already running or nothing is selected. The list must stay
 * loaded until backup_status.running goes false. */
bool backup_start(TrackList *list);

/* Where a disc file is (or would be) backed up. */
void backup_path(char *out, size_t size, const char *game_id, const char *disc_path);

/* Creates every directory in path up to its last '/'. */
bool mkdir_parents(const char *path);

#endif
