/* sound.c -- see sound.h. Adapted from GCMM exFAT's sound.c (MIT; AESND
 * there, libansnd here).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/aram.h>
#include <ogc/arqueue.h>
#include <ogc/lwp_watchdog.h>
#include <ansndlib.h>
#include "sound.h"
#include "audio.h"
#include "settings.h"

#define VOICES 4

static u32 aram_at[SFX_COUNT];
static u32 frames[SFX_COUNT];
static volatile bool ready[SFX_COUNT];
static s32 voice[VOICES] = { -1, -1, -1, -1 };
static u64 busy_until[VOICES];
static int next_voice;
static lwp_t synth_thread = LWP_THREAD_NULL;
static volatile bool quit;

static void synth(Sfx id) {
	int n = sfx_length(id);
	if (n <= 0)
		return;
	u32 bytes = ((u32)n * 2 + 31) & ~31u;
	s16 *buf = memalign(32, bytes);
	if (!buf)
		return;
	memset(buf, 0, bytes);
	if (sfx_render(id, buf, n)) {
		u32 at = AR_Alloc(bytes);
		if (at) {
			DCFlushRange(buf, bytes);
			ARQRequest req;
			ARQ_PostRequest(&req, 0x5346, ARQ_MRAMTOARAM, ARQ_PRIO_LO, at, MEM_VIRTUAL_TO_PHYSICAL(buf), bytes);
			aram_at[id] = at;
			frames[id] = n;
			ready[id] = true;
		}
	}
	free(buf);
}

/* the opening's first, then the ones used most */
static const Sfx order[] = {
	SFX_START, SFX_HOVER, SFX_HOVER2, SFX_HOVER3, SFX_SELECT, SFX_BACK, SFX_CONFIRM, SFX_CANCEL, SFX_ERROR,
	SFX_KEY, SFX_LOADED, SFX_OPEN, SFX_CLOSE,
};

static void *synth_main(void *arg) {
	for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]) && !quit; i++)
		synth(order[i]);
	return NULL;
}

void sound_init(void) {
	for (int i = 0; i < VOICES; i++)
		voice[i] = ansnd_allocate_voice();
	LWP_CreateThread(&synth_thread, synth_main, NULL, NULL, 32 * 1024, 40);
}

void sound_shutdown(void) {
	quit = true;
	if (synth_thread != LWP_THREAD_NULL)
		LWP_JoinThread(synth_thread, NULL);
	synth_thread = LWP_THREAD_NULL;
	for (int i = 0; i < VOICES; i++)
		if (voice[i] >= 0)
			ansnd_stop_voice(voice[i]);
}

void sound_play(Sfx s) {
	/* moves take turns between three sounds, like the system menu */
	static int move_turn;
	if (s == SFX_HOVER) {
		s = (Sfx)(SFX_HOVER + move_turn);
		move_turn = (move_turn + 1) % 3;
	}
	/* not over the music: it's what you're here to listen to */
	if (audio_audible())
		return;
	if (settings.menu_volume <= 0 || s < 0 || s >= SFX_COUNT || !ready[s])
		return;
	u64 now = gettime();
	int v = -1;
	for (int k = 0; k < VOICES && v < 0; k++) {
		int i = (next_voice + k) % VOICES;
		if (voice[i] >= 0 && now >= busy_until[i])
			v = i;
	}
	if (v < 0)
		v = next_voice;   /* all busy: cut the oldest */
	if (voice[v] < 0)
		return;
	next_voice = (v + 1) % VOICES;

	float vol = settings.menu_volume / (float)VOLUME_MAX;
	ansnd_pcm_voice_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.samplerate = SFX_RATE;
	cfg.format = ANSND_VOICE_PCM_FORMAT_SIGNED_16_PCM;
	cfg.channels = 1;
	cfg.pitch = 1.0f;
	cfg.left_volume = vol;
	cfg.right_volume = vol;
	cfg.frame_data_ptr = aram_at[s];
	cfg.frame_count = frames[s];
	ansnd_stop_voice(voice[v]);
	if (ansnd_configure_pcm_voice(voice[v], &cfg) < 0)
		return;
	ansnd_start_voice(voice[v]);
	busy_until[v] = now + millisecs_to_ticks(frames[s] * 1000 / SFX_RATE + 30);
}
