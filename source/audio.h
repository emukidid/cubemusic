/* audio.h
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef ___AUDIO_H_
#define ___AUDIO_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>

void audio_init();
void audio_frame();
int audio_starttrack(char *audiotrack);
void audio_stoptrack();
int audio_playsfx(FILE *sfx);
void audio_stopsfx(int sfxvoice);
void audio_deinit();
#endif
