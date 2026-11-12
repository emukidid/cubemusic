/* menu.h -- the screens
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef MENU_H_
#define MENU_H_

#include <stdbool.h>

void menu_init(bool sd_present);
void menu_frame(void);   /* input, logic and drawing for one frame */

#endif
