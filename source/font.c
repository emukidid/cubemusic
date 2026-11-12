/* font.c -- see font.h
 *
 * The font is M PLUS Rounded 1c Bold (SIL Open Font License 1.1, see
 * licenses/OFL-MPLUSRounded1c.txt), built into the program.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "font.h"
#include "text.h"
#include "mplus_ttf.h"

#define ATLAS_W   1024
#define ATLAS_H   1024
#define HASH_SIZE 4096      /* power of two */
#define CAP       0.72f     /* capital height, as a fraction of the size */

typedef struct {
	u32 key;                /* 0 = empty */
	u16 x, y;
	u8 w, h;
	s16 left, top;
	s16 adv;
	bool missing;           /* the font hasn't got it */
} Glyph;

static FT_Library ft;
static FT_Face face;
static int face_size;
static u8 *atlas;
static GXTexObj atlas_tex;
static Glyph glyphs[HASH_SIZE];
static int glyph_count;
static int shelf_x, shelf_y, shelf_h;
static bool atlas_full;
static bool ok;

static void cache_clear(void) {
	memset(glyphs, 0, sizeof(glyphs));
	glyph_count = 0;
	memset(atlas, 0, ATLAS_W * ATLAS_H);
	DCFlushRange(atlas, ATLAS_W * ATLAS_H);
	shelf_x = shelf_y = shelf_h = 0;
	atlas_full = false;
	GX_InvalidateTexAll();
}

void font_init(void) {
	atlas = memalign(32, ATLAS_W * ATLAS_H);
	if (!atlas)
		return;
	if (FT_Init_FreeType(&ft) || FT_New_Memory_Face(ft, mplus_ttf, mplus_ttf_size, 0, &face)) {
		printf("font: FreeType couldn't load the font\r\n");
		return;
	}
	gfx_tex_init(&atlas_tex, atlas, ATLAS_W, ATLAS_H, GX_TF_I8, true);
	cache_clear();
	ok = true;
}

void font_frame(void) {
	if (ok && atlas_full)
		cache_clear();   /* (the last frame is drawn: nothing uses the old glyphs now) */
}

static inline void put(int x, int y, u8 v) {
	atlas[((y >> 2) * (ATLAS_W >> 3) + (x >> 3)) * 32 + ((y & 3) << 3) + (x & 7)] = v;
}

static bool new_glyphs;

static const Glyph *glyph(u32 cp, int size) {
	if (!ok || size < 4 || size > 127)
		return NULL;
	u32 key = (cp << 7 | size) + 1;
	u32 h = (key * 2654435761u) & (HASH_SIZE - 1);
	while (glyphs[h].key) {
		if (glyphs[h].key == key)
			return &glyphs[h];
		h = (h + 1) & (HASH_SIZE - 1);
	}
	if (atlas_full || glyph_count >= HASH_SIZE * 3 / 4) {
		atlas_full = true;
		return NULL;
	}
	if (face_size != size) {
		FT_Set_Pixel_Sizes(face, 0, size);
		face_size = size;
	}
	Glyph *g = &glyphs[h];
	FT_UInt index = FT_Get_Char_Index(face, cp);
	if (!index || FT_Load_Glyph(face, index, FT_LOAD_RENDER)) {
		/* not in the font: cache that too (drawn as nothing, or the caller's '?') */
		g->key = key;
		g->missing = true;
		glyph_count++;
		return g;
	}
	FT_GlyphSlot s = face->glyph;
	int w = s->bitmap.width, ht = s->bitmap.rows;
	if (w > 255 || ht > 255)
		w = ht = 0;
	if (shelf_x + w + 1 > ATLAS_W) {
		shelf_x = 0;
		shelf_y += shelf_h + 1;
		shelf_h = 0;
	}
	if (shelf_y + ht + 1 > ATLAS_H) {
		atlas_full = true;
		return NULL;
	}
	g->key = key;
	g->x = shelf_x;
	g->y = shelf_y;
	g->w = w;
	g->h = ht;
	g->left = s->bitmap_left;
	g->top = s->bitmap_top;
	g->adv = (s16)((s->advance.x + 32) >> 6);
	for (int y = 0; y < ht; y++) {
		const u8 *row = s->bitmap.buffer + y * s->bitmap.pitch;
		for (int x = 0; x < w; x++)
			put(g->x + x, g->y + y, row[x]);
	}
	if (ht) {
		u32 from = (g->y >> 2) * (ATLAS_W >> 3) * 32, to = (((g->y + ht + 3) >> 2)) * (ATLAS_W >> 3) * 32;
		DCFlushRange(atlas + from, to - from);
		new_glyphs = true;
	}
	shelf_x += w + 1;
	if (ht > shelf_h)
		shelf_h = ht;
	glyph_count++;
	return g;
}

static const Glyph *glyph_or_box(u32 cp, int size) {
	const Glyph *g = glyph(cp, size);
	if (g && g->missing)
		g = glyph('?', size);
	return g;
}

float font_width(int size, const char *s) {
	float w = 0;
	u32 cp;
	while ((cp = utf8_next(&s))) {
		const Glyph *g = glyph_or_box(cp, size);
		if (g)
			w += g->adv;
	}
	return w;
}

