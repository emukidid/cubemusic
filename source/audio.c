/* audio.c -- Handles audio playback through GC Hardware
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>
#include <ogc/aram.h>
#include <ogc/arqueue.h>
#include <ogc/mutex.h>
#include <ansndlib.h>
#include "audio.h"

#define NBUF        14                         /* 3 in libansnd's hands + decode headroom */
#define IN_FLIGHT   3                          /* staged, queued, playing */
#define BUF_FRAMES  8192                       /* ~256 ms at 32 kHz; the ring holds ~3.6 s */
#define BUF_BYTES   (BUF_FRAMES * 2 * 2)       /* up to stereo, 16-bit */

enum { BUF_FREE, BUF_READY, BUF_GIVEN };

AudioSettings audio_settings = { .loop_count = 2, .fade_seconds = 10.0f, .loop_forever = false, .volume = 1.0f };

static u32 aram_stack[64];      /* the music buffers and the menu sounds */
static u32 aram[NBUF];
static u8 *mram[NBUF];
static volatile u32 buf_frames[NBUF];
static volatile u8 buf_state[NBUF];
static volatile int give_idx;     /* next buffer the callback hands over */
static int fill_idx;              /* next buffer the decode thread fills */
static volatile u32 frames_given; /* since the last start/seek */
static volatile u32 ahead[2];     /* frames in the last two hand-overs (staged + queued, not playing yet) */
static volatile u32 underruns;
#define HOT_PEAK    32000        /* -0.2 dBFS */
#define HEADROOM    0.79f        /* -2 dB */
static float gain = 1.0f, gain_target = 1.0f;   /* the track's headroom (fill_one) */
static libstreamfile_t *open_sf;          /* audio_play asked; the decode thread opens it */
static int open_subsong;
static volatile int open_state = AUDIO_IDLE;
static volatile bool active;              /* a track is opening or loaded */
static volatile bool seek_pending;       /* audio_seek asked; the decode thread does it */
static volatile int64_t seek_target;
static volatile float buf_level[NBUF];   /* loudness of each buffer (RMS, 0..1) */

static s32 voice = -1;
static volatile s32 voice_state = ANSND_VOICE_STATE_STOPPED;
static ansnd_pcm_voice_config_t voice_cfg;

static libvgmstream_t *lib;
static int channels;
static int sample_rate;
static int64_t play_samples;
static int64_t base_sample;      /* stream position the counters are relative to */
static bool decode_done;
static bool paused;
static char describe[256];

static mutex_t lock = LWP_MUTEX_NULL;
static lwp_t decode_thread = LWP_THREAD_NULL;
static volatile bool thread_run;

/* ------------------------------------------------------------- callbacks */

static void stream_cb(void *user, ansnd_pcm_data_buffer_t *db) {
	int i = give_idx;
	if (buf_state[i] != BUF_READY)
		return;   /* nothing decoded yet; asked again next DSP frame */
	db->frame_data_ptr = aram[i];
	db->frame_count = buf_frames[i];
	buf_state[i] = BUF_GIVEN;
	frames_given += buf_frames[i];
	ahead[1] = ahead[0];
	ahead[0] = buf_frames[i];
	int done = (i + NBUF - IN_FLIGHT) % NBUF;
	if (buf_state[done] == BUF_GIVEN)
		buf_state[done] = BUF_FREE;
	give_idx = (i + 1) % NBUF;
}

static void voice_cb(void *user, s32 state) {
	voice_state = state;
}

/* --------------------------------------------------------------- decoding */

/* Decodes into buffer fill_idx and moves it to ARAM. Lock held.
 * Returns false when there's nothing left to decode. */
