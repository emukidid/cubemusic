/* sky.h -- the background: a night sky with GameCube-indigo light and
 * small specks drifting down
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __SKY_H
#define __SKY_H

void sky_init(void);
/* pulse: 0..1, a gentle brightening of the light (the music's loudness) */
void sky_draw(float pulse);

#endif
