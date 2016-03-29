#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "draw.h"
#include "FrameBufferMagic.h"
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
	WriteFont(10, 400 , "Splash fadeout");
}