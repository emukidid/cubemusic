/* keyboard.c -- see keyboard.h
 *
 * A grid of 11 x 4 character keys, then Shift, Space, Delete, Cancel and Done.
 * L/R move the cursor, B deletes, Y shifts, X puts back the default name,
 * Z clears, START is Done. A legend along the bottom says so.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "keyboard.h"
#include "ui.h"
#include "font.h"
#include "text.h"
#include "sound.h"

#define COLS 11
#define ROWS 5      /* 4 of characters, then the wide keys */
#define KEY_W 40
#define KEY_H 34
#define GAP 4

static const char *const lower[4] = { "1234567890-", "qwertyuiop'", "asdfghjkl:;", "zxcvbnm,.!?" };
static const char *const upper[4] = { "!#%&()+=/_~", "QWERTYUIOP\"", "ASDFGHJKL@*", "ZXCVBNM<>[]" };

/* the bottom row: label, first column, columns wide */
enum { K_SHIFT, K_SPACE, K_DELETE, K_CANCEL, K_DONE, K_WIDE };
static const struct { const char *label; int col, span; } wide[K_WIDE] = {
	{ "Shift", 0, 2 }, { "Space", 2, 3 }, { "Delete", 5, 2 }, { "Cancel", 7, 2 }, { "Done", 9, 2 },
};

static char title[96];
static char text[128];
static char def[128];
static int max_len;
static int cur;              /* the cursor, a byte offset into text */
static float scroll;         /* how far the text is scrolled left to keep the cursor in view */
static int row, col;
static bool shift;
static int lr_frames;        /* L/R held, for repeat */

void kb_open(const char *t, const char *initial, const char *default_text, int max_bytes) {
	snprintf(title, sizeof(title), "%s", t);
	snprintf(text, sizeof(text), "%s", initial ? initial : "");
	snprintf(def, sizeof(def), "%s", default_text ? default_text : "");
	max_len = max_bytes < (int)sizeof(text) ? max_bytes : (int)sizeof(text);
	cur = strlen(text);
	scroll = 0;
	row = 1;
	col = 0;
	shift = false;
	lr_frames = 0;
}

const char *kb_text(void) {
	return text;
}

static int wide_at(int c) {
	for (int i = 0; i < K_WIDE; i++)
		if (c >= wide[i].col && c < wide[i].col + wide[i].span)
			return i;
	return 0;
}

/* the start of the character before / after byte offset i */
static int prev_char(int i) {
	if (i > 0)
		i--;
	while (i > 0 && (text[i] & 0xC0) == 0x80)
		i--;
	return i;
}

static int next_char(int i) {
	if (text[i])
		i++;
	while (text[i] && (text[i] & 0xC0) == 0x80)
		i++;
	return i;
}

static void type(u32 cp) {
	char b[8] = "";
	utf8_put(b, sizeof(b), cp);
	int n = strlen(b), len = strlen(text);
	if (len + n + 1 > max_len) {
		sound_play(SFX_ERROR);
	} else {
		memmove(text + cur + n, text + cur, len - cur + 1);
		memcpy(text + cur, b, n);
		cur += n;
		sound_play(SFX_KEY);
	}
	if (shift && cp != ' ') {
		shift = false;   /* one capital, like a phone */
	}
}

static void backspace(void) {
	if (cur > 0) {
		int p = prev_char(cur);
		memmove(text + p, text + cur, strlen(text + cur) + 1);
		cur = p;
		sound_play(SFX_KEY);
	}
}

static int press(void) {
	if (row < 4) {
		type((u8)(shift ? upper : lower)[row][col]);
		return KB_OPEN;
	}
	switch (wide_at(col)) {
	case K_SHIFT: shift = !shift; sound_play(SFX_KEY); break;
	case K_SPACE: type(' '); break;
	case K_DELETE: backspace(); break;
	case K_CANCEL: return KB_CANCEL;
	case K_DONE: return KB_DONE;
	}
	return KB_OPEN;
}

static void draw_key(float x, float y, float w, const char *label, bool sel, bool lit) {
	if (sel)
		ui_selection(x, y, w, KEY_H);
	else
		gfx_rrect(x, y, w, KEY_H, 7, lit ? rgba(70, 58, 170, 230) : rgba(30, 27, 70, 220));
	font_mid(x + w / 2, y + KEY_H / 2, strlen(label) > 1 ? 13 : 17, sel ? C_TEXT : C_SUB, ALIGN_CENTER, w - 6, label);
}

