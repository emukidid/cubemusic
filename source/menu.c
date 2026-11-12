/* menu.c -- the screens and what the buttons do
 *
 * Main menu:  Play from disc / Saved tracks / Settings / Credits / Exit
 * Track lists (disc and saved):
 *   A play (on the playing track: open the player), Y tick, X options,
 *   START back up the ticked tracks, Z pause, L/R previous/next track, B back
 * Player:     A or Z pause, L/R previous/next track, control pad left/right seek,
 *             up/down volume, B back to the list
 *
 * Everything runs on the main thread once a frame; reading the disc,
 * decoding and backing up happen on their own threads.
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <gccore.h>

#include "menu.h"
#include "video.h"
#include "font.h"
#include "ui.h"
#include "disc.h"
#include "intro.h"
#include "keyboard.h"
#include "text.h"
#include "sound.h"
#include "settings.h"
#include "dolload.h"
#include "dvd.h"
#include "gcm_fst.h"
#include "tracks.h"
#include "banner.h"
#include "audio.h"
#include "disc_sf.h"
#include "backup.h"
#include "saved.h"
#include "names.h"
#include "trackinfo.h"

#define LIST_X     SAFE_L
#define LIST_W     (SAFE_R - SAFE_L)
#define ROW_H      33
#define LIST_ROWS  8
#define ROWS_Y     (CONTENT_T + 8)

enum {
	ST_MAIN,
	ST_DISC_LOADING,
	ST_DISC_LIST,
	ST_SAVED_GAMES,
	ST_SAVED_LIST,
	ST_PLAYER,
	ST_SETTINGS,
	ST_CREDITS,
};

static int state;
static int player_return;      /* the list the player goes back to */
static float state_at;         /* when the screen appeared, for its fade-in */
static bool sd_present;

/* disc */
static TrackList disc_tracks;
static Banner disc_banner;
static GXTexObj disc_banner_tex;
static char disc_default_title[128];
static volatile int disc_load_result;   /* 0 = loading, 1 = done, <0 = error */
static char disc_load_step[64];
static lwp_t disc_thread = LWP_THREAD_NULL;
static ScanProgress scan;              /* searching the disc for music meta.c doesn't list */
static bool scanning, scan_from_list;

/* saved */
static SavedGame *saved_games;
static Banner *saved_banners;
static GXTexObj *saved_banner_tex;
static int saved_game_count;
static SavedTrack *saved_tracks;
static int saved_track_count;
static int saved_game_sel;
static NameTable saved_names;
static InfoCache saved_info;
static char saved_info_game[7];   /* the game saved_info (and saved_names) are for, "" none */
static bool backup_was_running;
static bool resort_pending;       /* the saved games' order changed (done before drawing) */

/* The saved game that's playing: its tracks as they were when it started.
 * Kept apart from the list on screen, so it carries on while you browse
 * other games (and into the next game, with END_NEXT_GAME). */
static SavedTrack *pq;
static int pq_count;
static char pq_game[7];

/* list cursors */
static int sel, scroll;
static int main_sel;
static int saved_games_sel, saved_games_scroll;
static int settings_sel, settings_scroll;

/* what's playing */
enum { SRC_NONE, SRC_DISC, SRC_SAVED };
static int play_src;
static bool play_opened;        /* audio_play's result has been dealt with (update_playback) */
static char play_key[256];     /* the track's path (disc path, or path under the game's folder) */

/* input */
static u16 down, dirs;

/* ---------------------------------------------------------------- input */

/* Control pad / stick directions: pressed, then repeating while held. */
static u16 read_dirs(void) {
	static int held_frames;
	static u16 last;
	u16 held = PAD_ButtonsHeld(0) & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
	s8 x = PAD_StickX(0), y = PAD_StickY(0);
	if (y > 50) held |= PAD_BUTTON_UP;
	if (y < -50) held |= PAD_BUTTON_DOWN;
	if (x > 50) held |= PAD_BUTTON_RIGHT;
	if (x < -50) held |= PAD_BUTTON_LEFT;
	if (!held || held != last) {
		held_frames = 0;
		last = held;
	}
	if (!held)
		return 0;
	held_frames++;
	if (held_frames == 1 || (held_frames > 18 && held_frames % 4 == 0))
		return held;
	return 0;
}

/* moves a list cursor (up/down, left/right a page); returns true if it moved */
static bool move_in_list(int *s, int *top, int count, int visible) {
	if (!count)
		return false;
	int old = *s;
	if (dirs & PAD_BUTTON_DOWN) *s = (*s + 1) % count;
	if (dirs & PAD_BUTTON_UP) *s = (*s + count - 1) % count;
	if (count > visible && (dirs & PAD_BUTTON_RIGHT)) *s = *s + visible < count ? *s + visible : count - 1;
	if (count > visible && (dirs & PAD_BUTTON_LEFT)) *s = *s - visible > 0 ? *s - visible : 0;
	if (*s < *top) *top = *s;
	if (*s >= *top + visible) *top = *s - visible + 1;
	if (*top > count - visible) *top = count - visible > 0 ? count - visible : 0;
	if (*s != old)
		sound_play(SFX_HOVER);
	return *s != old;
}

static void go(int st) {
	state = st;
	state_at = frame_time;
}

static float screen_fade(void) {
	float t = (frame_time - state_at) / 0.25f;
	return t > 1 ? 1 : t;
}

static void icon_mirrored(int icon, float cx, float cy, float size, Color c) {
	const float m[6] = { -1, 0, 2 * cx, 0, 1, 0 };
	gfx_transform(m);
	ui_icon(icon, cx, cy, size, c);
	gfx_identity();
}

/* ---------------------------------------------------------------- popup */

typedef void (*PopupFn)(int choice);   /* -1: cancelled */

static struct {
	bool open;
	char title[96];
	char note[400];
	char rows[8][80];
	const char *row_ptr[8];
	int count, sel;
	bool danger;            /* a red confirmation (Cancel is first, and selected) */
	PopupFn fn;
} popup;
static bool popup_ready;   /* (the press that opened it isn't also its answer) */

static void popup_open(PopupFn fn, const char *title, const char *note, int count, ...) {
	memset(&popup, 0, sizeof(popup));
	snprintf(popup.title, sizeof(popup.title), "%s", title);
	snprintf(popup.note, sizeof(popup.note), "%s", note ? note : "");
	va_list ap;
	va_start(ap, count);
	for (int i = 0; i < count && i < 8; i++) {
		snprintf(popup.rows[i], sizeof(popup.rows[i]), "%s", va_arg(ap, const char *));
		popup.row_ptr[i] = popup.rows[i];
	}
	va_end(ap);
	popup.count = count < 8 ? count : 8;
	popup.fn = fn;
	popup.open = true;
	popup_ready = false;
	sound_play(SFX_OPEN);
}

/* another row for a popup just opened with fewer */
static void popup_add(const char *row) {
	if (popup.count >= 8)
		return;
	snprintf(popup.rows[popup.count], sizeof(popup.rows[0]), "%s", row);
	popup.row_ptr[popup.count] = popup.rows[popup.count];
	popup.count++;
}

/* "Are you sure?" for something that can't be undone: choice 1 is yes */
static void popup_confirm(PopupFn fn, const char *title, const char *note, const char *yes) {
	popup_open(fn, title, note, 2, "Cancel", yes);
	popup.danger = true;
	popup.sel = 0;
}

static void popup_frame(void) {
	if (dirs & PAD_BUTTON_DOWN) { popup.sel = (popup.sel + 1) % popup.count; sound_play(SFX_HOVER); }
	if (dirs & PAD_BUTTON_UP) { popup.sel = (popup.sel + popup.count - 1) % popup.count; sound_play(SFX_HOVER); }
	int choice = -2;
	if (down & PAD_BUTTON_A) {
		choice = popup.sel;
		sound_play(SFX_SELECT);
	} else if (down & PAD_BUTTON_B) {
		choice = -1;
		sound_play(SFX_CANCEL);
	}
	if (choice != -2) {
		popup.open = false;
		if (popup.fn)
			popup.fn(choice);
	}
}

static void show_error(const char *msg) {
	popup_open(NULL, "Sorry", msg, 1, "OK");
	sound_play(SFX_ERROR);
}

/* ------------------------------------------------------------- playing */

static void fill_now_playing(const char *title, const char *game, GXTexObj *banner) {
	now_playing.active = true;
	now_playing.paused = false;
	snprintf(now_playing.title, sizeof(now_playing.title), "%s", title);
	snprintf(now_playing.game, sizeof(now_playing.game), "%s", game);
	snprintf(now_playing.format, sizeof(now_playing.format), "%s", audio_describe());
	now_playing.has_banner = banner != NULL;
	if (banner)
		now_playing.banner = *banner;
	now_playing.duration = 0;
	now_playing.position = 0;
	play_opened = false;
}

static void stop_music(void) {
	audio_stop();
	now_playing.active = false;
	play_src = SRC_NONE;
}

/* Starts sf (the decode thread opens it; update_playback records what
 * vgmstream said about it once it has). The cache is written between tracks
 * (here, before the new one starts) and when leaving a list, never while
 * playing: writing to SD holds libfat's lock, which would stall a track being
 * read from SD. False if the file couldn't be opened at all. */
static bool play_and_record(libstreamfile_t *sf, int subsong, InfoCache *info, const char *path) {
	audio_stop();
	if (info)
		info_save(info);
	if (!sf) {
		if (info)
			info_record(info, path, NULL);
		return false;
	}
	audio_play(sf, subsong);
	return true;
}

static GXTexObj *saved_banner_for(int g) {
	return saved_banners && saved_banners[g].valid ? &saved_banner_tex[g] : NULL;
}

static bool play_disc(int index) {
	Track *t = tracks_visible(&disc_tracks, index);
	if (!t)
		return false;
	if (!play_and_record(disc_sf_open(t->file->path), t->subsong, &disc_tracks.info, tracks_key(t))) {
		stop_music();
		return false;
	}
	play_src = SRC_DISC;
	snprintf(play_key, sizeof(play_key), "%s", tracks_key(t));
	fill_now_playing(t->name, disc_tracks.game_name, disc_banner.valid ? &disc_banner_tex : NULL);
	return true;
}

static int saved_game_index(const char *game_id) {
	for (int i = 0; i < saved_game_count; i++)
		if (!strcmp(saved_games[i].game_id, game_id))
			return i;
	return -1;
}