static bool fill_one(void) {
	int i = fill_idx;
	if (decode_done || buf_state[i] != BUF_FREE)
		return false;
	if (libvgmstream_fill(lib, mram[i], BUF_FRAMES) < 0) {
		decode_done = true;
		return false;
	}
	int frames = lib->decoder->buf_samples;
	if (lib->decoder->done)
		decode_done = true;
	if (frames <= 0)
		return false;

	s16 *pcm = (s16 *)mram[i];
	int n = frames * channels;

	/* how hot the decoded audio is (for the headroom below) */
	int peak = 0;
	for (int k = 0; k < n && peak < HOT_PEAK; k++) {
		int v = pcm[k] < 0 ? -pcm[k] : pcm[k];
		if (v > peak)
			peak = v;
	}

	/* Headroom for tracks mastered right up to full scale (F-Zero GX's ADX and others):
	 * the DSP's resampling to 48 kHz overshoots peaks like that and they
	 * clip. The first time a track gets that loud it's turned down a little
	 * (eased in over ~20 ms) and stays down until it ends. */
	if (peak >= HOT_PEAK && gain_target == 1.0f) {
		gain_target = HEADROOM;
		printf("audio: peaks at full scale, playing it %.1f dB quieter\r\n", 20 * log10f(HEADROOM));
	}
	if (gain < 1.0f || gain_target < 1.0f) {
		int ramp = frames < 1024 ? frames : 1024;
		float step = (gain_target - gain) / ramp;
		for (int f = 0; f < frames; f++) {
			if (f < ramp)
				gain += step;
			int g = (int)(gain * 32768);
			for (int c = 0; c < channels; c++) {
				s16 *p = &pcm[f * channels + c];
				*p = (s16)((*p * g) >> 15);
			}
		}
		gain = gain_target;
	}

	/* loudness, for the player's visuals (every 4th sample is plenty) */
	float acc = 0;
	int cnt = 0;
	for (int k = 0; k < n; k += 4, cnt++)
		acc += (float)pcm[k] * pcm[k];
	buf_level[i] = cnt ? sqrtf(acc / cnt) / 32768.0f : 0;

	u32 bytes = (frames * channels * 2 + 31) & ~31;
	DCFlushRange(mram[i], bytes);
	ARQRequest req;
	ARQ_PostRequest(&req, 0x4D55, ARQ_MRAMTOARAM, ARQ_PRIO_HI, aram[i], MEM_VIRTUAL_TO_PHYSICAL(mram[i]), bytes);
	buf_frames[i] = frames;
	buf_state[i] = BUF_READY;
	fill_idx = (i + 1) % NBUF;
	return true;
}

/* Starts the voice on ready buffer i (it goes in the config; the ones after
 * it are handed over by the callback). Lock held. */
static void launch_from(int i) {
	memset(&voice_cfg, 0, sizeof(voice_cfg));
	voice_cfg.samplerate = sample_rate;
	voice_cfg.format = ANSND_VOICE_PCM_FORMAT_SIGNED_16_PCM;
	voice_cfg.channels = channels;
	voice_cfg.pitch = 1.0f;
	voice_cfg.left_volume = audio_settings.volume;
	voice_cfg.right_volume = audio_settings.volume;
	voice_cfg.frame_data_ptr = aram[i];
	voice_cfg.frame_count = buf_frames[i];
	voice_cfg.voice_callback = voice_cb;
	voice_cfg.stream_callback = stream_cb;
	buf_state[i] = BUF_GIVEN;
	frames_given += buf_frames[i];
	ahead[0] = ahead[1] = 0;
	give_idx = (i + 1) % NBUF;

	s32 err = ansnd_configure_pcm_voice(voice, &voice_cfg);
	if (err < 0) {
		printf("audio: configure failed: %s\r\n", ansnd_get_error_string(err));
		return;
	}
	voice_state = ANSND_VOICE_STATE_RUNNING;
	err = ansnd_start_voice(voice);
	if (err < 0)
		printf("audio: start failed: %s\r\n", ansnd_get_error_string(err));
	if (paused)
		ansnd_pause_voice(voice);
}

/* Starts playing from the decoder's current position (track start, seek).
 * Decodes a few buffers first (the decode loop fills the rest). Lock held. */
