/* main.c -- CubeMusic main entry point.
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gccore.h>
#include <fat.h>

#include "video.h"
#include "font.h"
#include "settings.h"
#include "audio.h"
#include "sound.h"
#include "dvd.h"
#include "backup.h"
#include "ui.h"
#include "menu.h"

int main(int argc, char **argv) {
	VIDEO_Init();
	PAD_Init();
	*(volatile unsigned long *)0xcc00643c = 0x00000000;   /* allow the 32 MHz EXI bus */

	/* L held at start: ignore the component cable (480i) */
	bool no_progressive = false;
	for (int i = 0; i < 100; i++) {
		if (PAD_ScanPads() > 0) {
			no_progressive = PAD_ButtonsHeld(0) & PAD_TRIGGER_L;
			break;
		}
		usleep(1000);
	}

	if (usb_isgeckoalive(1)) {
		usb_flush(1);
		CON_EnableGecko(1, 1);
		printf("Enabled Console over USB Gecko\r\n");
	}

	bool sd = fatInitDefault();
	if (sd) {
		mkdir(CUBEMUSIC_DIR, 0777);
		settings_load();
	}

	video_init(no_progressive);
	font_init();
	dvd_init();
	audio_init();
	settings_apply_audio();
	sound_init();
	ui_init(sd);
	menu_init(sd);

	while (1) {
		video_begin();
		menu_frame();
		video_end();
	}
	return 0;
}
