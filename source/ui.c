/* ui.c -- see ui.h
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/statvfs.h>
#include <gccore.h>
#include "ui.h"
#include "disc_sf.h"
#include "font.h"
#include "sky.h"
#include "disc.h"
#include "audio.h"
#include "dvd.h"
#include "backup.h"
#include "settings.h"

const Color C_TEXT = { 238, 238, 252, 255 };
const Color C_SUB = { 170, 172, 210, 255 };
const Color C_DIM = { 112, 112, 150, 255 };
const Color C_ACCENT = { 112, 92, 255, 255 };
const Color C_ACCENT_HI = { 170, 155, 255, 255 };
const Color C_GOOD = { 96, 214, 150, 255 };
const Color C_WARN = { 240, 200, 90, 255 };
const Color C_BAD = { 240, 100, 100, 255 };

NowPlaying now_playing;

static float disc_angle, disc_speed;
static float level;

/* SD card */
static bool sd_present;
static volatile int sd_state;   /* 0 counting, 1 known, 2 failed */
static volatile unsigned long long sd_total, sd_free;
static float sd_write_at = -10;    /* last written or read */
static u32 sd_last_read;
static u32 seen_written;
static bool seen_running;

/* DVD */
static u32 dvd_last_bytes;
static float dvd_sample_at, dvd_rate, dvd_read_at = -10;

/* toast */
static char toast_msg[128];
static float toast_at = -10;

/* backup animation */
#define FLYERS 12
#define BACKUP_X 360   /* left edge of the backup card; the now-playing strip ends 8 px before it */
typedef struct { bool on; float born, x0, y0, spin; } Flyer;
static Flyer flyers[FLYERS];
static float origin_x = 200, origin_y = 220;
static float folder_x, folder_y, gulp_at = -10, spawn_at;
static float backup_shown_until;
static int last_file_index;

static lwp_t sd_thread = LWP_THREAD_NULL;

static void *sd_main(void *arg) {
	struct statvfs v;
	/* (counting the free clusters reads the whole FAT: a few seconds on a big card) */
	if (statvfs("/", &v) == 0) {
		unsigned long long bs = v.f_frsize ? v.f_frsize : v.f_bsize;
		sd_total = (unsigned long long)v.f_blocks * bs;
		sd_free = (unsigned long long)v.f_bfree * bs;
		sd_state = 1;
	} else {
		sd_state = 2;
	}
	return NULL;
}

void ui_init(bool sd) {
	sd_present = sd;
	sky_init();
	if (sd)
		LWP_CreateThread(&sd_thread, sd_main, NULL, NULL, 16 * 1024, 30);
}

void ui_sd_changed(long long delta) {
	if (sd_state != 1)
		return;
	long long f = (long long)sd_free - delta;
	sd_free = f < 0 ? 0 : (f > (long long)sd_total ? sd_total : f);
}

float ui_disc_angle(void) { return disc_angle; }
float ui_music_level(void) { return level; }

void ui_update(void) {
	float dt = frame_dt;
	/* the disc spins up when playing and coasts down when not */
	float target = now_playing.active && !now_playing.paused ? 1.8f : 0;
	disc_speed += (target - disc_speed) * (1 - expf(-dt * (target > disc_speed ? 1.6f : 0.9f)));
	disc_angle = fmodf(disc_angle + disc_speed * dt, 2 * (float)M_PI);

	float l = audio_level();
	l = l > 0 ? sqrtf(l) * 1.6f : 0;   /* (perceptual-ish) */
	if (l > 1) l = 1;
	level += (l - level) * (1 - expf(-dt * (l > level ? 14 : 4)));

	if (now_playing.active && !now_playing.paused)
		stats_add_played(dt);

	/* the backup's writes: statistics, free space, the SD light */
	if (backup_status.running && !seen_running)
		seen_written = 0;
	seen_running = backup_status.running;
	u32 w = backup_status.bytes_written;
	if (w > seen_written) {
		stats_add_backed_up(w - seen_written);
		ui_sd_changed(w - seen_written);
		seen_written = w;
		sd_write_at = frame_time;
	}
	/* tracks played from SD light it the same way */
	u32 rd = sd_bytes_read;
	if (rd != sd_last_read) {
		sd_last_read = rd;
		sd_write_at = frame_time;
	}

	/* the disc's read rate, over half a second */
	if (frame_time - dvd_sample_at >= 0.5f) {
		u32 b = dvd_bytes_read;
		float span = frame_time - dvd_sample_at;
		float r = (b - dvd_last_bytes) / span;
		if (b != dvd_last_bytes)
			dvd_read_at = frame_time;
		dvd_rate = dvd_rate * 0.4f + r * 0.6f;
		dvd_last_bytes = b;
		dvd_sample_at = frame_time;
	}

	/* files fly while copying */
	if (backup_status.running) {
		if (backup_status.file_index != last_file_index || frame_time - spawn_at > 0.45f) {
			last_file_index = backup_status.file_index;
			spawn_at = frame_time;
			for (int i = 0; i < FLYERS; i++)
				if (!flyers[i].on) {
					flyers[i] = (Flyer){ true, frame_time, origin_x + (rand() % 60) - 30, origin_y + (rand() % 40) - 20,
					                     ((rand() % 100) - 50) / 40.0f };
					break;
				}
		}
		backup_shown_until = frame_time + 6;
	}
	origin_x = 200;
	origin_y = 220;
}

