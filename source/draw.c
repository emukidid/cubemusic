/* draw.c -- Basic drawing routines
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "draw.h"
#include "gfx.h"
#include "IPLFontWrite.h"

void draw_init() {
	// Initialise & configure any draw logic here, such as video subsystem/etc
}

void draw_frame_begin() {
	DrawFrameStart();
}

void draw_frame_end() {
	DrawFrameFinish();
}

void draw_deinit() {
	// Stop anything that is resident
}

void draw_splash() {
	WriteFont(10, 400 , "Splash");
}

void draw_splash_fade(int fadepercent) {
	sprintf(txtbuffer, "Splash fade %i", fadepercent);
	WriteFont(10, 400 , txtbuffer);
}

void draw_menu() {
	WriteFont(10, 400 , "Menu");
}
