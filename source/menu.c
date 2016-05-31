/* menu.c -- Main menu code handler
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
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>

#include "gfx.h"
#include "IPLFontWrite.h"
#include "draw.h"
 
 // Menu States
 // Wait for SD
 // Wait for Disc
 // Reading
 // Decoding
 // Playing
 
enum MENU_STATES {
	SETUP = 0,
	WAIT_FOR_SD,
	WAIT_FOR_DISC,
	READING,
	DECODING,
	PLAYING,
	CREDITS
};

static int menu_state = SETUP;
static int menu_state_started;

#define MAX_MENU_ENTRY 4
static char *menu_entry[] = {
			"Play from Disc", 
			"Play from SD", 
			"Rip to SD", 
			"Credits"
		};
static int menu_pos;


void menu_init() {
	menu_state = SETUP;
	menu_state_started = 0;
	menu_pos = 0;
}

int menu_frame() {
	// Whenever this is called, update the current menu display
	switch(menu_state) {
		case SETUP:
			// Ask user what they want to do, play from Disc, play from SD, decode from disc to SD
			if(!menu_state_started) {
#ifdef _UI_DEBUG
				printf("Setup Menu\r\n");
#endif
				menu_state_started = 1;
			}
			int i = 0;
			for(i = 0; i < MAX_MENU_ENTRY; i++) {
				sprintf(txtbuffer, "%s%s", i==menu_pos?"-> ":"", menu_entry[i]);
				WriteFont(20, 200+(i*40) , txtbuffer);
			}
			if(PAD_ButtonsDown(0) & PAD_BUTTON_DOWN)
				menu_pos = (menu_pos+1 == MAX_MENU_ENTRY ? 0:menu_pos+1);
			if(PAD_ButtonsDown(0) & PAD_BUTTON_UP)
				menu_pos = (menu_pos == 0 ? MAX_MENU_ENTRY-1 : menu_pos-1);	
		break;
		case WAIT_FOR_SD:
			// Try to init SD, if none, ask user to insert & wait for a button
#ifdef _UI_DEBUG
			printf("Wait for SD\r\n");
#endif
			WriteFont(20, 280 , "Waiting for SD");
		break;
		case WAIT_FOR_DISC:
			// Try to read disc, if none, ask user to insert & wait for a button
#ifdef _UI_DEBUG
			printf("Wait for Disc\r\n");
#endif
			WriteFont(20, 280 , "Waiting for Disc");
		break;
		case READING:
			// Try to match the tracks to SD, if already dumped, play them
#ifdef _UI_DEBUG
			printf("Reading\r\n");
#endif
			WriteFont(20, 280 , "Reading Tracks");
		break;
		case DECODING:
			// Parse from disc, dump to SD
#ifdef _UI_DEBUG
			printf("Decoding\r\n");
#endif
			WriteFont(20, 280 , "Decoding");
		break;
		case PLAYING:
			// Show current track, allow user to stop/pause/next/prev/exit
#ifdef _UI_DEBUG
			printf("Playing\r\n");
#endif
			WriteFont(20, 280 , "Playing");
		break;
	}
	return 1;
}
 