/* the open game's tracks become what's playing from */
static void pq_take(void) {
	saved_free_tracks(pq);
	pq = NULL;
	pq_count = 0;
	if (saved_track_count > 0 && (pq = malloc(saved_track_count * sizeof(SavedTrack)))) {
		memcpy(pq, saved_tracks, saved_track_count * sizeof(SavedTrack));
		pq_count = saved_track_count;
	}
	snprintf(pq_game, sizeof(pq_game), "%s", saved_games[saved_game_sel].game_id);
}

static void pq_clear(void) {
	saved_free_tracks(pq);
	pq = NULL;
	pq_count = 0;
	pq_game[0] = 0;
}

/* where the playing track's length etc. are recorded (NULL: a saved game
 * whose list isn't open; it's recorded when that list is opened) */
static InfoCache *play_info(void) {
	if (play_src == SRC_DISC)
		return &disc_tracks.info;
	if (play_src == SRC_SAVED && saved_info_game[0] && !strcmp(saved_info_game, pq_game))
		return &saved_info;
	return NULL;
}

static bool play_saved(int index) {
	int gi = saved_game_index(pq_game);
	if (index < 0 || index >= pq_count || gi < 0)
		return false;
	SavedGame *g = &saved_games[gi];
	char file[256], path[1024];
	int subsong = tracks_key_split(pq[index].path, file, sizeof(file));
	backup_path(path, sizeof(path), g->game_id, file);
	InfoCache *info = saved_info_game[0] && !strcmp(saved_info_game, pq_game) ? &saved_info : NULL;
	if (!play_and_record(sd_sf_open(path), subsong, info, pq[index].path)) {
		stop_music();
		return false;
	}
	play_src = SRC_SAVED;
	snprintf(play_key, sizeof(play_key), "%s", pq[index].path);
	fill_now_playing(pq[index].name, g->title, saved_banner_for(gi));
	return true;
}

/* where the playing track is in its list now (-1 if it's gone) */
static int playing_index(void) {
	if (play_src == SRC_DISC) {
		int n = tracks_visible_count(&disc_tracks);
		for (int i = 0; i < n; i++)
			if (!strcmp(tracks_key(tracks_visible(&disc_tracks, i)), play_key))
				return i;
	} else if (play_src == SRC_SAVED) {
		for (int i = 0; i < pq_count; i++)
			if (!strcmp(pq[i].path, play_key))
				return i;
	}
	return -1;
}

static int playing_count(void) {
	return play_src == SRC_DISC ? tracks_visible_count(&disc_tracks) : play_src == SRC_SAVED ? pq_count : 0;
}

static bool play_at(int src, int index) {
	bool ok = src == SRC_DISC ? play_disc(index) : play_saved(index);
	if (!ok)
		show_error("This track's format isn't supported.");
	return ok;
}

/* next/previous track (wrapping round) */
static void skip(int step) {
	int n = playing_count(), i = playing_index(), src = play_src;
	if (n > 0)
		play_at(src, ((i < 0 ? 0 : i) + step + n) % n);
}

/* The playing game's last track ended: carry on with the next saved game
 * that has tracks (in the games list's order, wrapping round). */
static bool next_game(void) {
	int gi = saved_game_index(pq_game);
	if (gi < 0)
		return false;
	for (int k = 1; k <= saved_game_count; k++) {
		const SavedGame *g = &saved_games[(gi + k) % saved_game_count];
		NameTable names;
		memset(&names, 0, sizeof(names));
		names_load(&names, g->game_id);
		SavedTrack *tracks = NULL;
		int count = saved_list_tracks(g->game_id, &names, &tracks);
		names_free(&names);
		if (count <= 0) {
			saved_free_tracks(tracks);
			continue;
		}
		saved_free_tracks(pq);
		pq = tracks;
		pq_count = count;
		snprintf(pq_game, sizeof(pq_game), "%s", g->game_id);
		return play_at(SRC_SAVED, 0);
	}
	return false;
}

/* once a frame: keeps now_playing current, and carries on when a track ends */
static void update_playback(void) {
	if (!now_playing.active)
		return;
	/* the track opening on the decode thread: wait, then record how it went */
	int st = audio_state();
	if (st == AUDIO_OPENING)
		return;
	if (!play_opened) {
		play_opened = true;
		InfoCache *info = play_info();
		if (st == AUDIO_FAILED) {
			if (info)
				info_record(info, play_key, NULL);
			stop_music();
			show_error("This track's format isn't supported.");
			return;
		}
		if (info)
			info_record(info, play_key, audio_format());
		snprintf(now_playing.format, sizeof(now_playing.format), "%s", audio_describe());
	}
	if (audio_is_playing()) {
		now_playing.paused = audio_is_paused();
		now_playing.position = audio_position();
		now_playing.duration = audio_duration();
		return;
	}
	/* it played to the end */
	int src = play_src, i = playing_index(), n = playing_count();
	now_playing.active = false;
	bool next = settings.track_end == END_NEXT || settings.track_end == END_NEXT_GAME;
	if (settings.track_end == END_REPEAT && i >= 0)
		play_at(src, i);
	else if (next && i >= 0 && i + 1 < n)
		play_at(src, i + 1);
	else if (settings.track_end == END_NEXT_GAME && src == SRC_SAVED && next_game())
		;
	else
		play_src = SRC_NONE;
}

/* like a CD player: back to the start first, unless it's only just started */
static void track_prev(void) {
	if (audio_position() > 4)
		audio_seek(0);
	else
		skip(-1);
}

/* everywhere music can be playing: Z pauses, L and R change track */
static void playback_keys(void) {
	if (!now_playing.active)
		return;
	if (down & PAD_TRIGGER_Z)
		audio_set_paused(!audio_is_paused());
	if (down & PAD_TRIGGER_R) {
		sound_play(SFX_HOVER);
		skip(1);
	}
	if (down & PAD_TRIGGER_L) {
		sound_play(SFX_HOVER);
		track_prev();
	}
}

/* ----------------------------------------------------------------- rows */

static void draw_length(float right, float cy, InfoCache *info, const char *path) {
	TrackInfo ti;
	if (!info_get(info, path, &ti))
		return;
	char len[16];
	if (!ti.playable) {
		strcpy(len, "--");
	} else {
		float d = info_duration(&ti, &audio_settings);
		if (d > 0)
			ui_fmt_time(len, sizeof(len), d);
		else
			strcpy(len, "loop");
	}
	font_mid(right, cy, 13, C_SUB, ALIGN_RIGHT, 0, len);
}

static void sd_badge(float right, float cy) {
	gfx_rrect(right - 24, cy - 8, 24, 16, 8, rgba(60, 170, 120, 220));
	font_mid(right - 12, cy, 10, rgba(255, 255, 255, 255), ALIGN_CENTER, 0, "SD");
}

/* little bars moving with the music, next to the playing track */
static void playing_bars(float x, float cy, Color c) {
	float lv = 0.3f + 0.7f * ui_music_level();
	for (int i = 0; i < 3; i++) {
		float h = now_playing.paused ? 4 : 4 + 9 * lv * (0.55f + 0.45f * sinf(frame_time * (7 + i * 2.3f) + i * 1.7f));
		gfx_rect(x + i * 4, cy + 6 - h, 3, h, c);
	}
}

static void list_frame_panel(int count) {
	ui_panel(LIST_X, CONTENT_T, LIST_W, CONTENT_B - CONTENT_T);
	ui_scrollbar(LIST_X + LIST_W - 9, ROWS_Y + 2, LIST_ROWS * ROW_H - 4, scroll, LIST_ROWS, count);
}

/* ------------------------------------------------------------ disc load */

static void *disc_load_main(void *arg) {
	dvddiskid id;
	char raw_name[64], header_name[128];

	snprintf(disc_load_step, sizeof(disc_load_step), "Starting the disc...");
	dvd_reset();
	snprintf(disc_load_step, sizeof(disc_load_step), "Reading the disc...");
	if (dvd_read_id(&id) != 0) {
		disc_load_result = -1;
		return NULL;
	}
	snprintf(disc_load_step, sizeof(disc_load_step), "Reading its files...");
	if (fst_load(raw_name) < 0) {
		disc_load_result = -2;
		return NULL;
	}
	game_text_to_utf8(header_name, sizeof(header_name), (const u8 *)raw_name, sizeof(raw_name));
	char game_id[7];
	memcpy(game_id, &id, 6);
	game_id[6] = 0;
	snprintf(disc_load_step, sizeof(disc_load_step), "Finding the music...");
	tracks_load_disc(&disc_tracks, game_id, header_name);
	if (banner_load_disc(&disc_banner) && disc_banner.title[0])
		snprintf(disc_default_title, sizeof(disc_default_title), "%s", disc_banner.title);
	else
		snprintf(disc_default_title, sizeof(disc_default_title), "%s", header_name);
	if (!disc_tracks.names.title[0])
		snprintf(disc_tracks.game_name, sizeof(disc_tracks.game_name), "%s", disc_default_title);
	if (disc_banner.valid)
		gfx_tex_init(&disc_banner_tex, disc_banner.image, BANNER_W, BANNER_H, GX_TF_RGB5A3, true);
	disc_load_result = tracks_visible_count(&disc_tracks) > 0 ? 1 : -3;
	return NULL;
}

static void disc_load_start(void) {
	stop_music();
	tracks_free(&disc_tracks);
	memset(&disc_banner, 0, sizeof(disc_banner));
	disc_load_result = 0;
	disc_load_step[0] = 0;
	LWP_CreateThread(&disc_thread, disc_load_main, NULL, NULL, 32 * 1024, 70);
	go(ST_DISC_LOADING);
}

/* ---------------------------------------------------------- disc search */

static void *scan_main(void *arg) {
	tracks_scan_disc(&disc_tracks, &scan);
	info_save(&disc_tracks.info);
	disc_load_result = scan.found > 0 ? 1 : -4;
	return NULL;
}

static void scan_start(bool from_list) {
	stop_music();   /* (the search and the music would fight over the drive) */
	memset((void *)&scan, 0, sizeof(scan));
	scanning = true;
	scan_from_list = from_list;
	disc_load_result = 0;
	snprintf(disc_load_step, sizeof(disc_load_step), "Looking through the disc's files...");
	LWP_CreateThread(&disc_thread, scan_main, NULL, NULL, 64 * 1024, 70);
	go(ST_DISC_LOADING);
}