/* how many bytes of s fit in maxw (with room for the ellipsis if it's cut) */
static int fit(int size, const char *s, float maxw, float *width, bool *cut) {
	float w = 0;
	*cut = false;
	if (maxw <= 0 || font_width(size, s) <= maxw) {
		*width = font_width(size, s);
		return strlen(s);
	}
	const Glyph *e = glyph(0x2026, size);
	float ew = e && !e->missing ? e->adv : 0;
	const char *p = s, *last = s;
	u32 cp;
	while ((cp = utf8_next(&p))) {
		const Glyph *g = glyph_or_box(cp, size);
		float a = g ? g->adv : 0;
		if (w + a + ew > maxw)
			break;
		w += a;
		last = p;
	}
	/* no space before the ellipsis */
	while (last > s && last[-1] == ' ') {
		last--;
		w -= glyph(' ', size) ? glyph(' ', size)->adv : 0;
	}
	*width = w + ew;
	*cut = true;
	return last - s;
}

static float draw_run(float x, float y, int size, Color c, const char *s, int len) {
	const char *end = s + len;
	if (new_glyphs) {
		GX_InvalidateTexAll();   /* (queued: the GPU sees the new glyphs from here on) */
		new_glyphs = false;
	}
	gfx_mode_alpha(&atlas_tex);
	float pen = x;
	while (s < end) {
		u32 cp = utf8_next(&s);
		const Glyph *g = glyph_or_box(cp, size);
		if (!g)
			continue;
		if (g->w && g->h) {
			float gx = roundf(pen) + g->left, gy = roundf(y) - g->top;
			float s0 = g->x / (float)ATLAS_W, t0 = g->y / (float)ATLAS_H;
			float s1 = (g->x + g->w) / (float)ATLAS_W, t1 = (g->y + g->h) / (float)ATLAS_H;
			GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			gfx_v(gx, gy, c, s0, t0);
			gfx_v(gx + g->w, gy, c, s1, t0);
			gfx_v(gx + g->w, gy + g->h, c, s1, t1);
			gfx_v(gx, gy + g->h, c, s0, t1);
			GX_End();
		}
		pen += g->adv;
	}
	return pen - x;
}

float font_draw(float x, float y, int size, Color c, int align, float maxw, const char *s) {
	if (!s || !*s || !ok)
		return 0;
	float w;
	bool cut;
	int len = fit(size, s, maxw, &w, &cut);
	if (cut)
		glyph(0x2026, size);
	if (align == ALIGN_CENTER)
		x -= w / 2;
	else if (align == ALIGN_RIGHT)
		x -= w;
	float pen = draw_run(x, y, size, c, s, len);
	if (cut)
		draw_run(x + pen, y, size, c, "\xE2\x80\xA6", 3);
	return w;
}

float font_mid(float x, float y, int size, Color c, int align, float maxw, const char *s) {
	return font_draw(x, y + size * CAP / 2, size, c, align, maxw, s);
}

/* the next line of s that fits in maxw: returns its length, *next = where the following one starts */
static int wrap_line(int size, float maxw, const char *s, const char **next) {
	const char *p = s, *brk = NULL, *brk_next = NULL;
	float w = 0;
	u32 cp;
	const char *before = p;
	while ((cp = utf8_next(&p))) {
		if (cp == '\n') {
			*next = p;
			return before - s;
		}
		const Glyph *g = glyph_or_box(cp, size);
		float a = g ? g->adv : 0;
		if (cp == ' ') {
			brk = before;
			brk_next = p;
		}
		if (w + a > maxw && before > s) {
			if (brk && brk > s) {
				*next = brk_next;
				return brk - s;
			}
			/* no space (a long word, or Japanese): break here */
			*next = before;
			return before - s;
		}
		/* Japanese: may break after any wide character */
		if (cp >= 0x3000) {
			brk = p;
			brk_next = p;
		}
		w += a;
		before = p;
	}
	*next = p;
	return p - s;
}

static int wrap(float x, float y, int size, float line_h, Color c, int align, float maxw, int max_lines, const char *s, bool draw) {
	int lines = 0;
	char buf[512];
	while (*s && lines < max_lines) {
		while (*s == ' ')
			s++;
		const char *next;
		int len = wrap_line(size, maxw, s, &next);
		bool last = lines == max_lines - 1;
		if (last)
			snprintf(buf, sizeof(buf), "%s", s);   /* the rest, cut short */
		else
			snprintf(buf, sizeof(buf), "%.*s", len, s);
		if (draw)
			font_draw(x, y + lines * line_h, size, c, align, maxw, buf);
		lines++;
		s = next;
	}
	return lines;
}

int font_wrap(float x, float y, int size, float line_h, Color c, int align, float maxw, int max_lines, const char *s) {
	if (!s)
		return 0;
	wrap(x, y, size, line_h, c, align, maxw, max_lines, s, false);   /* (caches the glyphs) */
	return wrap(x, y, size, line_h, c, align, maxw, max_lines, s, true);
}

int font_wrap_count(int size, float maxw, int max_lines, const char *s) {
	return s ? wrap(0, 0, size, 0, (Color){ 0 }, 0, maxw, max_lines, s, false) : 0;
}
