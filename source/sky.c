/* sky.c -- see sky.h
 *
 * The light is a "cheap god ray": a few long, faint wedges fanning down
 * from above the top edge, drawn additively and swaying very slowly.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>
#include <math.h>
#include "sky.h"
#include "video.h"

#define SPECKS 150
#define RAYS   7

typedef struct {
	float x, y;
	float size;      /* radius of the visible dot */
	float speed;     /* px/s down */
	float drift;     /* px/s sideways */
	float sway, sway_f, phase;
	float tw_f, tw_phase;
	float bright;
} Speck;

typedef struct {
	float angle, width, len, speed, phase, bright;
} Ray;

static Speck specks[SPECKS];
static Ray rays[RAYS];

static float frand(float a, float b) {
	return a + (b - a) * (rand() / (float)RAND_MAX);
}

static void speck_new(Speck *s, bool anywhere) {
	/* mostly tiny, a few bigger (closer) ones */
	float k = frand(0, 1);
	k = k * k * k;
	s->size = 0.7f + k * 2.6f;
	s->speed = 5 + s->size * 6 + frand(0, 6);
	s->drift = frand(-3, 3);
	s->sway = frand(2, 16);
	s->sway_f = frand(0.05f, 0.35f);
	s->phase = frand(0, 6.283f);
	s->tw_f = frand(0.3f, 1.6f);
	s->tw_phase = frand(0, 6.283f);
	s->bright = frand(0.35f, 0.95f);
	s->x = frand(view_l - 20, view_r + 20);
	s->y = anywhere ? frand(view_t, view_b) : view_t - frand(4, 40);
}

void sky_init(void) {
	for (int i = 0; i < SPECKS; i++)
		speck_new(&specks[i], true);
	for (int i = 0; i < RAYS; i++) {
		Ray *r = &rays[i];
		r->angle = -0.55f + 1.1f * i / (RAYS - 1) + frand(-0.06f, 0.06f);
		r->width = frand(0.05f, 0.14f);
		r->len = frand(520, 760);
		r->speed = frand(0.05f, 0.12f);
		r->phase = frand(0, 6.283f);
		r->bright = frand(0.5f, 1.0f);
	}
}

void sky_draw(float pulse) {
	float t = frame_time;
	float w = view_r - view_l, h = view_b - view_t;

	/* the night: not black, a deep blue-violet getting more indigo lower down */
	gfx_rect_vgrad(view_l, view_t, w, h * 0.55f, rgba(6, 6, 18, 255), rgba(14, 12, 40, 255));
	gfx_rect_vgrad(view_l, view_t + h * 0.55f, w, h * 0.45f + 1, rgba(14, 12, 40, 255), rgba(30, 22, 78, 255));

	gfx_blend_add(true);
	/* a low glow along the bottom */
	gfx_rect_vgrad(view_l, view_b - h * 0.4f, w, h * 0.4f, rgba(70, 50, 190, 0), rgba(70, 50, 190, 46 + (int)(20 * pulse)));

	/* the rays, from a point above the top edge */
	float ox = 250, oy = view_t - 150;
	for (int i = 0; i < RAYS; i++) {
		Ray *r = &rays[i];
		float a = r->angle + 0.08f * sinf(t * r->speed + r->phase);
		float breathe = 0.6f + 0.4f * sinf(t * r->speed * 1.7f + r->phase * 2);
		int al = (int)((14 + 10 * pulse) * r->bright * breathe);
		Color c0 = rgba(110, 90, 255, al), c1 = rgba(80, 60, 220, 0);
		float a0 = a - r->width / 2, a1 = a + r->width / 2;
		/* (angles from straight down) */
		gfx_tri(ox, oy, ox + sinf(a0) * r->len, oy + cosf(a0) * r->len,
		        ox + sinf(a1) * r->len, oy + cosf(a1) * r->len, c0, c1, c1);
		/* a soft middle line, brighter, so it reads as a beam */
		float aw = r->width * 0.3f;
		gfx_tri(ox, oy, ox + sinf(a - aw) * r->len * 0.8f, oy + cosf(a - aw) * r->len * 0.8f,
		        ox + sinf(a + aw) * r->len * 0.8f, oy + cosf(a + aw) * r->len * 0.8f,
		        rgba(150, 130, 255, al), rgba(100, 80, 240, 0), rgba(100, 80, 240, 0));
	}

	/* the specks */
	for (int i = 0; i < SPECKS; i++) {
		Speck *s = &specks[i];
		s->y += s->speed * frame_dt;
		s->x += s->drift * frame_dt;
		if (s->y > view_b + 10 || s->x < view_l - 40 || s->x > view_r + 40)
			speck_new(s, false);
		float x = s->x + s->sway * sinf(t * s->sway_f * 6.283f + s->phase);
		float tw = 0.65f + 0.35f * sinf(t * s->tw_f * 6.283f + s->tw_phase);
		int a = (int)(255 * s->bright * tw);
		Color c = s->size > 2 ? rgba(200, 190, 255, a) : rgba(225, 228, 255, a);
		gfx_dot(x, s->y, s->size * 1.6f, c);
		if (s->size > 2.2f)   /* the near ones get a faint halo */
			gfx_dot(x, s->y, s->size * 5, rgba(120, 100, 255, a / 6));
	}
	gfx_blend_add(false);
}
