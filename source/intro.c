/* intro.c -- see intro.h
 *
 * Timeline (seconds):
 *   0.0 - 0.8  the photo fades in on black (inset, so all of it is on screen);
 *              a slow push toward the spindle
 *   1.4 - 2.2  the PUSH cap lights up
 *   2.2 - 3.4  a disc lifts off the cap, turning to face us and spinning up
 *   3.4 - 4.4  the photo falls away behind it into the sky; the disc fades
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <math.h>
#include "intro.h"
#include "video.h"
#include "disc.h"
#include "sound.h"
#include "logo_tpl.h"

#define CAP_X   443.0f     /* the spindle cap in the photo */
#define CAP_Y   235.0f
#define CAP_RX  66.0f
#define CAP_RY  56.0f
#define INSET   0.86f      /* the photo's size on screen: inside the title-safe area, on black,
                              so a TV's overscan doesn't cut its edges off */
#define T_LIFT  2.2f
#define T_OUT   3.4f
#define T_END   4.4f

static TPLFile tpl;
static GXTexObj photo;
static bool have_photo;
static bool running;
static float start_at;
static bool sound_done;

void intro_init(void) {
	DCFlushRange((void *)logo_tpl, logo_tpl_size);   /* (the GPU reads it from memory) */
	if (TPL_OpenTPLFromMemory(&tpl, (void *)logo_tpl, logo_tpl_size) >= 0 && TPL_GetTexture(&tpl, 0, &photo) >= 0)
		have_photo = true;
}

void intro_start(void) {
	running = true;
	start_at = frame_time;
	sound_done = false;
}

static float now(void) { return frame_time - start_at; }
bool intro_running(void) { return running; }
bool intro_covering(void) { return running && now() < T_OUT; }

void intro_input(u16 down) {
	if (running && now() < T_OUT && (down & (PAD_BUTTON_A | PAD_BUTTON_START)))
		start_at = frame_time - T_OUT;
}

static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static float smooth(float t) { t = clamp01(t); return t * t * (3 - 2 * t); }
static float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }

void intro_draw(void) {
	if (!running)
		return;
	float t = now();
	if (t >= T_END) {
		running = false;
		return;
	}
	float out = smooth(seg(t, T_OUT, T_END));   /* the hand-over to the menu */

	/* black under the photo until the hand-over */
	if (out < 1)
		gfx_rect(view_l, view_t, view_r - view_l, view_b - view_t, rgba(0, 0, 0, (int)(255 * (1 - out))));

	/* the photo, inset and centred, then pushed in toward the cap (faster as it falls away) */
	float push = 1 + 0.04f * smooth(t / T_OUT) + 0.9f * out * out;
	float photo_a = smooth(seg(t, 0, 0.8f)) * (1 - out) * (1 - 0.25f * smooth(seg(t, T_LIFT, T_OUT)));
	float cap_x = 320 + (CAP_X - 320) * INSET, cap_y = 240 + (CAP_Y - 240) * INSET;   /* on screen */
	float k = INSET * push;
	const float m[6] = { k, 0, cap_x + (320 - 320 * INSET - cap_x) * push,
	                     0, k, cap_y + (240 - 240 * INSET - cap_y) * push };
	if (have_photo && photo_a > 0) {
		gfx_transform(m);
		gfx_image(&photo, 0, 0, 640, 480, rgba(255, 255, 255, (int)(255 * photo_a)));
		/* the cap lighting up: a soft indigo light on it */
		float glow = smooth(seg(t, 1.4f, 2.2f)) * (1 - smooth(seg(t, 2.6f, T_OUT)));
		glow *= 0.75f + 0.25f * sinf(t * 7);
		if (glow > 0) {
			gfx_blend_add(true);
			gfx_dot(CAP_X, CAP_Y, CAP_RX * 1.6f, rgba(120, 100, 255, (int)(150 * glow * photo_a)));
			gfx_dot(CAP_X, CAP_Y, CAP_RX * 0.8f, rgba(190, 180, 255, (int)(110 * glow * photo_a)));
			gfx_blend_add(false);
		}
		gfx_identity();
	}

	/* the disc lifting off the cap */
	if (t >= T_LIFT) {
		if (!sound_done) {
			sound_play(SFX_START);
			sound_done = true;
		}
		float lift = smooth(seg(t, T_LIFT, T_OUT + 0.3f));
		/* from the cap's place and shape (on the photo as pushed in) to facing us in the middle */
		float sx = cap_x, sy = cap_y;   /* (the push is centred on the cap: it stays put) */
		float cx = sx + (320 - sx) * lift, cy = sy + (226 - sy) * lift;
		float rx = CAP_RX * k * (1 + 1.8f * lift) * (1 + 0.6f * out);
		float ry = rx * (CAP_RY / CAP_RX + (1 - CAP_RY / CAP_RX) * lift);
		float spin_t = t - T_LIFT;
		float angle = 1.5f * spin_t * spin_t;   /* spinning up */
		float a = smooth(seg(t, T_LIFT, T_LIFT + 0.35f)) * (1 - out);
		disc_draw(cx, cy, rx, ry, angle, a, NULL, NULL, 0.8f * a);
	}
}
