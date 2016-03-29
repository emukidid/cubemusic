#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>

void audio_init() {
	// Initialise & configure any audio logic here, such as audio subsystem/etc
}

int audio_starttrack(FILE *audiotrack) {
	return 0;
}

void audio_stoptrack(int trackvoice) {
	// Stop the currently playing track
}

int audio_playsfx(FILE *sfx) {

	return 0;
}

void audio_stopsfx(int sfxvoice) {
}

void audio_deinit() {
	// Stop anything that is resident
}
