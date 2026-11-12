/* ui.h -- the parts every screen shares: colours, panels, rows, the hints
 * and status bar, the now-playing strip, the backup card and its animation
 *
 * Layout (640x480, everything inside the title-safe area):
 *   header       y  24.. 76
 *   content      y  88..368
 *   now playing  y 378..418   (and the backup card on the right while it runs)
 *   bottom bar   y 430..456   hints on the left, SD and DVD on the right
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __UI_H
#define __UI_H

#include <gccore.h>
#include <stdbool.h>
#include "video.h"

#define SAFE_L   32
#define SAFE_R   608
#define CONTENT_T 88
#define CONTENT_B 368
#define STRIP_Y  378
#define STRIP_H  40
#define BAR_CY   443

extern const Color C_TEXT, C_SUB, C_DIM, C_ACCENT, C_ACCENT_HI, C_GOOD, C_WARN, C_BAD;

enum {
	ICON_DISC, ICON_FOLDER, ICON_GEAR, ICON_INFO, ICON_EXIT, ICON_SD, ICON_PLAY, ICON_PAUSE,
	ICON_NOTE, ICON_FILE, ICON_CHECK, ICON_STATS,
};

/* What's playing, for the strip and the player screen (set by menu.c) */
typedef struct {
	bool active;            /* a track is loaded */
	bool paused;
	char title[128];
	char game[128];
	char format[96];        /* "DSP ADPCM, 32000 Hz, 2ch" */
	GXTexObj banner;
	bool has_banner;
	float position, duration;
} NowPlaying;
extern NowPlaying now_playing;

void ui_init(bool sd_present);
/* once a frame, before drawing */
void ui_update(void);
void ui_background(void);

float ui_disc_angle(void);   /* the playing disc's turn (radians) */
float ui_music_level(void);  /* smoothed loudness 0..1 */

void ui_panel(float x, float y, float w, float h);
void ui_selection(float x, float y, float w, float h);   /* the highlighted row */
void ui_header(const char *title, const char *subtitle, GXTexObj *banner);
void ui_wordmark(float x, float cy, int size);           /* "CubeMusic" */
void ui_icon(int icon, float cx, float cy, float size, Color c);
void ui_checkbox(float cx, float cy, bool checked);
void ui_scrollbar(float x, float y, float h, int first, int visible, int total);
void ui_progress(float x, float y, float w, float h, float k, Color fill);
/* a controller button: "A", "B", "X", "Y", "Z", "L", "R", "START", "LR" (left/right on the
 * control pad), "UD" (up/down). Returns its width. */
float ui_button(float x, float cy, const char *button);

typedef struct { const char *button, *label; } Hint;
void ui_hints(const Hint *hints, int count);
void ui_status(void);       /* SD and DVD, bottom right */
void ui_strip(void);        /* the now-playing strip (if something's playing) */
void ui_backup(void);       /* the backup card and the files flying into it */
void ui_backup_origin(float x, float y);   /* where the flying files start (this frame) */
bool ui_backup_visible(void);

void ui_toast(const char *msg);
void ui_toast_draw(void);

/* a centred box with a title and rows, sel highlighted (-1 none). danger:
 * a confirmation of something that can't be undone (red title, and the last
 * row, the one that does it, in red) */
void ui_popup(const char *title, const char *const *rows, int count, int sel, const char *note, bool danger);

void ui_fmt_time(char *out, size_t size, float seconds);
void ui_fmt_size(char *out, size_t size, unsigned long long bytes);

/* the SD card got new files (or lost some): free space changes by delta bytes */
void ui_sd_changed(long long delta);

#endif
