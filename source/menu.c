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
	CREDITS,
	ERROR
};

static int menu_state = SETUP;
static int menu_state_started;
static int disc_initialising;
static int disc_initialised;
static dvdcmdblk dvd_block;
static dvddiskid dvd_diskid;
static int read_state;
enum READ_STATES {
	READ_ID,
	READ_ID_STARTED,
	READ_FST,
	READ_TRACKS
};
static char *errorMsg;

#define MAX_MENU_ENTRY 4
#define MENU_PLAYDISC 0
#define MENU_PLAYSD 1
#define MENU_RIPTOSD 2
#define MENU_CREDITS 3
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

static void dvd_mount_cb(s32 result,dvdcmdblk *block) {
	disc_initialised = 1;
}

static void dvd_diskid_cb(s32 result, dvdcmdblk *block) {
	read_state = READ_FST;
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
			if(PAD_ButtonsDown(0) & PAD_BUTTON_A) {
				switch(menu_pos) {
					case MENU_PLAYDISC:
						menu_state = WAIT_FOR_DISC;
						break;
					case MENU_PLAYSD:
					case MENU_RIPTOSD:
						break;
					case MENU_CREDITS:
						menu_state = CREDITS;
						break;
				}
			}
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
			WriteFont(20, 280 , "Resetting Disc Drive ...");
			if(!disc_initialising) {
				disc_initialising = 1;
#ifdef _UI_DEBUG
				printf("Resetting Disc Drive\r\n");
#endif
				DVD_MountAsync(&dvd_block, dvd_mount_cb);
			}
			if(disc_initialised==1) {
				disc_initialising = 0;
				menu_state = READING;
			}
		break;
		case READING:
			// Try to match the tracks to SD, if already dumped, play them
			if(read_state == READ_ID) {
#ifdef _UI_DEBUG
				printf("Reading ID\r\n");
#endif
				read_state = READ_ID_STARTED;
				DVD_ReadDiskID(&dvd_block, &dvd_diskid, dvd_diskid_cb);
			}	
			if(read_state == READ_FST) {
				WriteFont(20, 280 , "Reading FST");
				
				int numEntries = 0;
				const char **entries = getEntries((char*)&dvd_diskid.gamename[0], &numEntries);
				printf("found %i entries\r\n", numEntries);
				int i = 0;
				for(i = 0; i < numEntries; i++) {
					printf("found [%s]\r\n", entries[i]);
				}
				read_state = READ_TRACKS;
			}
			if(read_state == READ_TRACKS) {
				
			}
			/*if(tracks_read) {
				menu_state = PLAYING;
				tracks_read = 0;
			}*/
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
		case CREDITS:
			// Credits scroller
#ifdef _UI_DEBUG
			printf("Credits\r\n");
#endif
			WriteFont(20, 280 , "Credits");
		break;
		case ERROR:
			WriteFont(20, 280, errorMsg);
			if(PAD_ButtonsDown(0) & (PAD_BUTTON_A|PAD_BUTTON_B))
				menu_state = SETUP;
		break;
	}
	return 1;
}
 