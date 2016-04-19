/* logic.h
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
 
#ifndef LOGIC_H_
#define LOGIC_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

// What is being displayed currently?
enum playing_state {
	SPLASH=0,
	MENU
};

void logic_init();
void logic_update();
#endif