static void partial_music_answer(int choice) {
	if (choice == 0)
		scan_start(true);
}

static void no_music_answer(int choice) {
	if (choice == 0)
		scan_start(false);
	else
		dvd_motor_off();   /* nothing to play: let the disc stop, as leaving the disc list does */
}

static void disc_loading_frame(void) {
	ui_header(scanning ? "Searching the disc" : "Play from disc", NULL, NULL);
	disc_draw(320, 195, 80, 80, frame_time * 5, 1, NULL, NULL, 0.4f + 0.3f * sinf(frame_time * 4));
	if (scanning && scan.total > 0) {
		char t[96];
		snprintf(t, sizeof(t), "Trying file %d of %d", scan.done < scan.total ? scan.done + 1 : scan.total, scan.total);
		font_mid(320, 300, 16, C_SUB, ALIGN_CENTER, 0, t);
		ui_progress(200, 318, 240, 6, scan.done / (float)scan.total, C_ACCENT);
		snprintf(t, sizeof(t), scan.found == 1 ? "1 track found" : "%d tracks found", scan.found);
		font_mid(320, 342, 13, scan.found ? C_GOOD : C_DIM, ALIGN_CENTER, 0, t);
	} else {
		font_mid(320, 310, 16, C_SUB, ALIGN_CENTER, 0, disc_load_step);
	}
	if (scanning) {
		static const Hint h[] = { { "B", "Stop (keeps what's found)" } };
		ui_hints(h, 1);
		if ((down & PAD_BUTTON_B) && !scan.cancel) {
			scan.cancel = true;
			sound_play(SFX_CANCEL);
		}
	}
	if (disc_load_result == 0)
		return;
	LWP_JoinThread(disc_thread, NULL);
	disc_thread = LWP_THREAD_NULL;
	sel = scroll = 0;
	char msg[400];
	if (scanning) {
		scanning = false;
		if (disc_load_result == 1) {
			sound_play(SFX_LOADED);
			go(ST_DISC_LIST);
			snprintf(msg, sizeof(msg), scan.found == 1 ? "Found 1 track" : "Found %d tracks", scan.found);
			ui_toast(msg);
		} else if (scan_from_list) {
			go(ST_DISC_LIST);
			ui_toast("Didn't find any more music");
		} else {
			dvd_motor_off();
			go(ST_MAIN);
			show_error("Didn't find anything on this disc that plays as music.");
		}
		return;
	}
	switch (disc_load_result) {
	case 1:
		sound_play(SFX_LOADED);
		go(ST_DISC_LIST);
		/* Most of meta.c's names aren't on the disc and no search has added
		 * any: the rip meta.c was made from probably renamed its files
		 * ("02 - Menu Theme.dsp"), so the music is there under other names. */
		if (disc_tracks.meta_found * 2 < disc_tracks.meta_count && !disc_tracks.extra_found) {
			snprintf(msg, sizeof(msg), "Only %d of the %d known tracks for %s were found under their usual names. "
			         "The rest are probably on the disc under different names. Search the disc for them? (It can take a minute or two.)",
			         disc_tracks.meta_found, disc_tracks.meta_count,
			         disc_tracks.game_name[0] ? disc_tracks.game_name : "this disc");
			popup_open(partial_music_answer, "Some music is missing", msg, 2, "Search the disc", "Not now");
		}
		break;
	case -1:
		dvd_motor_off();
		go(ST_MAIN);
		show_error("Couldn't read the disc. Is an original GameCube disc in the drive?");
		break;
	case -2:
		dvd_motor_off();
		go(ST_MAIN);
		show_error("Couldn't read the disc's file system.");
		break;
	default:
		go(ST_MAIN);
		snprintf(msg, sizeof(msg), "There's no known music for %s (%s). "
		         "Do you want to search for possibly compatible tracks anyway? (It can take a minute or two.)",
		         disc_tracks.game_name[0] ? disc_tracks.game_name : "this disc", disc_tracks.game_id);
		popup_open(no_music_answer, "No known music", msg, 2, "Search the disc", "Not now");
		break;
	}
}

/* ------------------------------------------------------- renaming, hiding */

enum { KBP_NONE, KBP_DISC_TRACK, KBP_DISC_GAME, KBP_SAVED_TRACK, KBP_SAVED_GAME };
static int kb_purpose;
static char kb_key[256];     /* the track being renamed */

static void rename_track(const char *key, const char *name, bool paired, int purpose) {
	char def[96];
	tracks_default_track_name(def, sizeof(def), key, paired);
	kb_purpose = purpose;
	snprintf(kb_key, sizeof(kb_key), "%s", key);
	kb_open("Rename the track", name, def, 96);
}

/* hides a track, keeping its custom name (copied first: names_set frees the old one) */
static void hide_track(NameTable *names, const char *key) {
	const NameEntry *e = names_find(names, key);
	char name[128] = "";
	if (e && e->name)
		snprintf(name, sizeof(name), "%s", e->name);
	names_set(names, key, name[0] ? name : NULL, true);
}

static void reload_saved_tracks(void) {
	SavedTrack *old = saved_tracks;
	int old_count = saved_track_count;
	saved_tracks = NULL;
	saved_track_count = saved_list_tracks(saved_games[saved_game_sel].game_id, &saved_names, &saved_tracks);
	/* ticks stay with their tracks */
	for (int i = 0; i < saved_track_count; i++)
		for (int j = 0; j < old_count; j++)
			if (old[j].selected && !strcmp(old[j].path, saved_tracks[i].path))
				saved_tracks[i].selected = true;
	saved_free_tracks(old);
	if (sel >= saved_track_count) sel = saved_track_count ? saved_track_count - 1 : 0;
	if (scroll > sel) scroll = sel;
	if (play_src == SRC_SAVED && !strcmp(pq_game, saved_games[saved_game_sel].game_id))
		pq_take();   /* (renamed or hidden tracks: what's playing from follows) */
}

static void names_saved(bool ok) {
	if (ok) {
		ui_toast("Saved to the SD card");
	} else {
		show_error(sd_present ? "Couldn't save the names file." : "Names are kept on the SD card, and there isn't one.");
	}
}

static void keyboard_done(const char *text) {
	char name[128];
	snprintf(name, sizeof(name), "%s", text);
	/* no spaces at the ends */
	char *s = name;
	while (*s == ' ') s++;
	size_t n = strlen(s);
	while (n && s[n - 1] == ' ') s[--n] = 0;

	switch (kb_purpose) {
	case KBP_DISC_TRACK: {
		const NameEntry *e = names_find(&disc_tracks.names, kb_key);
		bool hidden = e && e->hidden;
		Track *t = NULL;
		for (int i = 0; i < disc_tracks.count; i++)
			if (!strcmp(tracks_key(&disc_tracks.tracks[i]), kb_key))
				t = &disc_tracks.tracks[i];
		char def[96];
		tracks_default_track_name(def, sizeof(def), kb_key, t && t->paired);
		names_set(&disc_tracks.names, kb_key, strcmp(s, def) ? s : NULL, hidden);
		tracks_apply_names(&disc_tracks);
		/* the list is in name order, so the track may have moved: keep the cursor on it */
		for (int i = 0; i < tracks_visible_count(&disc_tracks); i++) {
			if (!strcmp(tracks_key(tracks_visible(&disc_tracks, i)), kb_key)) {
				sel = i;
				if (sel < scroll || sel >= scroll + LIST_ROWS)
					scroll = sel > LIST_ROWS / 2 ? sel - LIST_ROWS / 2 : 0;
				break;
			}
		}
		names_saved(names_save(&disc_tracks.names));
		break;
	}
	case KBP_DISC_GAME:
		snprintf(disc_tracks.names.title, sizeof(disc_tracks.names.title), "%s", strcmp(s, disc_default_title) ? s : "");
		snprintf(disc_tracks.game_name, sizeof(disc_tracks.game_name), "%s", *s ? s : disc_default_title);
		tracks_apply_names(&disc_tracks);
		names_saved(names_save(&disc_tracks.names));
		break;
	case KBP_SAVED_TRACK: {
		const NameEntry *e = names_find(&saved_names, kb_key);
		bool hidden = e && e->hidden;
		char def[96];
		tracks_default_track_name(def, sizeof(def), kb_key, false);
		names_set(&saved_names, kb_key, *s && strcmp(s, def) ? s : NULL, hidden);
		names_saved(names_save(&saved_names));
		reload_saved_tracks();
		break;
	}
	case KBP_SAVED_GAME: {
		SavedGame *g = &saved_games[saved_games_sel];
		NameTable nt;
		memset(&nt, 0, sizeof(nt));
		names_load(&nt, g->game_id);
		snprintf(nt.title, sizeof(nt.title), "%s", s);
		names_saved(names_save(&nt));
		names_free(&nt);
		if (saved_game_sel == saved_games_sel && state == ST_SAVED_LIST)
			snprintf(saved_names.title, sizeof(saved_names.title), "%s", s);
		if (*s)
			snprintf(g->title, sizeof(g->title), "%s", s);
		break;
	}
	}
	/* the player shows the new name too */
	if (play_src != SRC_NONE && (kb_purpose == KBP_DISC_TRACK || kb_purpose == KBP_SAVED_TRACK) && !strcmp(play_key, kb_key) && *s)
		snprintf(now_playing.title, sizeof(now_playing.title), "%s", s);
	kb_purpose = KBP_NONE;
}

/* ------------------------------------------------------------ disc list */

static int disc_ticked(void) {
	int n = 0;
	for (int i = 0; i < disc_tracks.count; i++)
		if (disc_tracks.tracks[i].selected)
			n++;
	return n;
}

/* ------------------------------------------------------ export names */

static void fill_export_info(ExportTrack *e, InfoCache *info, const char *key) {
	TrackInfo ti;
	if (info_get(info, key, &ti) && ti.playable && ti.sample_rate > 0) {
		e->seconds = (float)ti.samples / ti.sample_rate;
		e->loops = ti.loop;
	}
}

static void show_exported(bool ok, const char *file) {
	if (!ok) {
		show_error(sd_present ? "Couldn't write the names file to the SD card."
		                      : "Exporting a names file needs an SD card (in an SD Gecko or SD2SP2).");
		return;
	}
	char msg[160];
	snprintf(msg, sizeof(msg), "Saved in %s, ready for renaming.", file);
	popup_open(NULL, "Names file exported", msg, 1, "OK");
	sound_play(SFX_LOADED);
}