/* the legend: two rows under the keys */
static void legend(float x, float y) {
	static const struct { const char *button, *label; } rows[2][4] = {
		{ { "A", "Type" }, { "B", "Delete" }, { "Y", "Shift" }, { "L/R", "Move cursor" } },
		{ { "X", "Default name" }, { "Z", "Clear" }, { "START", "Done" }, { NULL, NULL } },
	};
	for (int r = 0; r < 2; r++) {
		float px = x, cy = y + r * 20;
		for (int i = 0; i < 4 && rows[r][i].button; i++) {
			px += ui_button(px, cy, rows[r][i].button) + 5;
			px += font_mid(px, cy, 12, C_SUB, ALIGN_LEFT, 0, rows[r][i].label) + 14;
		}
	}
}

int kb_frame(u16 down, u16 dirs, u16 held) {
	/* input */
	int result = KB_OPEN;
	/* L/R: the cursor, repeating while held */
	u16 lr = held & (PAD_TRIGGER_L | PAD_TRIGGER_R);
	if (!lr || (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)))
		lr_frames = 0;
	if (lr) {
		lr_frames++;
		if (lr_frames == 1 || (lr_frames > 18 && lr_frames % 3 == 0)) {
			int was = cur;
			cur = (lr & PAD_TRIGGER_L) ? prev_char(cur) : next_char(cur);
			if (cur != was)
				sound_play(SFX_HOVER);
		}
	}
	if (dirs & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {
		int step = (dirs & PAD_BUTTON_LEFT) ? -1 : 1;
		if (row == 4)
			col = wide[(wide_at(col) + K_WIDE + step) % K_WIDE].col;
		else
			col = (col + COLS + step) % COLS;
		sound_play(SFX_HOVER);
	}
	if (dirs & PAD_BUTTON_UP) {
		row = (row + ROWS - 1) % ROWS;
		sound_play(SFX_HOVER);
	}
	if (dirs & PAD_BUTTON_DOWN) {
		row = (row + 1) % ROWS;
		sound_play(SFX_HOVER);
	}
	if (row == 4)
		col = wide[wide_at(col)].col;
	if (down & PAD_BUTTON_A)
		result = press();
	if (down & PAD_BUTTON_B)
		backspace();
	if (down & PAD_BUTTON_Y) {
		shift = !shift;
		sound_play(SFX_KEY);
	}
	if (down & PAD_BUTTON_X) {
		snprintf(text, max_len, "%s", def);
		cur = strlen(text);
		sound_play(SFX_SELECT);
	}
	if (down & PAD_TRIGGER_Z) {
		text[0] = 0;
		cur = 0;
		sound_play(SFX_KEY);
	}
	if (down & PAD_BUTTON_START)
		result = KB_DONE;

	/* drawing */
	gfx_rect(view_l, view_t, view_r - view_l, view_b - view_t, rgba(4, 3, 14, 150));
	float gw = COLS * KEY_W + (COLS - 1) * GAP;
	float x0 = 320 - gw / 2, w = gw + 32, h = 352;
	float px = x0 - 16, py = 60;
	ui_panel(px, py, w, h);
	font_mid(px + 18, py + 24, 17, C_TEXT, ALIGN_LEFT, w - 36, title);

	/* the text, with a blinking caret */
	float fy = py + 44;
	gfx_rrect(x0, fy, gw, 38, 8, rgba(6, 5, 20, 230));
	gfx_rrect_line(x0, fy, gw, 38, 8, 1, rgba(150, 135, 255, 120));
	char before[sizeof(text)];
	memcpy(before, text, cur);
	before[cur] = 0;
	float cx = font_width(18, before), room = gw - 30;
	if (cx - scroll > room)
		scroll = cx - room;   /* keep the cursor in view */
	if (cx - scroll < 0)
		scroll = cx;
	float tx = x0 + 12 - scroll;
	gfx_clip(x0 + 4, fy, gw - 8, 38);
	font_mid(tx, fy + 19, 18, C_TEXT, ALIGN_LEFT, 0, text);
	gfx_unclip();
	if (fmodf(frame_time, 1.0f) < 0.6f)
		gfx_rect(tx + cx, fy + 9, 2, 20, C_ACCENT_HI);

	float ky = fy + 52;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < COLS; c++) {
			char label[2] = { (shift ? upper : lower)[r][c], 0 };
			draw_key(x0 + c * (KEY_W + GAP), ky + r * (KEY_H + GAP), KEY_W, label, row == r && col == c, false);
		}
	float by = ky + 4 * (KEY_H + GAP);
	for (int i = 0; i < K_WIDE; i++)
		draw_key(x0 + wide[i].col * (KEY_W + GAP), by, wide[i].span * KEY_W + (wide[i].span - 1) * GAP, wide[i].label,
		         row == 4 && wide_at(col) == i, i == K_SHIFT && shift);
	legend(x0, by + KEY_H + 18);

	return result;
}