static void start_voice(void) {
	ansnd_stop_voice(voice);
	for (int i = 0; i < NBUF; i++) {
		buf_state[i] = BUF_FREE;
		buf_frames[i] = 0;
	}
	fill_idx = 0;
	give_idx = 0;
	frames_given = 0;
	base_sample = libvgmstream_get_play_position(lib);

	for (int i = 0; i < IN_FLIGHT + 1 && fill_one(); i++)
		;
	if (buf_state[0] == BUF_READY)
		launch_from(0);
}

/* The voice played everything it was given before decoding caught up
 * (e.g. a slow SD card). Everything handed over has played; carry on from
 * the next decoded buffer, so nothing is skipped (just a gap). Lock held. */
static bool resume_voice(void) {
	for (int i = 0; i < NBUF; i++)
		if (buf_state[i] == BUF_GIVEN)
			buf_state[i] = BUF_FREE;
	if (buf_state[give_idx] != BUF_READY)
		return false;   /* wait for the decoder */
	underruns++;
	printf("audio: underrun %u, resuming\r\n", underruns);
	launch_from(give_idx);
	return true;
}

static bool buffers_left(void) {
	return buf_state[give_idx] == BUF_READY;
}

/* Opens the track audio_play was given (vgmstream reads its headers, which
 * can take a moment from the disc) and starts it. Lock held. */
static void open_pending(void) {
	libstreamfile_t *sf = open_sf;
	open_sf = NULL;
	libvgmstream_config_t cfg = {0};
	cfg.loop_count = audio_settings.loop_count;
	cfg.fade_time = audio_settings.fade_seconds;
	cfg.allow_play_forever = true;
	cfg.play_forever = audio_settings.loop_forever;
	cfg.auto_downmix_channels = 2;
	cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;

	lib = libvgmstream_create(sf, open_subsong, &cfg);
	libstreamfile_close(sf);   /* (vgmstream reopens what it needs) */
	if (!lib) {
		active = false;
		open_state = AUDIO_FAILED;
		return;
	}
	channels = lib->format->channels;
	sample_rate = lib->format->sample_rate;
	play_samples = lib->format->play_forever ? 0 : lib->format->play_samples;
	snprintf(describe, sizeof(describe), "%s, %d Hz, %dch", lib->format->codec_name, sample_rate,
	         lib->format->input_channels ? lib->format->input_channels : channels);
	printf("audio: %s [%s] %lld samples\r\n", describe, lib->format->meta_name, play_samples);
	start_voice();
	open_state = AUDIO_PLAYING;
}

static void *decode_main(void *arg) {
	while (thread_run) {
		bool worked = false;
		LWP_MutexLock(lock);
		if (open_sf) {
			open_pending();
			worked = true;
		} else if (lib && seek_pending) {
			/* (seeking decodes forward from the start or loop, which can take a
			 * while: done here so the UI keeps running) */
			seek_pending = false;
			ansnd_stop_voice(voice);
			libvgmstream_seek(lib, seek_target);
			decode_done = false;
			start_voice();
			worked = true;
		} else if (lib) {
			if (voice_state == ANSND_VOICE_STATE_FINISHED && (!decode_done || buffers_left()))
				worked = resume_voice();
			worked |= fill_one();
		}
		LWP_MutexUnlock(lock);
		if (!worked)
			usleep(4000);
	}
	return NULL;
}

/* ------------------------------------------------------------------- API */

void audio_init(void) {
	AR_Init(aram_stack, 64);
	ARQ_Init();
	ansnd_initialize();
	for (int i = 0; i < NBUF; i++) {
		aram[i] = AR_Alloc(BUF_BYTES);
		mram[i] = memalign(32, BUF_BYTES);
	}
	voice = ansnd_allocate_voice();
	if (voice < 0)
		printf("audio: no voice: %s\r\n", ansnd_get_error_string(voice));
	LWP_MutexInit(&lock, false);
	thread_run = true;
	/* above the UI loop so drawing can't starve the audio */
	LWP_CreateThread(&decode_thread, decode_main, NULL, NULL, 64 * 1024, 80);
}

