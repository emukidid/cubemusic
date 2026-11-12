/* settings.h -- settings and play/backup statistics (/cubemusic/settings.json)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_FILE "/cubemusic/settings.json"

enum { RES_AUTO, RES_480I, RES_480P, RES_COUNT };
enum { END_STOP, END_NEXT, END_REPEAT, END_NEXT_GAME, END_COUNT };   /* END_NEXT_GAME: saved tracks carry on into the next game */
enum { SORT_NAME_UP, SORT_NAME_DOWN, SORT_NEWEST, SORT_OLDEST, SORT_COUNT };   /* saved games */

#define OVERSCAN_MAX  10    /* percent */
#define VOLUME_MAX    10

typedef struct {
	/* display */
	int resolution;
	bool widescreen;        /* 16:9 TV that stretches the picture */
	bool full_width;        /* 704 pixels across instead of 640 */
	int overscan;           /* 0..OVERSCAN_MAX percent pulled in from the edges */
	bool intro;
	/* sound */
	int music_volume;       /* 0..VOLUME_MAX */
	int menu_volume;        /* 0..VOLUME_MAX, 0 = no menu sounds */
	/* playback (also in audio_settings) */
	int loop_count;         /* 1..9 */
	int fade_seconds;       /* 0..20 */
	bool loop_forever;
	int track_end;          /* END_* */
	int saved_sort;         /* SORT_* */
	/* exit */
	char launcher[64];      /* .dol at the root of the SD card, "" = automatic */
} Settings;

typedef struct {
	double seconds_played;
	uint64_t bytes_backed_up;
} Stats;

extern Settings settings;
extern Stats stats_all;       /* all time, including this session */
extern Stats stats_session;

void settings_load(void);
bool settings_save(void);     /* also writes the all-time stats */
/* copies the playback settings into audio_settings */
void settings_apply_audio(void);

void stats_add_played(double seconds);
void stats_add_backed_up(uint64_t bytes);

#endif
