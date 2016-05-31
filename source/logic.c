/* logic.c -- Handles program flow/logic
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
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include "logic.h"
#include "draw.h"
#include "menu.h"
#include "splash.h"

static int current_state;
static int current_state_started;

static u64 splashtime = 0;

void logic_init() {
	current_state = SPLASH;
	current_state_started = 0;
}

void logic_update() {

	switch(current_state) {
		case SPLASH:
		{
			if(!current_state_started) {
				printf("Splash Screen\r\n");
				current_state_started = 1;
				splashtime = gettime();
			}

			if(diff_msec(splashtime, gettime()) >=5000) {
				// commence fadeout for 3 more seconds
				splash_fade((int)(((diff_msec(splashtime, gettime())-5000)/(float)5000)*100));
				if(diff_msec(splashtime, gettime()) >=10000) {
					current_state = MENU;
					current_state_started = 0;
				}
			}
			else {
				splash_draw();
			}
		}
		break;
		case MENU:
		{
			if(!current_state_started) {
				printf("In Menu\n");
				menu_init();
				current_state_started = 1;
			}
			if(!menu_frame()) {
				exit(1);
			}
		}
		break;
		break;
		default:
			printf("Unknown state!!\r\n");
			break;
	}
	// Update everything
}

