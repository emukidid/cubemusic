/* draw.h
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
 
#ifndef DRAW_H_
#define DRAW_H_

void draw_init();
void draw_frame_begin();
void draw_frame_end();
void draw_deinit();
void draw_splash();
void draw_splash_fade(int fadepercent);
void draw_menu();

#endif
