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
#include "player.h"
#include "enemy.h"
#include "draw.h"

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
				draw_splash_fade(0);	// TODO actual fade %
				if(diff_msec(splashtime, gettime()) >=8000) {
					current_state = MENU;
					current_state_started = 0;
				}
			}
			else {
				draw_splash();
			}
		}
		break;
		case MENU:
		{
			if(!current_state_started) {
				printf("In Menu\n");
				current_state_started = 1;
			}
		}
		break;
		case LEVEL_SELECT:
		{
		
		}
		break;
		case IN_LEVEL:
		{
		
		}
		break;
		case HI_SCORES:
		{
		
		}
		break;
		case PAUSED:
		{
		
		}
		break;
		case CREDITS:
		{
		
		}
		break;
		default:
			printf("Unknown state!!\r\n");
			break;
	}
	// Update everything
	enemy_update();
	player_update();
}

// Manage what level

// Manage current score

// Manage number of lives? Or stick that in player.c