/* every track found on the disc, hidden ones included (marked), one entry
 * per stereo pair (its left half) and no companion files */
static void export_disc_names(void) {
	ExportTrack *list = calloc(disc_tracks.count + 1, sizeof(ExportTrack));
	if (!list)
		return;
	int n = 0;
	for (int i = 0; i < disc_tracks.count; i++) {
		Track *t = &disc_tracks.tracks[i];
		if (!t->file || t->companion || t->pair_left)
			continue;
		ExportTrack *e = &list[n++];
		e->path = tracks_key(t);
		e->name = t->name;
		e->hidden = t->hidden;
		fill_export_info(e, &disc_tracks.info, e->path);
	}
	char file[64];
	show_exported(names_export(disc_tracks.game_id, disc_tracks.game_name, list, n, file, sizeof(file)), file);
	free(list);
}

/* the game's backed-up tracks, plus the ones hidden in its names file */
static void export_saved_names(void) {
	int hidden = 0;
	for (int i = 0; i < saved_names.count; i++)
		hidden += saved_names.entries[i].hidden;
	ExportTrack *list = calloc(saved_track_count + hidden + 1, sizeof(ExportTrack));
	char (*defaults)[96] = calloc(hidden + 1, 96);
	if (!list || !defaults) {
		free(list);
		free(defaults);
		return;
	}
	int n = 0, d = 0;
	for (int i = 0; i < saved_track_count; i++) {
		ExportTrack *e = &list[n++];
		e->path = saved_tracks[i].path;
		e->name = saved_tracks[i].name;
		fill_export_info(e, &saved_info, e->path);
	}
	for (int i = 0; i < saved_names.count; i++) {
		const NameEntry *ne = &saved_names.entries[i];
		if (!ne->hidden)
			continue;
		ExportTrack *e = &list[n++];
		e->path = ne->path;
		if (ne->name) {
			e->name = ne->name;
		} else {
			tracks_default_track_name(defaults[d], 96, ne->path, false);
			e->name = defaults[d++];
		}
		e->hidden = true;
		fill_export_info(e, &saved_info, e->path);
	}
	const SavedGame *g = &saved_games[saved_game_sel];
	char file[64];
	show_exported(names_export(g->game_id, g->title, list, n, file, sizeof(file)), file);
	free(list);
	free(defaults);
}

static int disc_hidden(void) {
	int n = 0;
	for (int i = 0; i < disc_tracks.count; i++)
		if (disc_tracks.tracks[i].hidden && !disc_tracks.tracks[i].companion && disc_tracks.tracks[i].file)
			n++;
	return n;
}

static void disc_options(int choice) {
	Track *cur = tracks_visible(&disc_tracks, sel);
	int count = tracks_visible_count(&disc_tracks);
	switch (choice) {
	case 0:
		if (cur)
			rename_track(tracks_key(cur), cur->name, cur->paired, KBP_DISC_TRACK);
		break;
	case 1:
		if (cur) {
			hide_track(&disc_tracks.names, tracks_key(cur));
			cur->selected = false;
			tracks_apply_names(&disc_tracks);
			names_saved(names_save(&disc_tracks.names));
			count = tracks_visible_count(&disc_tracks);
			if (sel >= count) sel = count ? count - 1 : 0;
			if (scroll > sel) scroll = sel;
		}
		break;
	case 2: {
		bool all = disc_ticked() < count;
		for (int i = 0; i < count; i++)
			tracks_visible(&disc_tracks, i)->selected = all;
		break;
	}
	case 3:
		if (disc_hidden()) {
			for (int i = 0; i < disc_tracks.names.count; i++)
				disc_tracks.names.entries[i].hidden = false;
			tracks_apply_names(&disc_tracks);
			names_saved(names_save(&disc_tracks.names));
		}
		break;
	case 4:
		kb_purpose = KBP_DISC_GAME;
		kb_key[0] = 0;
		kb_open("Rename the game", disc_tracks.game_name, disc_default_title, 128);
		break;
	case 5:
		if (backup_status.running)
			show_error("Wait for the backup to finish first.");
		else
			scan_start(true);
		break;
	case 6:
		export_disc_names();
		break;
	}
}

static void open_disc_options(void) {
	int count = tracks_visible_count(&disc_tracks);
	char unhide[48];
	int hidden = disc_hidden();
	if (hidden)
		snprintf(unhide, sizeof(unhide), "Show %d hidden track%s", hidden, hidden == 1 ? "" : "s");
	else
		snprintf(unhide, sizeof(unhide), "No hidden tracks");
	popup_open(disc_options, "Options", "Z pauses; L and R play the previous and next track.", 7,
	           "Rename this track", "Hide this track", disc_ticked() >= count ? "Untick all" : "Tick all",
	           unhide, "Rename the game", "Search the disc for more tracks", "Export a names file");
}

static void start_backup(void) {
	if (!sd_present) {
		show_error("Backing up needs an SD card (in an SD Gecko or SD2SP2).");
		return;
	}
	if (backup_start(&disc_tracks))
		sound_play(SFX_CONFIRM);
}

static void disc_list_frame(void) {
	int count = tracks_visible_count(&disc_tracks);
	int ticked = disc_ticked();
	int on_sd = 0;
	for (int i = 0; i < count; i++)
		if (tracks_visible(&disc_tracks, i)->on_sd)
			on_sd++;

	if (backup_was_running && !backup_status.running) {
		tracks_refresh_sd(&disc_tracks);
		settings_save();   /* (the backed-up statistics) */
		sound_play(backup_status.errors ? SFX_ERROR : SFX_LOADED);
	}
	backup_was_running = backup_status.running;

	char sub[200];
	int l = snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %d track%s", disc_tracks.game_id, count, count == 1 ? "" : "s");
	if (on_sd)
		snprintf(sub + l, sizeof(sub) - l, "  \xC2\xB7  %d on the SD card", on_sd);
	ui_header(disc_tracks.game_name, sub, disc_banner.valid ? &disc_banner_tex : NULL);

	list_frame_panel(count);
	int pi = play_src == SRC_DISC ? playing_index() : -1;
	for (int row = 0; row < LIST_ROWS && scroll + row < count; row++) {
		int i = scroll + row;
		Track *t = tracks_visible(&disc_tracks, i);
		float y = ROWS_Y + row * ROW_H, cy = y + ROW_H / 2;
		if (i == sel)
			ui_selection(LIST_X + 8, y + 1, LIST_W - 24, ROW_H - 2);
		ui_checkbox(LIST_X + 28, cy, t->selected);
		float right = LIST_X + LIST_W - 26;
		if (t->on_sd)
			sd_badge(right, cy);
		draw_length(right - 34, cy, &disc_tracks.info, tracks_key(t));
		Color c = i == sel ? C_TEXT : C_SUB;
		float nx = LIST_X + 46;
		if (i == pi) {
			playing_bars(nx, cy, i == sel ? C_TEXT : C_ACCENT_HI);
			nx += 18;
			c = i == sel ? C_TEXT : C_ACCENT_HI;
		}
		font_mid(nx, cy, 15, c, ALIGN_LEFT, right - 90 - nx, t->name);
		if (i == sel)
			ui_backup_origin(LIST_X + 200, cy);
	}

	Hint hints[5];
	int n = 0;
	hints[n++] = (Hint){ "A", sel == pi ? "Player" : "Play" };
	hints[n++] = (Hint){ "Y", "Tick" };
	hints[n++] = (Hint){ "X", "Options" };
	if (ticked && !backup_status.running)
		hints[n++] = (Hint){ "START", "Back up" };
	hints[n++] = (Hint){ "B", backup_status.running ? "Stop" : "Back" };
	ui_hints(hints, n);

	if (popup.open || kb_purpose)
		return;
	move_in_list(&sel, &scroll, count, LIST_ROWS);
	playback_keys();
	Track *cur = tracks_visible(&disc_tracks, sel);
	if ((down & PAD_BUTTON_A) && cur) {
		sound_play(SFX_SELECT);
		if (sel == pi) {
			player_return = ST_DISC_LIST;
			go(ST_PLAYER);
		} else {
			play_at(SRC_DISC, sel);
		}
	}
	if ((down & PAD_BUTTON_Y) && cur) {
		cur->selected = !cur->selected;
		sound_play(SFX_KEY);
	}
	if (down & PAD_BUTTON_X)
		open_disc_options();
	if ((down & PAD_BUTTON_START) && ticked && !backup_status.running)
		start_backup();
	if (down & PAD_BUTTON_B) {
		if (backup_status.running) {
			backup_status.cancel = true;   /* the first B stops the backup */
			sound_play(SFX_CANCEL);
		} else {
			stop_music();
			info_save(&disc_tracks.info);
			settings_save();
			sound_play(SFX_BACK);
			dvd_motor_off();
			go(ST_MAIN);
		}
	}
}

/* ----------------------------------------------------------------- saved */

#define GAME_ROW_H 44
#define GAME_ROWS  6

static void free_saved_games(void) {
	saved_free_games(saved_games);
	saved_games = NULL;
	free(saved_banners);
	saved_banners = NULL;
	free(saved_banner_tex);
	saved_banner_tex = NULL;
	saved_game_count = 0;
}

static void sort_saved_games(void);


static const char *const sort_names[SORT_COUNT] = { "Name A–Z", "Name Z–A", "Newest first", "Oldest first" };

static int sort_cmp(const void *a, const void *b) {
	const SavedGame *x = &saved_games[*(const int *)a], *y = &saved_games[*(const int *)b];
	int c;
	switch (settings.saved_sort) {
	case SORT_NAME_DOWN: c = strcasecmp(y->title, x->title); break;
	case SORT_NEWEST: c = (y->added > x->added) - (y->added < x->added); break;
	case SORT_OLDEST: c = (x->added > y->added) - (x->added < y->added); break;
	default: c = strcasecmp(x->title, y->title); break;
	}
	return c ? c : strcmp(x->game_id, y->game_id);
}

/* Puts the saved games (and their banners) in settings.saved_sort order;
 * the highlighted game stays highlighted. Not while the GPU may still be
 * drawing the old banners: at the start of a frame. */
