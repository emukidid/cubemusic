/* keyboard.h -- an on-screen keyboard for names
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include <gccore.h>
#include <stdbool.h>

enum { KB_OPEN, KB_DONE, KB_CANCEL };

/* default_text: what X puts back (the name it would have without a custom one) */
void kb_open(const char *title, const char *text, const char *default_text, int max_bytes);
/* input and drawing for a frame; dirs = auto-repeated control pad directions,
 * held = buttons held (for L/R) */
int kb_frame(u16 down, u16 dirs, u16 held);
const char *kb_text(void);

#endif
