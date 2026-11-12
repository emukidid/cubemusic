/* audio.h
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef ___AUDIO_H_
#define ___AUDIO_H_

#include <stdbool.h>
#include "libvgmstream.h"

typedef struct {
	int loop_count;        /* times a looping track repeats before fading (default 2) */
	float fade_seconds;    /* fade-out after the last loop (default 10) */
	bool loop_forever;     /* ignore loop_count: loop until stopped */
	float volume;          /* 0..1 */
} AudioSettings;

extern AudioSettings audio_settings;

void audio_init(void);
void audio_deinit(void);

/* Starts a track without waiting: the decode thread opens it (vgmstream reads
 * its headers) and starts playing. Takes the stream file and closes it when
 * it's done with it (sf may be NULL: that fails). audio_state() says how it went. */
void audio_play(libstreamfile_t *sf, int subsong);   /* subsong: 0 for the file's first (or only) stream */

enum { AUDIO_IDLE, AUDIO_OPENING, AUDIO_PLAYING, AUDIO_FAILED };
int audio_state(void);          /* AUDIO_FAILED: vgmstream couldn't play the last audio_play */
bool audio_audible(void);       /* a track is opening or playing, and not paused */
void audio_stop(void);
void audio_set_paused(bool paused);
void audio_seek(float seconds);
void audio_set_volume(float volume);

bool audio_is_playing(void);   /* true while a track is loaded (paused or not) */
bool audio_is_paused(void);
float audio_position(void);    /* seconds */
float audio_duration(void);    /* seconds, after loops and fade; 0 if it loops forever */

/* Format details of the loaded track ("Nintendo DSP 4-bit ADPCM, 32000 Hz, 2ch") */
float audio_level(void);       /* loudness of what's playing now, 0..1 */
const char *audio_describe(void);

/* vgmstream's view of the loaded track (NULL if none), for the track info cache. */
const libvgmstream_format_t *audio_format(void);

#endif
