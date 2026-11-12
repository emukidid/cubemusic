/* sound.h -- menu sounds (made in the background at start-up, see sfxsynth.c)
 * played on libansnd voices next to the music.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __SOUND_H
#define __SOUND_H

#include <stdbool.h>
#include "sfxsynth.h"

void sound_init(void);       /* after audio_init (it sets up ARAM and libansnd) */
void sound_play(Sfx s);      /* at Settings > Menu sounds volume; nothing if 0 */
void sound_shutdown(void);

#endif