void audio_deinit(void) {
	audio_stop();
	thread_run = false;
	if (decode_thread != LWP_THREAD_NULL)
		LWP_JoinThread(decode_thread, NULL);
	decode_thread = LWP_THREAD_NULL;
	if (voice >= 0)
		ansnd_deallocate_voice(voice);
	voice = -1;
	ansnd_uninitialize();
}

static void close_locked(void) {
	if (open_sf)
		libstreamfile_close(open_sf);
	open_sf = NULL;
	open_state = AUDIO_IDLE;
	active = false;
	if (voice >= 0)
		ansnd_stop_voice(voice);
	voice_state = ANSND_VOICE_STATE_STOPPED;
	if (lib)
		libvgmstream_free(lib);
	lib = NULL;
	seek_pending = false;
	describe[0] = 0;
}

void audio_play(libstreamfile_t *sf, int subsong) {
	LWP_MutexLock(lock);
	close_locked();
	paused = false;
	decode_done = false;
	gain = gain_target = 1.0f;
	sample_rate = 0;
	play_samples = 0;
	if (sf) {
		open_sf = sf;
		open_subsong = subsong;
		open_state = AUDIO_OPENING;
		active = true;
	} else {
		open_state = AUDIO_FAILED;
	}
	LWP_MutexUnlock(lock);
}

int audio_state(void) {
	return open_state;
}

bool audio_audible(void) {
	return active && !paused;
}

void audio_stop(void) {
	LWP_MutexLock(lock);
	close_locked();
	LWP_MutexUnlock(lock);
}

void audio_set_paused(bool p) {
	LWP_MutexLock(lock);
	if ((lib || open_sf) && p != paused) {
		paused = p;
		if (lib && p)
			ansnd_pause_voice(voice);
		else if (lib)
			ansnd_unpause_voice(voice);
	}
	LWP_MutexUnlock(lock);
}

/* Doesn't wait: the decode thread seeks (see decode_main). Until it has,
 * audio_position() reports the target, so presses in a row add up. */
void audio_seek(float seconds) {
	if (!lib || !sample_rate)
		return;
	int64_t s = (int64_t)(seconds * sample_rate);
	if (s < 0)
		s = 0;
	if (play_samples > 0 && s > play_samples)
		s = play_samples;
	seek_target = s;
	seek_pending = true;
}

void audio_set_volume(float v) {
	audio_settings.volume = v;
	if (voice >= 0)
		ansnd_set_voice_volume(voice, v, v);
}

bool audio_is_playing(void) {
	/* finished = played to the end (nothing left to decode or play) */
	if (open_state == AUDIO_OPENING)
		return true;
	if (!lib)
		return false;
	if (!seek_pending && decode_done && voice_state == ANSND_VOICE_STATE_FINISHED && !buffers_left()) {
		audio_stop();
		return false;
	}
	return true;
}

bool audio_is_paused(void) {
	return active && paused;
}

float audio_position(void) {
	if (!lib || !sample_rate)
		return 0;
	if (seek_pending)
		return (float)seek_target / sample_rate;
	/* everything handed over except the two buffers not playing yet
	 * (accurate to within the buffer that is playing, ~256 ms at 32 kHz) */
	u32 played = frames_given - ahead[0] - ahead[1];
	return (float)(base_sample + played) / sample_rate;
}

float audio_duration(void) {
	return sample_rate ? (float)play_samples / sample_rate : 0;
}

float audio_level(void) {
	if (!lib || paused)
		return 0;
	return buf_level[(give_idx + NBUF - IN_FLIGHT) % NBUF];   /* the one playing (i-2) */
}

const char *audio_describe(void) {
	return describe;
}

const libvgmstream_format_t *audio_format(void) {
	return lib ? lib->format : NULL;
}
