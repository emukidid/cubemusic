/* video.h -- the screen: video mode, GX set-up and 2D drawing
 *
 * Everything is drawn in a 640x480 layout. The projection widens what's
 * visible for 16:9 (TVs that stretch the picture) and pulls in from the
 * edges for overscan, so screens never deal with either: view_l..view_r and
 * view_t..view_b give the visible part of the layout, for things that
 * should reach the edges (the sky) or sit in the corners.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __VIDEO_H
#define __VIDEO_H

#include <gccore.h>
#include <stdbool.h>

typedef struct { u8 r, g, b, a; } Color;

static inline Color rgba(int r, int g, int b, int a) { return (Color){ r, g, b, a }; }
Color col_alpha(Color c, float k);           /* alpha scaled by k (0..1) */
Color col_mix(Color a, Color b, float t);    /* a..b */

extern float view_l, view_t, view_r, view_b;
extern float frame_time;   /* seconds since start */
extern float frame_dt;     /* seconds since the last frame (capped) */
extern float gfx_fade;     /* every colour's alpha is multiplied by this (default 1) */

void video_init(bool force_interlaced);
/* picks the video mode again from the settings (at the next frame) */
void video_settings_changed(void);
void video_begin(void);    /* start of a frame */
void video_end(void);      /* shows it and waits for the next one */
void video_shutdown(void); /* black screen, GX idle (before launching another program) */

/* ------------------------------------------------------------- drawing */

void gfx_blend_add(bool add);   /* additive blending (glows) instead of normal */
/* a clip rectangle in layout coordinates; gfx_unclip() for the whole screen */
void gfx_clip(float x, float y, float w, float h);
void gfx_unclip(void);
/* affine transform for what's drawn next: x' = m[0]x + m[1]y + m[2], y' = m[3]x + m[4]y + m[5] */
void gfx_transform(const float m[6]);
void gfx_identity(void);
void affine_mul(float out[6], const float a[6], const float b[6]);   /* out = a after b */

void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom);
void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right);
void gfx_rrect(float x, float y, float w, float h, float r, Color c);
void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom);
void gfx_rrect_line(float x, float y, float w, float h, float r, float thick, Color c);
/* a soft glow around a rounded rectangle, fading out over size */
void gfx_rrect_glow(float x, float y, float w, float h, float r, float size, Color c);
void gfx_circle(float cx, float cy, float r, Color c);
void gfx_ring(float cx, float cy, float r0, float r1, Color inner, Color outer);
/* part of a ring, angles in radians, 0 = right, clockwise (y points down) */
void gfx_arc(float cx, float cy, float r0, float r1, float a0, float a1, Color c);
void gfx_line(float x0, float y0, float x1, float y1, float thick, Color c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c0, Color c1, Color c2);
void gfx_quad(const float xy[8], const Color c[4]);
/* a soft round dot (stars, glows), r = radius of the visible part */
void gfx_dot(float cx, float cy, float r, Color c);

void gfx_tex_init(GXTexObj *t, void *data, int w, int h, u8 fmt, bool clamp);
void gfx_image(GXTexObj *t, float x, float y, float w, float h, Color tint);
void gfx_image_part(GXTexObj *t, float x, float y, float w, float h, float s0, float t0, float s1, float t1, Color tint);

/* low level, for font.c */
void gfx_mode_color(void);
void gfx_mode_tex(GXTexObj *t);
void gfx_mode_alpha(GXTexObj *t);   /* I8 texture as the alpha of the vertex colour */
static inline void gfx_v(float x, float y, Color c, float s, float t) {
	GX_Position2f32(x, y);
	GX_Color4u8(c.r, c.g, c.b, (u8)(c.a * gfx_fade));
	GX_TexCoord2f32(s, t);
}

#endif