static void sort_saved_games(void) {
	int n = saved_game_count;
	if (n < 2)
		return;
	char keep[7], open[7];
	snprintf(keep, sizeof(keep), "%s", saved_games[saved_games_sel].game_id);
	snprintf(open, sizeof(open), "%s", saved_games[saved_game_sel].game_id);
	int *order = malloc(n * sizeof(int));
	SavedGame *g2 = malloc(n * sizeof(SavedGame));
	Banner *b2 = saved_banners ? memalign(32, n * sizeof(Banner)) : NULL;
	if (!order || !g2 || (saved_banners && !b2)) {
		free(order);
		free(g2);
		free(b2);
		return;
	}
	for (int i = 0; i < n; i++)
		order[i] = i;
	qsort(order, n, sizeof(int), sort_cmp);
	for (int i = 0; i < n; i++) {
		g2[i] = saved_games[order[i]];
		if (b2)
			b2[i] = saved_banners[order[i]];
	}
	free(order);
	saved_free_games(saved_games);
	saved_games = g2;
	if (b2) {
		DCFlushRange(b2, n * sizeof(Banner));
		free(saved_banners);
		saved_banners = b2;
		for (int i = 0; i < n; i++)
			if (b2[i].valid)
				gfx_tex_init(&saved_banner_tex[i], b2[i].image, BANNER_W, BANNER_H, GX_TF_RGB5A3, true);
	}
	int k = saved_game_index(keep), o = saved_game_index(open);
	saved_games_sel = k < 0 ? 0 : k;
	saved_game_sel = o < 0 ? 0 : o;
	if (saved_games_sel < saved_games_scroll)
		saved_games_scroll = saved_games_sel;
	if (saved_games_sel >= saved_games_scroll + GAME_ROWS)
		saved_games_scroll = saved_games_sel - GAME_ROWS + 1;
	/* the playing game's banner moved */
	int pg = play_src == SRC_SAVED ? saved_game_index(pq_game) : -1;
	if (pg >= 0 && saved_banner_for(pg))
		now_playing.banner = *saved_banner_for(pg);
}

/* (Re)reads the saved games and their banners, in the chosen order, with
 * keep_id highlighted if it's still there. False if there are none. */
static bool load_saved_games(const char *keep_id) {
	int old_sel = saved_games_sel;
	free_saved_games();
	saved_game_count = saved_list_games(&saved_games);
	if (!saved_game_count)
		return false;
	saved_banners = memalign(32, saved_game_count * sizeof(Banner));
	saved_banner_tex = calloc(saved_game_count, sizeof(GXTexObj));
	if (saved_banners && saved_banner_tex) {
		for (int i = 0; i < saved_game_count; i++) {
			char path[256];
			backup_path(path, sizeof(path), saved_games[i].game_id, BANNER_FILE);
			if (banner_load_file(&saved_banners[i], path))
				gfx_tex_init(&saved_banner_tex[i], saved_banners[i].image, BANNER_W, BANNER_H, GX_TF_RGB5A3, true);
		}
	} else {
		free(saved_banners);
		saved_banners = NULL;
	}
	saved_games_sel = saved_games_scroll = saved_game_sel = 0;
	sort_saved_games();
	int k = keep_id ? saved_game_index(keep_id) : -1;
	if (k >= 0)
		saved_games_sel = k;
	else if (keep_id)
		saved_games_sel = old_sel < saved_game_count ? old_sel : saved_game_count - 1;
	saved_games_scroll = saved_games_sel >= GAME_ROWS ? saved_games_sel - GAME_ROWS + 1 : 0;
	/* the playing game's banner may have moved */
	int pg = play_src == SRC_SAVED ? saved_game_index(pq_game) : -1;
	if (pg >= 0 && saved_banner_for(pg))
		now_playing.banner = *saved_banner_for(pg);
	return true;
}

static void saved_open(void) {
	if (!sd_present) {
		show_error("Saved tracks are on the SD card, and there isn't one.");
		return;
	}
	if (!load_saved_games(NULL)) {
		show_error("No saved tracks yet. Back some up from a disc first.");
		return;
	}
	sound_play(SFX_SELECT);
	go(ST_SAVED_GAMES);
}

/* ------------------------------------------------------------- deleting */

static char delete_game_id[7];

/* nothing can be playing from what's about to go */
static void stop_if_playing_from(const char *game_id) {
	if (play_src == SRC_SAVED && !strcmp(pq_game, game_id)) {
		stop_music();
		pq_clear();
	}
}

static void delete_game_confirmed(int choice) {
	if (choice != 1)
		return;
	stop_if_playing_from(delete_game_id);
	long long freed = saved_delete_game(delete_game_id);
	ui_sd_changed(-freed);
	sound_play(SFX_BACK);
	if (load_saved_games(delete_game_id)) {
		ui_toast("Deleted from the SD card");
	} else {
		go(ST_MAIN);
		ui_toast("Deleted. No saved tracks left.");
	}
}

static void ask_delete_game(int g) {
	char note[300];
	snprintf(delete_game_id, sizeof(delete_game_id), "%s", saved_games[g].game_id);
	snprintf(note, sizeof(note), "Delete every saved track of %s from the SD card? This can't be undone.", saved_games[g].title);
	popup_confirm(delete_game_confirmed, "Delete this game?", note, "Delete the game");
}

static void saved_games_options(int choice) {
	if (choice == 0) {
		kb_purpose = KBP_SAVED_GAME;
		kb_open("Rename the game", saved_games[saved_games_sel].title, "", 128);
	} else if (choice == 1) {
		ask_delete_game(saved_games_sel);
	}
}

