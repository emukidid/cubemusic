/* font.h -- text with FreeType (M PLUS Rounded 1c, Latin and Japanese)
 *
 * Glyphs are rendered when first used and kept in one I8 texture. Sizes are
 * pixel heights in the 640x480 layout. Positions are the baseline (font_draw)
 * or the vertical middle of the capitals (font_mid), which is what lines up
 * text with icons and rows.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __FONT_H
#define __FONT_H

#include <stdbool.h>
#include "video.h"

enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

void font_init(void);
void font_frame(void);   /* video_begin: starts the glyph cache over if it filled up */

float font_width(int size, const char *s);
/* Draws s with its baseline at y. maxw > 0 cuts it short with "..." to fit.
 * Returns the width drawn. */
float font_draw(float x, float y, int size, Color c, int align, float maxw, const char *s);
/* the same with the capitals' middle at y */
float font_mid(float x, float y, int size, Color c, int align, float maxw, const char *s);
/* word-wrapped into up to max_lines lines of maxw (the last one cut short);
 * returns the number of lines drawn */
int font_wrap(float x, float y, int size, float line_h, Color c, int align, float maxw, int max_lines, const char *s);
int font_wrap_count(int size, float maxw, int max_lines, const char *s);

#endif
