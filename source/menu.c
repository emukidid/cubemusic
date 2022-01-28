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

#include "dvd.h"
#include "gfx.h"
#include "IPLFontWrite.h"
#include "draw.h"
#include "gcm_fst.h"
#include "audio.h"
#include "splash.h"

extern const char** getEntries(char *gameId, int *count);

static AudioEntry entries[1024];
 
 // Menu States
 // Wait for SD
 // Wait for Disc
 // Reading
 // Playing
 
enum MENU_STATES {
	SETUP = 0,
	WAIT_FOR_SD,
	WAIT_FOR_DISC,
	READING,
	SELECT_TRACK,
	PLAYING,
	CREDITS,
	ERROR
};

static int menu_state = SETUP;
static int menu_state_started;
static dvddiskid dvd_diskid;
static int read_state;
enum READ_STATES {
	READ_ID,
	READ_ID_STARTED,
	READ_FST,
	DUMP_TRACKS
};
#define READ_SIZE (128*1024)
static char read_buffer[READ_SIZE] __attribute((aligned(32)));
typedef struct {
	int read_num_cur;
	int read_num_total;
	u32  read_offset;
	u32 dump_cur;
	u32 dump_total;
	FILE *write_fp;
} ReadTrackState;
static ReadTrackState readTrackState;

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
static int track_sel;


void menu_init() {
	menu_state = SETUP;
	read_state = 0;
	menu_state_started = 0;
	menu_pos = 0;
	track_sel = 0;
	memset(&readTrackState, 0, sizeof(ReadTrackState));
	memset(&entries, 0, sizeof(AudioEntry) * 1024);
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
			dvd_reset();
			menu_state = READING;
		break;
		case READING:
			// Try to match the tracks to SD, if already dumped, play them
			if(read_state == READ_ID) {
#ifdef _UI_DEBUG
				printf("Reading ID\r\n");
#endif
				read_state = READ_ID_STARTED;
				DVD_ReadID(&dvd_diskid);
				read_state = READ_FST;
			}	
			if(read_state == READ_FST) {
				WriteFont(20, 280 , "Reading FST");
				
				const char **rawNameEntries = getEntries((char*)&dvd_diskid.gamename[0], &readTrackState.read_num_total);
				printf("found %i entries for gamename: [%s]\r\n", readTrackState.read_num_total, (char*)&dvd_diskid.gamename[0]);
				// read the FST for this disc to resolve file offsets now
				int i = 0;
				char *FST = get_fst();
				if(!FST) {
					errorMsg = "Failed to read file system from disc.";
					menu_state = ERROR;
					break;
				}
				memset(entries, 0, sizeof(AudioEntry) * 1024);
				for(i = 0; i < readTrackState.read_num_total; i++) {
					entries[i].filename = rawNameEntries[i];
					get_fst_details(FST, rawNameEntries[i], &entries[i].discOffset, &entries[i].fileSize);
					readTrackState.dump_total += entries[i].fileSize;
					printf("found [%s] at offset %08X, with size %i\r\n", entries[i].filename, entries[i].discOffset, entries[i].fileSize);
					char* filenameOutput = calloc(1, 1024);
					sprintf(filenameOutput, "/cubemusic/%08X_%s", entries[i].discOffset, entries[i].filename);
					strcpy(entries[i].storagePath, filenameOutput);
					free(filenameOutput);
				}
				free(FST);
				if(readTrackState.read_num_total > 0) {
					read_state = DUMP_TRACKS;
				}
				else {
					menu_state = SETUP;
				}
			}
			if(read_state == DUMP_TRACKS) {
				int i = readTrackState.read_num_cur;
				
				// Prepare for the next write
				if(readTrackState.write_fp == NULL) {
					// TODO if exist, use it.
					printf("Opening new file %i of %i [%s]\r\n", readTrackState.read_num_cur, readTrackState.read_num_total, entries[readTrackState.read_num_cur].filename);
					readTrackState.write_fp = fopen(entries[readTrackState.read_num_cur].storagePath, "wb");
					readTrackState.read_offset = entries[readTrackState.read_num_cur].discOffset;
				}
				
				sprintf(txtbuffer, "Dumping %i of %i [%.2f/%.2f KB] [%.2f/%.2f MB] to SD",
									i+1,
									readTrackState.read_num_total,
									(readTrackState.read_offset-entries[i].discOffset)/1024.0f,
									entries[i].fileSize/1024.0f,
									readTrackState.dump_cur/(1024.0f*1024.0f),
									readTrackState.dump_total/(1024.0f*1024.0f));
									
				WriteFont(20, 280 , txtbuffer);
				printf(txtbuffer);
				
				// TODO async this.
				if(readTrackState.read_offset < entries[i].discOffset + entries[i].fileSize) {
					// Read & dump a chunk
					u32 readsize = readTrackState.read_offset + READ_SIZE > entries[i].discOffset + entries[i].fileSize ? (entries[i].discOffset + entries[i].fileSize)-readTrackState.read_offset : READ_SIZE;
					printf("Writing %i bytes from %08X to %s\r\n", readsize, entries[i].discOffset, entries[i].storagePath);
					DVD_Read(read_buffer, entries[i].discOffset, readsize);
					fwrite(read_buffer, 1, readsize, readTrackState.write_fp);
					readTrackState.read_offset+=READ_SIZE;
					readTrackState.dump_cur += readsize;
				}
				else {
					// We're done
					fclose(readTrackState.write_fp);
					readTrackState.write_fp = NULL;
					readTrackState.read_num_cur++;
					
					// See if there's more to dump
					if(readTrackState.read_num_cur >= readTrackState.read_num_total) {
						menu_state = SELECT_TRACK;
					}
				}
				
			}
			/*if(tracks_read) {
				menu_state = PLAYING;
				tracks_read = 0;
			}*/
		break;
		case SELECT_TRACK:
			i = 0;
			for(i = 0; i < readTrackState.read_num_total; i++) {
				sprintf(txtbuffer, "%s%s", i==track_sel?"-> ":"", entries[i].filename);
				WriteFont(20, 200+(i*40) , txtbuffer);
			}
			if(PAD_ButtonsDown(0) & PAD_BUTTON_DOWN)
				track_sel = (track_sel+1 == readTrackState.read_num_total ? 0:track_sel+1);
			if(PAD_ButtonsDown(0) & PAD_BUTTON_UP)
				track_sel = (track_sel == 0 ? readTrackState.read_num_total-1 : track_sel-1);	
			if(PAD_ButtonsDown(0) & PAD_BUTTON_A) {
				menu_state = PLAYING;
				audio_starttrack(entries[track_sel].storagePath);
				printf("Opening [%s]\r\n", entries[track_sel].storagePath);
			}
		break;
		case PLAYING:
			// Show current track, allow user to stop/pause/next/prev/exit
#ifdef _UI_DEBUG
			//printf("Playing\r\n");
#endif
			WriteFont(20, 280 , "Playing");
			if(PAD_ButtonsDown(0) & PAD_BUTTON_B) {
				menu_state = SELECT_TRACK;
				audio_stoptrack();
			}
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
 