static void saved_games_frame(void) {
	if (resort_pending) {
		resort_pending = false;
		sort_saved_games();
	}
	char sub[96];
	snprintf(sub, sizeof(sub), "%d game%s on the SD card  ·  %s", saved_game_count, saved_game_count == 1 ? "" : "s",
	         sort_names[settings.saved_sort]);
	ui_header("Saved tracks", sub, NULL);
	ui_panel(LIST_X, CONTENT_T, LIST_W, CONTENT_B - CONTENT_T);
	ui_scrollbar(LIST_X + LIST_W - 9, ROWS_Y + 2, GAME_ROWS * GAME_ROW_H - 4, saved_games_scroll, GAME_ROWS, saved_game_count);
	int playing = play_src == SRC_SAVED ? saved_game_index(pq_game) : -1;
	for (int row = 0; row < GAME_ROWS && saved_games_scroll + row < saved_game_count; row++) {
		int i = saved_games_scroll + row;
		float y = ROWS_Y + row * GAME_ROW_H, cy = y + GAME_ROW_H / 2;
		if (i == saved_games_sel)
			ui_selection(LIST_X + 8, y + 2, LIST_W - 24, GAME_ROW_H - 4);
		GXTexObj *b = saved_banner_for(i);
		if (b)
			gfx_image(b, LIST_X + 20, cy - 16, 96, 32, rgba(255, 255, 255, 255));
		else
			ui_icon(ICON_DISC, LIST_X + 68, cy, 26, C_DIM);
		float tx = LIST_X + 132;
		if (i == playing) {
			playing_bars(tx, cy - 7, i == saved_games_sel ? C_TEXT : C_ACCENT_HI);
			tx += 18;
		}
		Color tc = i == saved_games_sel ? C_TEXT : i == playing ? C_ACCENT_HI : C_SUB;
		font_mid(tx, cy - 7, 15, tc, ALIGN_LEFT, LIST_X + LIST_W - 38 - tx, saved_games[i].title);
		char line[64];
		int l = snprintf(line, sizeof(line), "%s", saved_games[i].game_id);
		if (saved_games[i].added > 0) {
			time_t t = (time_t)saved_games[i].added;
			struct tm *tm = gmtime(&t);
			if (tm)
				snprintf(line + l, sizeof(line) - l, "  ·  added %04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
		}
		font_mid(LIST_X + 132, cy + 10, 11, C_DIM, ALIGN_LEFT, 0, line);
	}
	Hint h[5] = { { "A", "Open" }, { "X", "Manage" }, { "Y", "Sort" }, { "B", "Back" } };
	int nh = 4;
	if (now_playing.active)
		h[nh++] = (Hint){ "START", "Player" };
	ui_hints(h, nh);

	if (popup.open || kb_purpose)
		return;
	move_in_list(&saved_games_sel, &saved_games_scroll, saved_game_count, GAME_ROWS);
	playback_keys();
	if (down & PAD_BUTTON_X)
		popup_open(saved_games_options, "Manage", NULL, 2, "Rename the game", "Delete this game");
	if (down & PAD_BUTTON_Y) {
		settings.saved_sort = (settings.saved_sort + 1) % SORT_COUNT;
		resort_pending = true;
		ui_toast(sort_names[settings.saved_sort]);
		sound_play(SFX_KEY);
	}
	if ((down & PAD_BUTTON_START) && now_playing.active) {
		sound_play(SFX_SELECT);
		player_return = ST_SAVED_GAMES;
		go(ST_PLAYER);
	}
	if (down & PAD_BUTTON_A) {
		saved_game_sel = saved_games_sel;
		const char *id = saved_games[saved_game_sel].game_id;
		names_free(&saved_names);
		info_free(&saved_info);
		names_load(&saved_names, id);
		info_load(&saved_info, id);
		snprintf(saved_info_game, sizeof(saved_info_game), "%s", id);
		saved_free_tracks(saved_tracks);
		saved_tracks = NULL;
		saved_track_count = saved_list_tracks(id, &saved_names, &saved_tracks);
		sel = scroll = 0;
		/* opening the game that's playing: start at its track */
		if (play_src == SRC_SAVED && !strcmp(pq_game, id)) {
			for (int i = 0; i < saved_track_count; i++)
				if (!strcmp(saved_tracks[i].path, play_key)) {
					sel = i;
					scroll = i >= LIST_ROWS ? i - LIST_ROWS + 1 : 0;
				}
		}
		sound_play(SFX_SELECT);
		go(ST_SAVED_LIST);
	}
	if (down & PAD_BUTTON_B) {
		stop_music();
		pq_clear();
		settings_save();
		free_saved_games();
		sound_play(SFX_BACK);
		go(ST_MAIN);
	}
}

static int saved_ticked(void) {
	int n = 0;
	for (int i = 0; i < saved_track_count; i++)
		if (saved_tracks[i].selected)
			n++;
	return n;
}

static char delete_key[256];   /* the one track being deleted ("" = the ticked ones) */

/* after deleting from the open game: the list again, or the games list if
 * nothing's left of it (then the game's folder goes too) */
static void after_delete(long long freed, int deleted) {
	SavedGame *g = &saved_games[saved_game_sel];
	names_save(&saved_names);
	sound_play(SFX_BACK);
	char msg[64];
	if (!saved_has_tracks(g->game_id)) {
		char id[7];
		snprintf(id, sizeof(id), "%s", g->game_id);
		freed += saved_delete_game(id);
		ui_sd_changed(-freed);
		info_save(&saved_info);
		names_free(&saved_names);
		info_free(&saved_info);
		saved_info_game[0] = 0;
		if (load_saved_games(id)) {
			go(ST_SAVED_GAMES);
			ui_toast("Deleted. That was the last of the game's tracks.");
		} else {
			go(ST_MAIN);
			ui_toast("Deleted. No saved tracks left.");
		}
		return;
	}
	ui_sd_changed(-freed);
	reload_saved_tracks();
	snprintf(msg, sizeof(msg), deleted == 1 ? "Deleted 1 track" : "Deleted %d tracks", deleted);
	ui_toast(msg);
}

static void delete_tracks_confirmed(int choice) {
	if (choice != 1)
		return;
	SavedGame *g = &saved_games[saved_game_sel];
	stop_if_playing_from(g->game_id);
	long long freed = 0;
	int deleted = 0;
	if (delete_key[0]) {
		freed += saved_delete_track(g->game_id, &saved_names, delete_key);
		deleted = 1;
	} else {
		for (int i = 0; i < saved_track_count; i++)
			if (saved_tracks[i].selected) {
				freed += saved_delete_track(g->game_id, &saved_names, saved_tracks[i].path);
				deleted++;
			}
	}
	after_delete(freed, deleted);
}

enum { SO_RENAME, SO_HIDE, SO_SHOW_HIDDEN, SO_TICK_ALL, SO_DELETE, SO_DELETE_TICKED, SO_RENAME_GAME, SO_EXPORT };
static int saved_option_ids[8];

static void saved_options(int choice) {
	if (choice < 0 || choice >= 8)
		return;
	SavedTrack *t = saved_track_count ? &saved_tracks[sel] : NULL;
	char note[300];
	switch (saved_option_ids[choice]) {
	case SO_RENAME:
		if (t)
			rename_track(t->path, t->name, false, KBP_SAVED_TRACK);
		break;
	case SO_HIDE:
		if (t) {
			hide_track(&saved_names, t->path);
			names_saved(names_save(&saved_names));
			reload_saved_tracks();
		}
		break;
	case SO_SHOW_HIDDEN: {
		int hidden = 0;
		for (int i = 0; i < saved_names.count; i++)
			if (saved_names.entries[i].hidden) {
				saved_names.entries[i].hidden = false;
				hidden++;
			}
		if (hidden) {
			names_saved(names_save(&saved_names));
			reload_saved_tracks();
		}
		break;
	}
	case SO_TICK_ALL: {
		bool all = saved_ticked() < saved_track_count;
		for (int i = 0; i < saved_track_count; i++)
			saved_tracks[i].selected = all;
		break;
	}
	case SO_DELETE:
		if (t) {
			snprintf(delete_key, sizeof(delete_key), "%s", t->path);
			snprintf(note, sizeof(note), "Delete %s from the SD card? This can't be undone.", t->name);
			popup_confirm(delete_tracks_confirmed, "Delete this track?", note, "Delete");
		}
		break;
	case SO_DELETE_TICKED: {
		int n = saved_ticked();
		delete_key[0] = 0;
		snprintf(note, sizeof(note), "Delete the %d ticked track%s from the SD card? This can't be undone.", n, n == 1 ? "" : "s");
		popup_confirm(delete_tracks_confirmed, n == 1 ? "Delete 1 track?" : "Delete the ticked tracks?", note, "Delete");
		break;
	}
	case SO_RENAME_GAME:
		saved_games_sel = saved_game_sel;
		kb_purpose = KBP_SAVED_GAME;
		kb_open("Rename the game", saved_games[saved_game_sel].title, "", 128);
		break;
	case SO_EXPORT:
		export_saved_names();
		break;
	}
}

static void open_saved_options(void) {
	int n = 0, ticked = saved_ticked();
	popup_open(saved_options, "Options", "Z pauses; L and R play the previous and next track.", 0);
	char row[48];
#define OPT(id, text) do { saved_option_ids[n++] = (id); popup_add(text); } while (0)
	OPT(SO_RENAME, "Rename this track");
	OPT(SO_HIDE, "Hide this track");
	OPT(SO_SHOW_HIDDEN, "Show hidden tracks");
	OPT(SO_TICK_ALL, ticked >= saved_track_count && saved_track_count ? "Untick all" : "Tick all");
	OPT(SO_RENAME_GAME, "Rename the game");
	OPT(SO_EXPORT, "Export a names file");
	OPT(SO_DELETE, "Delete this track");
	if (ticked) {
		snprintf(row, sizeof(row), ticked == 1 ? "Delete 1 ticked track" : "Delete %d ticked tracks", ticked);
		OPT(SO_DELETE_TICKED, row);
	}
#undef OPT
}

static void saved_list_frame(void) {
	SavedGame *g = &saved_games[saved_game_sel];
	char sub[64];
	snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %d track%s", g->game_id, saved_track_count, saved_track_count == 1 ? "" : "s");
	ui_header(g->title, sub, saved_banner_for(saved_game_sel));
	list_frame_panel(saved_track_count);
	int pi = play_src == SRC_SAVED && !strcmp(pq_game, g->game_id) ? playing_index() : -1;
	for (int row = 0; row < LIST_ROWS && scroll + row < saved_track_count; row++) {
		int i = scroll + row;
		float y = ROWS_Y + row * ROW_H, cy = y + ROW_H / 2;
		if (i == sel)
			ui_selection(LIST_X + 8, y + 1, LIST_W - 24, ROW_H - 2);
		float right = LIST_X + LIST_W - 26;
		ui_checkbox(LIST_X + 28, cy, saved_tracks[i].selected);
		draw_length(right, cy, &saved_info, saved_tracks[i].path);
		Color c = i == sel ? C_TEXT : C_SUB;
		float nx = LIST_X + 46;
		if (i == pi) {
			playing_bars(nx, cy, i == sel ? C_TEXT : C_ACCENT_HI);
			nx += 18;
			c = i == sel ? C_TEXT : C_ACCENT_HI;
		}
		font_mid(nx, cy, 15, c, ALIGN_LEFT, right - 60 - nx, saved_tracks[i].name);
	}
	Hint hints[] = { { "A", sel == pi ? "Player" : "Play" }, { "Y", "Tick" }, { "X", "Options" }, { "Z", "Pause" }, { "B", "Back" } };
	ui_hints(hints, 5);

	if (popup.open || kb_purpose)
		return;
	move_in_list(&sel, &scroll, saved_track_count, LIST_ROWS);
	playback_keys();
	if ((down & PAD_BUTTON_A) && saved_track_count) {
		sound_play(SFX_SELECT);
		if (sel == pi) {
			player_return = ST_SAVED_LIST;
			go(ST_PLAYER);
		} else {
			pq_take();
			play_at(SRC_SAVED, sel);
		}
	}
	if ((down & PAD_BUTTON_Y) && saved_track_count) {
		saved_tracks[sel].selected = !saved_tracks[sel].selected;
		sound_play(SFX_KEY);
	}
	if (down & PAD_BUTTON_X)
		open_saved_options();
	if (down & PAD_BUTTON_B) {
		/* (the music carries on: B on the games list stops it) */
		info_save(&saved_info);
		settings_save();
		names_free(&saved_names);
		info_free(&saved_info);
		saved_info_game[0] = 0;
		sound_play(SFX_BACK);
		go(ST_SAVED_GAMES);
	}
}

/* ---------------------------------------------------------------- player */

static float volume_shown_at = -10;

static void player_frame(void) {
	if (!now_playing.active) {
		go(player_return);   /* (it stopped) */
		return;
	}
	float lv = ui_music_level();
	disc_draw(196, 222, 128, 128, ui_disc_angle(), 1, now_playing.has_banner ? &now_playing.banner : NULL,
	          now_playing.game, 0.25f + 0.75f * lv);

	/* what it is */
	float x = 350, w = SAFE_R - x;
	font_mid(x, 112, 12, C_ACCENT_HI, ALIGN_LEFT, w, "NOW PLAYING");
	int lines = font_wrap(x, 145, 22, 27, C_TEXT, ALIGN_LEFT, w, 3, now_playing.title);
	float y = 145 + lines * 27 - 6;
	y += font_wrap(x, y, 14, 18, C_SUB, ALIGN_LEFT, w, 2, now_playing.game) * 18 + 4;
	font_draw(x, y, 12, C_DIM, ALIGN_LEFT, w, now_playing.format);

	/* where it's up to */
	char pos[16], dur[16];
	ui_fmt_time(pos, sizeof(pos), now_playing.position);
	float k = 0;
	if (now_playing.duration > 0) {
		ui_fmt_time(dur, sizeof(dur), now_playing.duration);
		k = now_playing.position / now_playing.duration;
	} else {
		strcpy(dur, "loops");
	}
	ui_progress(x, 286, w, 6, k, C_ACCENT);
	font_mid(x, 304, 12, C_SUB, ALIGN_LEFT, 0, pos);
	font_mid(x + w, 304, 12, C_SUB, ALIGN_RIGHT, 0, dur);

	/* (no on-screen buttons: they looked clickable but aren't; the hint bar
	 * says what the controller does). Paused is shown as a status. */
	if (now_playing.paused) {
		ui_icon(ICON_PAUSE, x + 6, 336, 12, C_WARN);
		font_mid(x + 18, 336, 13, C_WARN, ALIGN_LEFT, 0, "Paused");
	}

	/* the volume, for a moment after it changes */
	float vt = frame_time - volume_shown_at;
	if (vt < 1.5f) {
		float old = gfx_fade;
		gfx_fade *= vt > 1.1f ? (1.5f - vt) / 0.4f : 1;
		char v[32];
		snprintf(v, sizeof(v), "Volume %d", settings.music_volume);
		font_mid(x, 380, 12, C_SUB, ALIGN_LEFT, 0, v);
		ui_progress(x + 76, 377, w - 76, 6, settings.music_volume / (float)VOLUME_MAX, C_ACCENT_HI);
		gfx_fade = old;
	}

	static const Hint h[] = { { "A", "Pause" }, { "L/R", "Track" }, { "LR", "Seek" }, { "UD", "Volume" }, { "B", "Back" } };
	ui_hints(h, 5);

	if (popup.open)
		return;
	if (down & (PAD_BUTTON_A | PAD_TRIGGER_Z)) {
		audio_set_paused(!audio_is_paused());
		sound_play(SFX_SELECT);
	}
	if (down & PAD_TRIGGER_R) {
		sound_play(SFX_HOVER);
		skip(1);
	}
	if (down & PAD_TRIGGER_L) {
		sound_play(SFX_HOVER);
		track_prev();
	}
	/* (dirs repeats while held, so holding seeks on) */
	if (dirs & PAD_BUTTON_RIGHT)
		audio_seek(audio_position() + 10.0f);
	if (dirs & PAD_BUTTON_LEFT)
		audio_seek(audio_position() - 10.0f);
	if (dirs & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
		int v = settings.music_volume + ((dirs & PAD_BUTTON_UP) ? 1 : -1);
		if (v >= 0 && v <= VOLUME_MAX) {
			settings.music_volume = v;
			audio_set_volume(v / (float)VOLUME_MAX);
		}
		volume_shown_at = frame_time;
	}
	if (down & PAD_BUTTON_B) {
		sound_play(SFX_BACK);
		/* the list's cursor on what's playing */
		int i = playing_index();
		if (i >= 0) {
			sel = i;
			if (sel < scroll || sel >= scroll + LIST_ROWS)
				scroll = sel > LIST_ROWS / 2 ? sel - LIST_ROWS / 2 : 0;
		}
		go(player_return);
	}
}

/* ------------------------------------------------------------- settings */

enum {
	SET_MUSIC_VOL, SET_MENU_VOL, SET_LOOPS, SET_FADE, SET_LOOP_FOREVER, SET_TRACK_END,
	SET_RESOLUTION, SET_WIDESCREEN, SET_WIDTH, SET_OVERSCAN, SET_INTRO, SET_LAUNCHER, SET_COUNT
};
static const char *const set_labels[SET_COUNT] = {
	"Music volume", "Menu sounds", "Loops before the fade", "Fade out", "Loop forever", "When a track ends",
	"Resolution", "Widescreen TV (16:9)", "Picture width", "Overscan", "Intro", "Exit starts",
};
#define SET_ROWS 5
#define SET_ROW_H 33

static char launchers[32][64];
static int launcher_count;

static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

/* the .dol files at the root of the SD card */
static void list_launchers(void) {
	launcher_count = 0;
	DIR *d = sd_present ? opendir("/") : NULL;
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d)) && launcher_count < 32) {
		size_t l = strlen(e->d_name);
		if (e->d_name[0] == '.' || l < 5 || l >= 64 || strcasecmp(e->d_name + l - 4, ".dol") || e->d_type == DT_DIR)
			continue;
		snprintf(launchers[launcher_count++], 64, "%s", e->d_name);
	}
	closedir(d);
	qsort(launchers, launcher_count, 64, cmp_name);
}

