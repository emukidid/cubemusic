/* text.h -- UTF-8 helpers and Shift-JIS / Latin-1 to UTF-8
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __TEXT_H
#define __TEXT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Next code point of a UTF-8 string (advances *s; 0 at the end).
 * Broken sequences come out as U+FFFD. */
uint32_t utf8_next(const char **s);
/* Appends cp to out (size bytes, kept 0-terminated). Returns false if it didn't fit. */
bool utf8_put(char *out, size_t size, uint32_t cp);
/* Removes the last character of a UTF-8 string. */
void utf8_pop(char *s);

/* Game text (banner titles) to UTF-8: Shift-JIS if it looks like it
 * (Japanese discs), otherwise Latin-1. */
void game_text_to_utf8(char *out, size_t size, const uint8_t *in, size_t in_len);

#endif
