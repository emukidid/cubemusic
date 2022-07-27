/* audio.c -- Handles audio playback through to GC Hardware
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>
#include <aesndlib.h>
#include "vgmstream/streamfile.h"
#include "vgmstream/vgmstream.h"

#define BUFSIZE 4000
#define BUFFER_SIZE (DSP_STREAMBUFFER_SIZE * 64)

static AESNDPB *voice;
static VGMSTREAM *vgmstream;
static sample *vgmbuffer;
static int vgmlength;
static int vgmpos;
static int vgmfadesamples;
static int buffered;
static char buffer[BUFFER_SIZE];
static const char *end_ptr = buffer + BUFFER_SIZE;
static char *write_ptr, *read_ptr;

static void aesnd_callback(AESNDPB *pb, uint32_t state)
{
	if (state == VOICE_STATE_STREAM) {
		if (buffered >= DSP_STREAMBUFFER_SIZE) {
			AESND_SetVoiceBuffer(pb, read_ptr, DSP_STREAMBUFFER_SIZE);
			read_ptr += DSP_STREAMBUFFER_SIZE;
			if (read_ptr >= end_ptr)
				read_ptr = buffer;
			buffered -= DSP_STREAMBUFFER_SIZE;
		}
	}
}

static void reset_buffer(void)
{
	write_ptr = buffer;
	read_ptr = buffer;
	buffered = 0;
	memset(buffer, 0, BUFFER_SIZE);
}

void audio_init() {
	// Initialise & configure any audio logic here, such as audio subsystem/etc
	AESND_Init();
	reset_buffer();
	voice = AESND_AllocateVoice(aesnd_callback);
	printf("Voice: %08X\r\n", (u32)voice);
	AESND_SetVoiceFormat(voice, VOICE_STEREO16);
	AESND_SetVoiceStream(voice, true);
}

void audio_stoptrack() {
	// Stop the currently playing track
	AESND_SetVoiceMute(voice, true);
	AESND_SetVoiceStop(voice, true);
	if(vgmstream) {
		close_vgmstream(vgmstream);
		vgmstream = NULL;
	}
	if(vgmbuffer) {
		free(vgmbuffer);
		vgmbuffer = NULL;
	}
}

int audio_starttrack(char *audiofile) {
    
	if(vgmstream == NULL) {		
		double loop_count = 2.0;
		double fade_seconds = 10.0;
		double fade_delay_seconds = 0.0;
	
		vgmstream = init_vgmstream(audiofile);

		if (!vgmstream) {
			printf("Failed to open file [%s]\r\n", audiofile);
			return -1;
		}
	
		printf("Sample rate: %i channels: %i\r\n", vgmstream->sample_rate, vgmstream->channels);
		AESND_SetVoiceFrequency(voice, vgmstream->sample_rate);
		AESND_SetVoiceLoop(voice, false);
		AESND_SetVoiceMute(voice, false);

		AESND_SetVoiceStop(voice, false);
	   
		vgmbuffer = memalign(32, BUFSIZE*sizeof(sample)*vgmstream->channels);
		printf("vgmbuffer = %08X, size req %i\r\n", (u32)vgmbuffer, BUFSIZE*sizeof(sample)*vgmstream->channels);
		vgmlength = get_vgmstream_play_samples(loop_count,fade_seconds,fade_delay_seconds,vgmstream);
		printf("samples to play: %d (%.4lf seconds)\r\n",vgmlength,(double)vgmlength/vgmstream->sample_rate);
		vgmfadesamples = fade_seconds * vgmstream->sample_rate;
		vgmpos = 0;
    }
	
	return 0;
}

void audio_frame() {
	if(vgmstream != NULL) {
		if (vgmpos < vgmlength) {
			int toget=BUFSIZE;
			if (vgmpos+BUFSIZE>vgmlength) toget=vgmlength-vgmpos;
			
			int length = sizeof(sample)*vgmstream->channels*toget;
			
			// See if we have room for this yet
			if (buffered + length > BUFFER_SIZE) {
				return;
			}
			
			render_vgmstream(vgmbuffer,toget,vgmstream);

			if (vgmstream->loop_flag && vgmfadesamples > 0) {
				int samples_into_fade = vgmpos - (vgmlength - vgmfadesamples);
				if (samples_into_fade + toget > 0) {
					int j,k;
					for (j=0;j<toget;j++,samples_into_fade++) {
						if (samples_into_fade > 0) {
							double fadedness = (double)(vgmfadesamples-samples_into_fade)/vgmfadesamples;
							for (k=0;k<vgmstream->channels;k++) {
								vgmbuffer[j*vgmstream->channels+k] = vgmbuffer[j*vgmstream->channels+k]*fadedness;
							}
						}
					}
				}
			}
			
			/* add to the stream */
			uint32_t level = IRQ_Disable();
			
			char *stream = (char*)vgmbuffer;
			if (buffered + length < BUFFER_SIZE) {
				do {
					int size = MIN(end_ptr - write_ptr, length);
					memcpy(write_ptr, stream, size);
					stream += size;
					length -= size;
					
					write_ptr += size;
					if (write_ptr >= end_ptr)
						write_ptr = buffer;
					buffered += size;
				} while (length > 0);
			}
			IRQ_Restore(level);
			vgmpos+=BUFSIZE;
			
		}
		else {
			audio_stoptrack();
		}
	}
}

int audio_playsfx(FILE *sfx) {

	return 0;
}

void audio_stopsfx(int sfxvoice) {
}

void audio_deinit() {
	// Stop anything that is resident
}

int audio_isplaying() {
	return vgmstream != NULL ? 1 : 0;
}