/* what Exit starts: the one chosen, else autoexec.dol, boot.dol, ipl.dol or a swiss*.dol ("" if none) */
static const char *launcher_current(void) {
	static const char *const usual[] = { "autoexec.dol", "boot.dol", "ipl.dol" };
	if (settings.launcher[0])
		for (int i = 0; i < launcher_count; i++)
			if (!strcasecmp(launchers[i], settings.launcher))
				return launchers[i];
	for (unsigned u = 0; u < sizeof(usual) / sizeof(usual[0]); u++)
		for (int i = 0; i < launcher_count; i++)
			if (!strcasecmp(launchers[i], usual[u]))
				return launchers[i];
	for (int i = 0; i < launcher_count; i++)
		if (!strncasecmp(launchers[i], "swiss", 5))
			return launchers[i];
	return "";
}

static void setting_value(int i, char *out, size_t size, float *bar) {
	static const char *const ends[END_COUNT] = { "Stop", "Play the next", "Play it again", "Next, then next game" };
	static const char *const res[RES_COUNT] = { "Auto", "480i", "480p" };
	*bar = -1;
	switch (i) {
	case SET_MUSIC_VOL:
		snprintf(out, size, "%d", settings.music_volume);
		*bar = settings.music_volume / (float)VOLUME_MAX;
		break;
	case SET_MENU_VOL:
		if (settings.menu_volume) snprintf(out, size, "%d", settings.menu_volume);
		else snprintf(out, size, "Off");
		*bar = settings.menu_volume / (float)VOLUME_MAX;
		break;
	case SET_LOOPS: snprintf(out, size, "%d", settings.loop_count); break;
	case SET_FADE: snprintf(out, size, "%d seconds", settings.fade_seconds); break;
	case SET_LOOP_FOREVER: snprintf(out, size, "%s", settings.loop_forever ? "On" : "Off"); break;
	case SET_TRACK_END: snprintf(out, size, "%s", ends[settings.track_end]); break;
	case SET_RESOLUTION:
		snprintf(out, size, "%s%s", res[settings.resolution],
		         settings.resolution == RES_480P && !VIDEO_HaveComponentCable() ? " (needs component)" : "");
		break;
	case SET_WIDESCREEN: snprintf(out, size, "%s", settings.widescreen ? "On" : "Off"); break;
	case SET_WIDTH: snprintf(out, size, "%s", settings.full_width ? "Full (704)" : "Standard (640)"); break;
	case SET_OVERSCAN: snprintf(out, size, "%d%%", settings.overscan); break;
	case SET_INTRO: snprintf(out, size, "%s", settings.intro ? "On" : "Off"); break;
	case SET_LAUNCHER: {
		const char *cur = launcher_current();
		if (settings.launcher[0] && !strcasecmp(cur, settings.launcher))
			snprintf(out, size, "%s", cur);
		else if (cur[0])
			snprintf(out, size, "Auto (%s)", cur);
		else
			snprintf(out, size, "Restart");
		break;
	}
	}
}

static int wrapi(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void setting_change(int i, int step) {
	switch (i) {
	case SET_MUSIC_VOL:
		settings.music_volume = clampi(settings.music_volume + step, 0, VOLUME_MAX);
		break;
	case SET_MENU_VOL: settings.menu_volume = clampi(settings.menu_volume + step, 0, VOLUME_MAX); break;
	case SET_LOOPS: settings.loop_count = clampi(settings.loop_count + step, 1, 9); break;
	case SET_FADE: settings.fade_seconds = clampi(settings.fade_seconds + step, 0, 20); break;
	case SET_LOOP_FOREVER: settings.loop_forever = !settings.loop_forever; break;
	case SET_TRACK_END: settings.track_end = wrapi(settings.track_end + step, 0, END_COUNT - 1); break;
	case SET_RESOLUTION:
		settings.resolution = wrapi(settings.resolution + step, 0, RES_COUNT - 1);
		video_settings_changed();
		break;
	case SET_WIDESCREEN: settings.widescreen = !settings.widescreen; break;
	case SET_WIDTH: settings.full_width = !settings.full_width; video_settings_changed(); break;
	case SET_OVERSCAN: settings.overscan = clampi(settings.overscan + step, 0, OVERSCAN_MAX); break;
	case SET_INTRO: settings.intro = !settings.intro; break;
	case SET_LAUNCHER: {
		/* Auto, then each .dol */
		int cur = -1;
		for (int k = 0; k < launcher_count; k++)
			if (settings.launcher[0] && !strcasecmp(launchers[k], settings.launcher))
				cur = k;
		cur = wrapi(cur + step, -1, launcher_count - 1);
		snprintf(settings.launcher, sizeof(settings.launcher), "%s", cur < 0 ? "" : launchers[cur]);
		break;
	}
	}
	settings_apply_audio();
}

static void fmt_hours(char *out, size_t size, double s) {
	int m = (int)(s / 60);
	snprintf(out, size, "%d:%02d", m / 60, m % 60);
}

static void stats_column(float x, const char *title, const Stats *st) {
	char t[32], b[32];
	fmt_hours(t, sizeof(t), st->seconds_played);
	ui_fmt_size(b, sizeof(b), st->bytes_backed_up);
	font_mid(x, CONTENT_T + 20, 11, C_ACCENT_HI, ALIGN_LEFT, 0, title);
	font_mid(x, CONTENT_T + 42, 13, C_SUB, ALIGN_LEFT, 0, "Time played");
	font_mid(x + 236, CONTENT_T + 42, 15, C_TEXT, ALIGN_RIGHT, 0, t);
	font_mid(x, CONTENT_T + 62, 13, C_SUB, ALIGN_LEFT, 0, "Amount backed up");
	font_mid(x + 236, CONTENT_T + 62, 15, C_TEXT, ALIGN_RIGHT, 0, b);
}

static void settings_open(void) {
	list_launchers();
	settings_sel = settings_scroll = 0;
	sound_play(SFX_SELECT);
	go(ST_SETTINGS);
}

static void settings_frame(void) {
	ui_header("Settings", NULL, NULL);
	/* the statistics first */
	ui_panel(LIST_X, CONTENT_T, LIST_W, 80);
	ui_icon(ICON_STATS, LIST_X + 24, CONTENT_T + 20, 14, C_ACCENT_HI);
	stats_column(LIST_X + 44, "THIS SESSION", &stats_session);
	stats_column(LIST_X + 310, "ALL TIME", &stats_all);
	gfx_rect(LIST_X + 296, CONTENT_T + 14, 1, 56, rgba(140, 125, 255, 60));

	float py = CONTENT_T + 90, ph = CONTENT_B - py;
	ui_panel(LIST_X, py, LIST_W, ph);
	ui_scrollbar(LIST_X + LIST_W - 9, py + 10, SET_ROWS * SET_ROW_H - 4, settings_scroll, SET_ROWS, SET_COUNT);
	for (int row = 0; row < SET_ROWS && settings_scroll + row < SET_COUNT; row++) {
		int i = settings_scroll + row;
		float y = py + 6 + row * SET_ROW_H, cy = y + SET_ROW_H / 2;
		bool s = i == settings_sel;
		if (s)
			ui_selection(LIST_X + 8, y + 1, LIST_W - 24, SET_ROW_H - 2);
		font_mid(LIST_X + 24, cy, 15, s ? C_TEXT : C_SUB, ALIGN_LEFT, 250, set_labels[i]);
		char v[80];
		float bar;
		setting_value(i, v, sizeof(v), &bar);
		float right = LIST_X + LIST_W - 36;
		if (bar >= 0) {
			ui_progress(right - 150, cy - 3, 120, 6, bar, s ? C_ACCENT_HI : C_ACCENT);
			font_mid(right, cy, 14, s ? C_TEXT : C_SUB, ALIGN_RIGHT, 0, v);
		} else {
			font_mid(right, cy, 14, s ? C_TEXT : C_SUB, ALIGN_RIGHT, 240, v);
		}
		if (s) {
			float vw = bar >= 0 ? 150 : font_width(14, v);
			if (vw > 240) vw = 240;
			ui_icon(ICON_PLAY, right + 12, cy, 8, C_TEXT);
			icon_mirrored(ICON_PLAY, right - vw - 12, cy, 8, C_TEXT);
		}
	}
	static const Hint h[] = { { "LR", "Change" }, { "UD", "Choose" }, { "B", "Save and go back" } };
	ui_hints(h, 3);

	if (popup.open)
		return;
	u16 lr = dirs & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
	dirs &= PAD_BUTTON_UP | PAD_BUTTON_DOWN;
	move_in_list(&settings_sel, &settings_scroll, SET_COUNT, SET_ROWS);
	if (lr & PAD_BUTTON_RIGHT) { setting_change(settings_sel, 1); sound_play(SFX_HOVER); }
	if (lr & PAD_BUTTON_LEFT) { setting_change(settings_sel, -1); sound_play(SFX_HOVER); }
	if (down & PAD_BUTTON_A) { setting_change(settings_sel, 1); sound_play(SFX_SELECT); }
	if (down & PAD_BUTTON_B) {
		if (sd_present && !settings_save())
			ui_toast("Couldn't save the settings");
		sound_play(SFX_BACK);
		go(ST_MAIN);
	}
}

/* -------------------------------------------------------------- credits */

static void credits_frame(void) {
	ui_header("Credits", NULL, NULL);
	ui_panel(LIST_X, CONTENT_T, LIST_W, CONTENT_B - CONTENT_T);
	static const char *const lines[][2] = {
		{ "CubeMusic", "emu_kidid" },
		{ "Playback", "vgmstream; libansnd" },
		{ "Text", "FreeType; M PLUS Rounded 1c (SIL Open Font License)" },
		{ "Menu sounds", "from GCMM-exFAT / Flashcube" },
		{ "Built with", "devkitPPC, libogc2, libfat, jansson" },
	};
	ui_wordmark(LIST_X + 24, CONTENT_T + 32, 26);
	char ver[64];
	snprintf(ver, sizeof(ver), "Version %s (%s)", GITVERSION, GITREVISION);
	font_mid(LIST_X + LIST_W - 24, CONTENT_T + 32, 12, C_DIM, ALIGN_RIGHT, 0, ver);
	for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
		float y = CONTENT_T + 76 + i * 36;
		font_mid(LIST_X + 24, y, 13, C_ACCENT_HI, ALIGN_LEFT, 0, lines[i][0]);
		font_mid(LIST_X + 150, y, 15, C_TEXT, ALIGN_LEFT, LIST_W - 180, lines[i][1]);
	}
	font_mid(320, CONTENT_B - 22, 12, C_DIM, ALIGN_CENTER, LIST_W - 40, "Plays and backs up the music on discs you own.");
	static const Hint h[] = { { "B", "Back" } };
	ui_hints(h, 1);
	if (!popup.open && (down & (PAD_BUTTON_A | PAD_BUTTON_B))) {
		sound_play(SFX_BACK);
		go(ST_MAIN);
	}
}

