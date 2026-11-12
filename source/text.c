/* text.c -- UTF-8 helpers and Shift-JIS / Latin-1 to UTF-8
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <string.h>
#include "text.h"

extern const uint16_t sjis_table[];   /* sjis_table.c (tools/make_sjis_table.py) */
#define TRAIL0  0x40
#define TRAILS  (0xFD - 0x40)

uint32_t utf8_next(const char **s) {
	const uint8_t *p = (const uint8_t *)*s;
	uint32_t c = *p;
	if (!c)
		return 0;
	int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
	if (n < 0) {
		*s += 1;
		return 0xFFFD;
	}
	if (n)
		c &= 0x3F >> n;
	for (int i = 1; i <= n; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s += i;
			return 0xFFFD;
		}
		c = (c << 6) | (p[i] & 0x3F);
	}
	*s += n + 1;
	return c;
}

bool utf8_put(char *out, size_t size, uint32_t cp) {
	size_t len = strlen(out);
	char b[4];
	int n;
	if (cp < 0x80) { b[0] = cp; n = 1; }
	else if (cp < 0x800) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); n = 2; }
	else if (cp < 0x10000) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); n = 3; }
	else { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); n = 4; }
	if (len + n + 1 > size)
		return false;
	memcpy(out + len, b, n);
	out[len + n] = 0;
	return true;
}

void utf8_pop(char *s) {
	size_t len = strlen(s);
	while (len && (s[len - 1] & 0xC0) == 0x80)
		len--;
	if (len)
		len--;
	s[len] = 0;
}

static uint32_t sjis_pair(uint8_t lead, uint8_t trail) {
	int l;
	if (lead >= 0x81 && lead <= 0x9F) l = lead - 0x81;
	else if (lead >= 0xE0 && lead <= 0xFC) l = lead - 0xE0 + 0x1F;
	else return 0;
	if (trail < TRAIL0 || trail >= TRAIL0 + TRAILS)
		return 0;
	return sjis_table[l * TRAILS + (trail - TRAIL0)];
}

/* Shift-JIS if every high byte decodes and at least one lead byte is in
 * 0x81-0x9F (kana, punctuation, common kanji). Latin-1 never uses that
 * range (it's control codes there), so accented titles stay Latin-1. */
static bool looks_sjis(const uint8_t *in, size_t n) {
	bool lead_seen = false;
	for (size_t i = 0; i < n && in[i]; i++) {
		uint8_t c = in[i];
		if (c < 0x80 || (c >= 0xA1 && c <= 0xDF))
			continue;
		if (i + 1 >= n || !sjis_pair(c, in[i + 1]))
			return false;
		if (c <= 0x9F)
			lead_seen = true;
		i++;
	}
	return lead_seen;
}

void game_text_to_utf8(char *out, size_t size, const uint8_t *in, size_t n) {
	if (!size)
		return;
	out[0] = 0;
	bool sjis = looks_sjis(in, n);
	for (size_t i = 0; i < n && in[i]; i++) {
		uint32_t cp = in[i];
		if (sjis && cp >= 0xA1 && cp <= 0xDF) {
			cp = 0xFF61 + (cp - 0xA1);   /* half-width katakana */
		} else if (sjis && cp >= 0x80) {
			cp = sjis_pair(in[i], in[i + 1]);
			i++;
			if (cp == 0x3000)
				cp = ' ';   /* full-width space */
		}
		if (cp == '\n' || cp == '\r')
			cp = ' ';
		if (!utf8_put(out, size, cp))
			break;
	}
	/* no trailing spaces */
	size_t len = strlen(out);
	while (len && out[len - 1] == ' ')
		out[--len] = 0;
}
