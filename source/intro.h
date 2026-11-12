/* intro.h -- the opening: the drive photo (textures/logo.bmp), the spindle
 * lighting up and a disc lifting off it into the night sky. A or START skips.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __INTRO_H
#define __INTRO_H

#include <gccore.h>
#include <stdbool.h>

void intro_init(void);
void intro_start(void);
bool intro_running(void);     /* still drawing (over the menu, as it fades) */
bool intro_covering(void);    /* hides the whole screen: don't draw or take input */
void intro_input(u16 down);
void intro_draw(void);        /* after the menu's drawing */

#endif