/* ------------------------------------------------------------------ exit */

static bool loader_stub(void) {
	return *(volatile u32 *)0x80001804 == 0x53545542 && *(volatile u32 *)0x80001808 == 0x48415858;   /* "STUBHAXX" */
}

/* dolload.c: just before it jumps to the new program */
void dol_before_jump(void) {
	dvd_flippy_restore();
	sound_shutdown();
	audio_deinit();
	video_shutdown();
}

static void exit_chosen(int choice) {
	if (choice != 0)
		return;
	const char *name = launcher_current();
	stop_music();
	settings_save();
	if (name[0]) {
		char path[80], err[200];
		snprintf(path, sizeof(path), "/%s", name);
		dol_launch(path, err, sizeof(err));
		show_error(err);   /* (only gets here if it couldn't start it) */
		return;
	}
	dol_before_jump();
	if (loader_stub()) {
		void (*reload)(void) = (void (*)(void))0x80001800;
		reload();
	}
	SYS_ResetSystem(SYS_HOTRESET, 0, 0);
}

static void exit_open(void) {
	list_launchers();
	const char *name = launcher_current();
	char row[96];
	if (name[0])
		snprintf(row, sizeof(row), "Start %s", name);
	else
		snprintf(row, sizeof(row), "%s", loader_stub() ? "Go back to the loader" : "Restart the GameCube");
	popup_open(exit_chosen, "Exit CubeMusic?", NULL, 2, row, "Cancel");
}

/* ------------------------------------------------------------------ main */

enum { MAIN_DISC, MAIN_SAVED, MAIN_SETTINGS, MAIN_CREDITS, MAIN_EXIT, MAIN_COUNT };
static const char *const main_items[MAIN_COUNT] = { "Play from disc", "Saved tracks", "Settings", "Credits", "Exit" };
static const char *const main_notes[MAIN_COUNT] = {
	"Play the music on the GameCube disc in the drive, and back it up to the SD card.",
	"Play music you've backed up to the SD card. No disc needed.",
	"Display, sound and playback, and how much you've listened to.",
	"Who made this, and what it's made with.",
	"Leave CubeMusic.",
};
static const int main_icons[MAIN_COUNT] = { ICON_DISC, ICON_FOLDER, ICON_GEAR, ICON_INFO, ICON_EXIT };

static void main_frame(void) {
	ui_wordmark(SAFE_L + 4, 46, 30);
	font_mid(SAFE_R, 48, 13, C_DIM, ALIGN_RIGHT, 0, "Music from your GameCube discs");
	gfx_rect_hgrad(SAFE_L, 78, (SAFE_R - SAFE_L) / 2.0f, 1, rgba(140, 125, 255, 0), rgba(140, 125, 255, 90));
	gfx_rect_hgrad(320, 78, (SAFE_R - SAFE_L) / 2.0f, 1, rgba(140, 125, 255, 90), rgba(140, 125, 255, 0));

	float bw = 320, bh = 44, x = 320 - bw / 2;
	for (int i = 0; i < MAIN_COUNT; i++) {
		float y = CONTENT_T + 8 + i * (bh + 8);
		bool s = i == main_sel;
		if (s) {
			ui_selection(x, y, bw, bh);
		} else {
			gfx_rrect(x, y, bw, bh, 9, rgba(20, 18, 50, 190));
			gfx_rrect_line(x, y, bw, bh, 9, 1, rgba(140, 125, 255, 50));
		}
		ui_icon(main_icons[i], x + 30, y + bh / 2, 20, s ? C_TEXT : C_ACCENT_HI);
		font_mid(x + 56, y + bh / 2, 17, s ? C_TEXT : C_SUB, ALIGN_LEFT, 0, main_items[i]);
	}
	font_wrap(320, CONTENT_B + 24, 13, 17, C_SUB, ALIGN_CENTER, 440, 2, main_notes[main_sel]);
	static const Hint h[] = { { "A", "Select" } };
	ui_hints(h, 1);

	if (popup.open)
		return;
	int top = 0;
	dirs &= PAD_BUTTON_UP | PAD_BUTTON_DOWN;
	move_in_list(&main_sel, &top, MAIN_COUNT, MAIN_COUNT);
	if (down & PAD_BUTTON_A) {
		switch (main_sel) {
		case MAIN_DISC: sound_play(SFX_SELECT); disc_load_start(); break;
		case MAIN_SAVED: saved_open(); break;
		case MAIN_SETTINGS: settings_open(); break;
		case MAIN_CREDITS: sound_play(SFX_SELECT); go(ST_CREDITS); break;
		case MAIN_EXIT: exit_open(); break;
		}
	}
}

/* ------------------------------------------------------------- the frame */

void menu_init(bool sd) {
	sd_present = sd;
	intro_init();
	go(ST_MAIN);
	if (settings.intro)
		intro_start();
}

void menu_frame(void) {
	down = PAD_ButtonsDown(0);
	dirs = read_dirs();
	ui_update();
	update_playback();

	if (intro_covering()) {
		intro_input(down);
		intro_draw();
		return;
	}

	/* input goes to the keyboard or a popup when one is up (the screens check) */
	u16 all_down = down, all_dirs = dirs;
	bool overlay = popup.open || kb_purpose;
	if (overlay)
		down = dirs = 0;

	ui_background();
	float old = gfx_fade;
	gfx_fade = screen_fade();
	switch (state) {
	case ST_MAIN: main_frame(); break;
	case ST_DISC_LOADING: disc_loading_frame(); break;
	case ST_DISC_LIST: disc_list_frame(); break;
	case ST_SAVED_GAMES: saved_games_frame(); break;
	case ST_SAVED_LIST: saved_list_frame(); break;
	case ST_PLAYER: player_frame(); break;
	case ST_SETTINGS: settings_frame(); break;
	case ST_CREDITS: credits_frame(); break;
	}
	gfx_fade = old;
	if (state != ST_PLAYER && state != ST_MAIN)
		ui_strip();
	if (state == ST_DISC_LIST || state == ST_PLAYER)
		ui_backup();
	ui_status();
	ui_toast_draw();

	down = all_down;
	dirs = all_dirs;
	if (kb_purpose) {
		bool kb_input = overlay && !popup.open;
		int r = kb_frame(kb_input ? down : 0, kb_input ? dirs : 0, kb_input ? PAD_ButtonsHeld(0) : 0);
		if (r == KB_DONE) {
			sound_play(SFX_CONFIRM);
			keyboard_done(kb_text());
		} else if (r == KB_CANCEL) {
			sound_play(SFX_CANCEL);
			kb_purpose = KBP_NONE;
		}
	}
	if (popup.open) {
		if (popup_ready && overlay)
			popup_frame();
		popup_ready = true;
		if (popup.open)
			ui_popup(popup.title, popup.row_ptr, popup.count, popup.sel, popup.note[0] ? popup.note : NULL, popup.danger);
	}
	intro_draw();
}
