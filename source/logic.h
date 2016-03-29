#ifndef LOGIC_H_
#define LOGIC_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

// What is being displayed currently?
enum playing_state {
	SPLASH=0,
	MENU,
	LEVEL_SELECT,
	IN_LEVEL,
	HI_SCORES,
	PAUSED,
	CREDITS
};

void logic_init();
void logic_update();
#endif
