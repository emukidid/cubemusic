/* disc.h -- a GameCube disc, label side up, that can spin
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __DISC_H
#define __DISC_H

#include <gccore.h>
#include "video.h"

/* centred on cx,cy with radii rx,ry (ry < rx tips it back), turned by angle
 * (radians). banner: the game's banner image printed on the label (or NULL);
 * title: printed under the hole (or NULL). glow 0..1 lights the edge. */
void disc_draw(float cx, float cy, float rx, float ry, float angle, float alpha,
               GXTexObj *banner, const char *title, float glow);

#endif