void ui_backup_origin(float x, float y) {
	origin_x = x;
	origin_y = y;
}

void ui_background(void) {
	sky_draw(level);
}

/* ------------------------------------------------------------- bits */

void ui_fmt_time(char *out, size_t size, float s) {
	int t = (int)s;
	if (t >= 3600)
		snprintf(out, size, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	else
		snprintf(out, size, "%d:%02d", t / 60, t % 60);
}

void ui_fmt_size(char *out, size_t size, unsigned long long b) {
	if (b >= 1000ull * 1024 * 1024)
		snprintf(out, size, "%.1f GB", b / (1024.0 * 1024 * 1024));
	else if (b >= 10ull * 1024 * 1024)
		snprintf(out, size, "%.0f MB", b / (1024.0 * 1024));
	else
		snprintf(out, size, "%.1f MB", b / (1024.0 * 1024));
}

void ui_panel(float x, float y, float w, float h) {
	gfx_rrect_glow(x, y + 3, w, h, 14, 10, rgba(0, 0, 0, 70));
	gfx_rrect_vgrad(x, y, w, h, 14, rgba(22, 20, 52, 200), rgba(12, 11, 32, 215));
	gfx_rrect_line(x, y, w, h, 14, 1.2f, rgba(140, 125, 255, 60));
}

void ui_selection(float x, float y, float w, float h) {
	float p = 0.5f + 0.5f * sinf(frame_time * 3.2f);
	gfx_blend_add(true);
	gfx_rrect_glow(x, y, w, h, 9, 7, rgba(120, 100, 255, (int)(40 + 50 * p)));
	gfx_blend_add(false);
	gfx_rrect_vgrad(x, y, w, h, 9, rgba(104, 86, 236, 235), rgba(70, 56, 190, 235));
	gfx_rrect_line(x, y, w, h, 9, 1.2f, rgba(200, 190, 255, (int)(110 + 80 * p)));
}

void ui_wordmark(float x, float cy, int size) {
	float w = font_mid(x, cy, size, C_TEXT, ALIGN_LEFT, 0, "Cube");
	font_mid(x + w, cy, size, C_ACCENT_HI, ALIGN_LEFT, 0, "Music");
}

void ui_header(const char *title, const char *subtitle, GXTexObj *banner) {
	float x = SAFE_L + 4;
	if (banner) {
		gfx_rrect(x - 3, 25, 102, 38, 6, rgba(10, 8, 30, 200));
		gfx_image(banner, x, 28, 96, 32, rgba(255, 255, 255, 255));
		gfx_rrect_line(x - 3, 25, 102, 38, 6, 1, rgba(160, 150, 255, 90));
		x += 112;
	}
	float maxw = SAFE_R - x - 8;
	if (subtitle && *subtitle) {
		font_mid(x, 38, 22, C_TEXT, ALIGN_LEFT, maxw, title);
		font_mid(x, 62, 14, C_SUB, ALIGN_LEFT, maxw, subtitle);
	} else {
		font_mid(x, 46, 24, C_TEXT, ALIGN_LEFT, maxw, title);
	}
	gfx_rect_hgrad(SAFE_L, 78, (SAFE_R - SAFE_L) / 2.0f, 1, rgba(140, 125, 255, 0), rgba(140, 125, 255, 90));
	gfx_rect_hgrad(320, 78, (SAFE_R - SAFE_L) / 2.0f, 1, rgba(140, 125, 255, 90), rgba(140, 125, 255, 0));
}

void ui_icon(int icon, float cx, float cy, float s, Color c) {
	float h = s / 2;
	switch (icon) {
	case ICON_DISC:
		gfx_ring(cx, cy, h * 0.28f, h, c, c);
		gfx_ring(cx, cy, h * 0.42f, h * 0.5f, col_alpha(c, 0.4f), col_alpha(c, 0.4f));
		break;
	case ICON_FOLDER:
		gfx_rrect(cx - h, cy - h * 0.7f, h * 0.9f, h * 0.5f, h * 0.12f, c);
		gfx_rrect(cx - h, cy - h * 0.45f, s, h * 1.3f, h * 0.16f, c);
		gfx_rect(cx - h * 0.85f, cy - h * 0.2f, s * 0.85f, h * 0.08f, col_alpha(rgba(0, 0, 0, 255), 0.25f));
		break;
	case ICON_GEAR:
		for (int i = 0; i < 8; i++) {
			float a = i * (float)M_PI / 4;
			gfx_line(cx + cosf(a) * h * 0.5f, cy + sinf(a) * h * 0.5f, cx + cosf(a) * h, cy + sinf(a) * h, h * 0.36f, c);
		}
		gfx_ring(cx, cy, h * 0.3f, h * 0.74f, c, c);
		break;
	case ICON_INFO:
		gfx_ring(cx, cy, h * 0.82f, h, c, c);
		gfx_circle(cx, cy - h * 0.42f, h * 0.13f, c);
		gfx_line(cx, cy - h * 0.14f, cx, cy + h * 0.52f, h * 0.22f, c);
		break;
	case ICON_EXIT:
		gfx_arc(cx, cy + h * 0.08f, h * 0.68f, h * 0.88f, -(float)M_PI / 2 + 0.65f, 3 * (float)M_PI / 2 - 0.65f, c);
		gfx_line(cx, cy - h, cx, cy + h * 0.05f, h * 0.22f, c);
		break;
	case ICON_SD: {
		float x0 = cx - h * 0.72f, x1 = cx + h * 0.72f, y0 = cy - h, y1 = cy + h, cut = h * 0.5f;
		const float body[8] = { x0, y0, x1 - cut, y0, x1, y0 + cut, x1, y1 };
		Color cc[4] = { c, c, c, c };
		gfx_quad(body, cc);
		const float rest[8] = { x0, y0, x1, y1, x0, y1, x0, y1 };
		gfx_quad(rest, cc);
		for (int i = 0; i < 3; i++)
			gfx_rect(x0 + h * 0.2f + i * h * 0.34f, y0 + h * 0.18f, h * 0.18f, h * 0.45f, col_alpha(rgba(0, 0, 0, 255), 0.45f));
		break;
	}
	case ICON_PLAY:
		gfx_tri(cx - h * 0.6f, cy - h * 0.8f, cx + h * 0.85f, cy, cx - h * 0.6f, cy + h * 0.8f, c, c, c);
		break;
	case ICON_PAUSE:
		gfx_rrect(cx - h * 0.7f, cy - h * 0.8f, h * 0.5f, h * 1.6f, h * 0.1f, c);
		gfx_rrect(cx + h * 0.2f, cy - h * 0.8f, h * 0.5f, h * 1.6f, h * 0.1f, c);
		break;
	case ICON_NOTE:
		gfx_circle(cx - h * 0.3f, cy + h * 0.55f, h * 0.38f, c);
		gfx_line(cx + h * 0.02f, cy + h * 0.55f, cx + h * 0.02f, cy - h * 0.9f, h * 0.16f, c);
		gfx_line(cx + h * 0.02f, cy - h * 0.86f, cx + h * 0.6f, cy - h * 0.45f, h * 0.2f, c);
		break;
	case ICON_FILE: {
		float x0 = cx - h * 0.72f, x1 = cx + h * 0.72f, y0 = cy - h, y1 = cy + h, cut = h * 0.5f;
		Color cc[4] = { c, c, c, c };
		const float a[8] = { x0, y0, x1 - cut, y0, x1, y0 + cut, x1, y1 };
		const float b[8] = { x0, y0, x1, y1, x0, y1, x0, y1 };
		gfx_quad(a, cc);
		gfx_quad(b, cc);
		Color fold = col_mix(c, rgba(0, 0, 0, c.a), 0.3f);
		gfx_tri(x1 - cut, y0, x1 - cut, y0 + cut, x1, y0 + cut, fold, fold, fold);
		for (int i = 0; i < 3; i++)
			gfx_rect(x0 + h * 0.25f, y0 + h * (0.75f + i * 0.35f), h * 0.95f, h * 0.1f, col_alpha(rgba(60, 50, 120, 255), c.a / 255.0f * 0.6f));
		break;
	}
	case ICON_CHECK:
		gfx_line(cx - h * 0.6f, cy, cx - h * 0.15f, cy + h * 0.5f, h * 0.3f, c);
		gfx_line(cx - h * 0.25f, cy + h * 0.5f, cx + h * 0.7f, cy - h * 0.55f, h * 0.3f, c);
		break;
	case ICON_STATS:
		for (int i = 0; i < 3; i++) {
			float bh = h * (0.7f + 0.5f * i);
			gfx_rrect(cx - h * 0.85f + i * h * 0.62f, cy + h - bh, h * 0.45f, bh, h * 0.08f, c);
		}
		break;
	}
}

void ui_checkbox(float cx, float cy, bool on) {
	if (on) {
		gfx_rrect(cx - 8, cy - 8, 16, 16, 4, rgba(130, 112, 255, 255));
		ui_icon(ICON_CHECK, cx, cy, 13, rgba(255, 255, 255, 255));
	} else {
		gfx_rrect_line(cx - 8, cy - 8, 16, 16, 4, 1.6f, rgba(170, 165, 220, 170));
	}
}

void ui_scrollbar(float x, float y, float h, int first, int visible, int total) {
	if (total <= visible)
		return;
	gfx_rrect(x, y, 4, h, 2, rgba(255, 255, 255, 25));
	float th = h * visible / total;
	if (th < 16) th = 16;
	float ty = y + (h - th) * first / (float)(total - visible);
	gfx_rrect(x, ty, 4, th, 2, rgba(170, 160, 255, 170));
}

void ui_progress(float x, float y, float w, float h, float k, Color fill) {
	if (k < 0) k = 0;
	if (k > 1) k = 1;
	gfx_rrect(x, y, w, h, h / 2, rgba(255, 255, 255, 30));
	if (k > 0)
		gfx_rrect_vgrad(x, y, w * k < h ? h : w * k, h, h / 2, col_mix(fill, rgba(255, 255, 255, 255), 0.25f), fill);
}

float ui_button(float x, float cy, const char *b) {
	Color white = rgba(255, 255, 255, 255), dark = rgba(30, 30, 44, 255);
	if (!strcmp(b, "A")) {
		gfx_circle(x + 9, cy, 9, rgba(44, 178, 112, 255));
		font_mid(x + 9, cy, 12, white, ALIGN_CENTER, 0, "A");
		return 18;
	}
	if (!strcmp(b, "B")) {
		gfx_circle(x + 8, cy, 8, rgba(222, 72, 72, 255));
		font_mid(x + 8, cy, 11, white, ALIGN_CENTER, 0, "B");
		return 16;
	}
	if (!strcmp(b, "LR") || !strcmp(b, "UD")) {
		/* the control pad, with the arms that do something lit */
		bool lr = b[0] == 'L';
		Color off = rgba(150, 150, 170, 255), on = rgba(235, 235, 250, 255);
		gfx_rect(x + 1, cy - 3, 16, 6, lr ? on : off);
		gfx_rect(x + 6, cy - 8, 6, 16, lr ? off : on);
		gfx_rect(x + 6, cy - 3, 6, 6, off);
		return 18;
	}
	Color bg = !strcmp(b, "Z") ? rgba(124, 92, 214, 255) : rgba(196, 196, 212, 255);
	Color fg = !strcmp(b, "Z") ? white : dark;
	int size = strlen(b) > 1 ? 9 : 11;
	float w = font_width(size, b) + 10;
	if (w < 18) w = 18;
	gfx_rrect(x, cy - 8, w, 16, 8, bg);
	font_mid(x + w / 2, cy, size, fg, ALIGN_CENTER, 0, b);
	return w;
}

void ui_hints(const Hint *hints, int count) {
	float x = SAFE_L;
	for (int i = 0; i < count; i++) {
		x += ui_button(x, BAR_CY, hints[i].button) + 5;
		x += font_mid(x, BAR_CY, 13, C_SUB, ALIGN_LEFT, 0, hints[i].label) + 13;
	}
}

/* --------------------------------------------------------- status chips */

/* min_text: the text area is never narrower than this (so the chip doesn't change size) */
static float chip(float right, Color glow, float glow_k, int icon, Color icon_c, const char *top, const char *bottom,
                  float min_text) {
	float tw = font_width(10, top), bw = font_width(12, bottom);
	float tx = tw > bw ? tw : bw;
	if (tx < min_text)
		tx = min_text;
	float w = 30 + tx + 10;
	float x = right - w, y = BAR_CY - 15;
	if (glow_k > 0.01f) {
		gfx_blend_add(true);
		gfx_rrect_glow(x, y, w, 30, 10, 9, col_alpha(glow, glow_k));
		gfx_blend_add(false);
	}
	gfx_rrect(x, y, w, 30, 10, rgba(16, 14, 40, 210));
	gfx_rrect_line(x, y, w, 30, 10, 1, col_mix(rgba(140, 125, 255, 60), glow, glow_k));
	ui_icon(icon, x + 15, BAR_CY, 15, icon_c);
	font_mid(x + 28, BAR_CY - 6, 10, C_DIM, ALIGN_LEFT, 0, top);
	font_mid(x + 28, BAR_CY + 6, 12, C_TEXT, ALIGN_LEFT, 0, bottom);
	return w;
}

void ui_status(void) {
	char buf[48];
	float right = SAFE_R;

	/* the disc drive: its read rate while it's reading. Always as wide as
	 * the widest "x.x MB/s", so it never changes size. */
	static float dvd_w;
	if (!dvd_w) {
		for (int d = 0; d <= 9; d++) {
			char t[16];
			snprintf(t, sizeof(t), "%d.%d MB/s", d, d);
			float fw = font_width(12, t);
			if (fw > dvd_w)
				dvd_w = fw;
		}
	}
	static float dvd_k, sd_k;   /* how lit they are, eased in and out */
	bool reading = frame_time - dvd_read_at < 1.2f;
	dvd_k += ((reading ? 1.0f : 0.0f) - dvd_k) * (1 - expf(-frame_dt * 6));
	float mb = dvd_rate / (1024 * 1024);
	if (reading)
		snprintf(buf, sizeof(buf), "%.1f MB/s", mb > 9.9f ? 9.9f : mb);
	else
		snprintf(buf, sizeof(buf), "Idle");
	Color led = col_mix(rgba(120, 120, 150, 255), rgba(235, 196, 120, 255), dvd_k * 0.7f);
	right -= chip(right, rgba(255, 180, 80, 255), dvd_k * (0.22f + 0.06f * sinf(frame_time * 5)), ICON_DISC, led,
	              "DVD", buf, dvd_w) + 8;

	/* the SD card: free space, lit while it's being written or read */
	bool busy = frame_time - sd_write_at < 0.6f;
	sd_k += ((busy ? 1.0f : 0.0f) - sd_k) * (1 - expf(-frame_dt * 6));
	if (!sd_present)
		snprintf(buf, sizeof(buf), "None");
	else if (sd_state == 0)
		snprintf(buf, sizeof(buf), "Checking...");
	else if (sd_state == 2)
		snprintf(buf, sizeof(buf), "Ready");
	else
		ui_fmt_size(buf, sizeof(buf), sd_free);
	chip(right, rgba(120, 220, 160, 255), sd_k * (0.22f + 0.06f * sinf(frame_time * 5)), ICON_SD,
	     sd_present ? col_mix(rgba(190, 190, 215, 255), rgba(150, 235, 190, 255), sd_k * 0.7f) : rgba(110, 110, 130, 255),
	     sd_state == 1 ? "SD free" : "SD card", buf, 0);
}

/* ------------------------------------------------------- now playing */

bool ui_backup_visible(void) {
	return backup_status.running || frame_time < backup_shown_until;
}

void ui_strip(void) {
	if (!now_playing.active)
		return;
	float x = SAFE_L, w = (ui_backup_visible() ? BACKUP_X - 8 : SAFE_R) - x, y = STRIP_Y, h = STRIP_H;
	gfx_rrect(x, y, w, h, 12, rgba(16, 14, 42, 215));
	gfx_rrect_line(x, y, w, h, 12, 1, rgba(140, 125, 255, 70));
	disc_draw(x + 22, y + h / 2, 15, 15, ui_disc_angle(), 1, NULL, NULL, 0);

	char pos[16], dur[16], t[40];
	ui_fmt_time(pos, sizeof(pos), now_playing.position);
	if (now_playing.duration > 0) {
		ui_fmt_time(dur, sizeof(dur), now_playing.duration);
		snprintf(t, sizeof(t), "%s / %s", pos, dur);
	} else {
		snprintf(t, sizeof(t), "%s / loop", pos);
	}
	float tw = font_width(12, t);
	float tx = x + w - 12;
	font_mid(tx, y + 14, 12, C_SUB, ALIGN_RIGHT, 0, t);
	ui_icon(now_playing.paused ? ICON_PAUSE : ICON_PLAY, tx - tw - 12, y + 14, 10, now_playing.paused ? C_WARN : C_ACCENT_HI);

	float textw = w - 46 - tw - 34;
	font_mid(x + 44, y + 14, 14, C_TEXT, ALIGN_LEFT, textw, now_playing.title);
	float k = now_playing.duration > 0 ? now_playing.position / now_playing.duration : 0;
	ui_progress(x + 44, y + 28, w - 56, 4, k, C_ACCENT);
}

/* --------------------------------------------------------------- backup */

static float ease_out(float t) { return 1 - (1 - t) * (1 - t); }

void ui_backup(void) {
	if (!ui_backup_visible())
		return;
	float x = BACKUP_X, y = STRIP_Y, w = SAFE_R - x, h = STRIP_H;
	float a = backup_status.running ? 1 : (backup_shown_until - frame_time) / 1.0f;
	if (a > 1) a = 1;
	float old = gfx_fade;
	gfx_fade *= a;

	gfx_rrect(x, y, w, h, 12, rgba(16, 14, 42, 225));
	gfx_rrect_line(x, y, w, h, 12, 1, rgba(140, 125, 255, 80));

	/* the folder, which gulps when a file lands */
	folder_x = x + 22;
	folder_y = y + h / 2 + 1;
	float g = frame_time - gulp_at < 0.25f ? sinf((frame_time - gulp_at) / 0.25f * (float)M_PI) : 0;
	ui_icon(ICON_FOLDER, folder_x, folder_y - g * 1.5f, 22 + g * 4, rgba(255, 206, 110, 255));

	char line[128];
	float pct = backup_status.bytes_total ? (float)backup_status.bytes_done / backup_status.bytes_total : 0;
	if (backup_status.running) {
		char p[8];
		snprintf(p, sizeof(p), "%d%%", (int)(pct * 100));
		float pw = font_width(12, "100%");
		font_mid(x + w - 10, y + 13, 12, C_SUB, ALIGN_RIGHT, 0, p);
		snprintf(line, sizeof(line), "Backing up %d track%s", backup_status.track_total, backup_status.track_total == 1 ? "" : "s");
		font_mid(x + 42, y + 13, 13, C_TEXT, ALIGN_LEFT, w - 42 - 10 - pw - 6, line);
		ui_progress(x + 42, y + 26, w - 52, 5, pct, rgba(255, 196, 90, 255));
	} else if (backup_status.errors) {
		snprintf(line, sizeof(line), "%d file%s failed", backup_status.errors, backup_status.errors == 1 ? "" : "s");
		font_mid(x + 42, y + 13, 13, C_BAD, ALIGN_LEFT, w - 50, line);
		font_mid(x + 42, y + 28, 11, C_SUB, ALIGN_LEFT, w - 50, backup_status.error);
	} else {
		snprintf(line, sizeof(line), "Backed up %d track%s", backup_status.track_total, backup_status.track_total == 1 ? "" : "s");
		font_mid(x + 42, y + 13, 13, C_GOOD, ALIGN_LEFT, w - 50, line);
		if (backup_status.skipped)
			snprintf(line, sizeof(line), "%d file%s already on the SD card", backup_status.skipped, backup_status.skipped == 1 ? "" : "s");
		else
			snprintf(line, sizeof(line), "Saved to the SD card");
		font_mid(x + 42, y + 28, 11, C_SUB, ALIGN_LEFT, w - 50, line);
	}
	gfx_fade = old;

	/* the files, flying in an arc into the folder */
	for (int i = 0; i < FLYERS; i++) {
		Flyer *f = &flyers[i];
		if (!f->on)
			continue;
		float t = (frame_time - f->born) / 0.85f;
		if (t >= 1) {
			f->on = false;
			gulp_at = frame_time;
			continue;
		}
		float e = ease_out(t);
		float fx = f->x0 + (folder_x - f->x0) * e;
		float fy = f->y0 + (folder_y - f->y0) * t * t - 90 * sinf(t * (float)M_PI);
		float s = 20 * (1 - 0.55f * t);
		float ang = f->spin * t;
		const float m[6] = { cosf(ang), -sinf(ang), fx, sinf(ang), cosf(ang), fy };
		gfx_transform(m);
		ui_icon(ICON_FILE, 0, 0, s, rgba(240, 240, 255, (int)(255 * (t < 0.1f ? t * 10 : 1))));
		gfx_identity();
	}
}

/* --------------------------------------------------------------- toast */

void ui_toast(const char *msg) {
	snprintf(toast_msg, sizeof(toast_msg), "%s", msg);
	toast_at = frame_time;
}

void ui_toast_draw(void) {
	float t = frame_time - toast_at;
	if (t > 2.8f || !toast_msg[0])
		return;
	float a = t < 0.15f ? t / 0.15f : t > 2.4f ? (2.8f - t) / 0.4f : 1;
	float w = font_width(14, toast_msg) + 36;
	if (w > 520) w = 520;
	float x = 320 - w / 2, y = CONTENT_B - 44 + (1 - a) * 8;
	float old = gfx_fade;
	gfx_fade *= a;
	gfx_rrect_glow(x, y, w, 32, 16, 10, rgba(0, 0, 0, 110));
	gfx_rrect(x, y, w, 32, 16, rgba(44, 36, 120, 240));
	gfx_rrect_line(x, y, w, 32, 16, 1, rgba(190, 180, 255, 120));
	font_mid(320, y + 16, 14, C_TEXT, ALIGN_CENTER, w - 30, toast_msg);
	gfx_fade = old;
}

/* --------------------------------------------------------------- popup */

void ui_popup(const char *title, const char *const *rows, int count, int sel, const char *note, bool danger) {
	gfx_rect(view_l, view_t, view_r - view_l, view_b - view_t, rgba(4, 3, 14, 150));
	float w = 340, rh = 34;
	int note_lines = note ? font_wrap_count(14, w - 40, 7, note) : 0;
	float h = 58 + note_lines * 19 + count * rh + 12 + (note_lines ? 8 : 0);
	float x = 320 - w / 2, y = 228 - h / 2;
	ui_panel(x, y, w, h);
	if (danger)
		gfx_rrect(x + 1, y + 1, w - 2, 4, 2, C_BAD);
	font_mid(x + 20, y + 28, 18, danger ? C_BAD : C_TEXT, ALIGN_LEFT, w - 40, title);
	float ry = y + 50;
	if (note_lines) {
		font_wrap(x + 20, ry + 12, 14, 19, C_SUB, ALIGN_LEFT, w - 40, 7, note);
		ry += note_lines * 19 + 8;
	}
	for (int i = 0; i < count; i++) {
		float yy = ry + i * rh;
		bool red = danger && i == count - 1;
		if (i == sel && red) {
			float p = 0.5f + 0.5f * sinf(frame_time * 3.2f);
			gfx_rrect_vgrad(x + 12, yy + 2, w - 24, rh - 4, 9, rgba(200, 60, 60, 235), rgba(150, 36, 40, 235));
			gfx_rrect_line(x + 12, yy + 2, w - 24, rh - 4, 9, 1.2f, rgba(255, 190, 190, (int)(110 + 80 * p)));
		} else if (i == sel) {
			ui_selection(x + 12, yy + 2, w - 24, rh - 4);
		}
		font_mid(x + 26, yy + rh / 2, 15, i == sel ? C_TEXT : red ? C_BAD : C_SUB, ALIGN_LEFT, w - 52, rows[i]);
	}
}
