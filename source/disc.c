/* disc.c -- see disc.h
 *
 * Drawn in the disc's own coordinates (radius 1) through a transform, so
 * everything printed on the label turns with it. The shine stays put, as a
 * reflection would.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <math.h>
#include "disc.h"
#include "font.h"

#define HOLE   0.17f
#define HUB    0.31f
#define LABEL  0.955f
#define SEGS   64

static void sheen(float light) {
	gfx_mode_color();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (SEGS + 1));
	for (int i = 0; i <= SEGS; i++) {
		float a = i * 2 * (float)M_PI / SEGS;
		float k1 = cosf(a - light), k2 = cosf(a - light - (float)M_PI);
		k1 = k1 > 0 ? powf(k1, 5) : 0;
		k2 = k2 > 0 ? powf(k2, 5) * 0.45f : 0;
		Color c = rgba(255, 255, 255, (int)(52 * (k1 + k2)));
		float ca = cosf(a), sa = sinf(a);
		gfx_v(ca * LABEL, sa * LABEL, col_alpha(c, 0.55f), 0, 0);
		gfx_v(ca * HUB, sa * HUB, c, 0, 0);
	}
	GX_End();
}

void disc_draw(float cx, float cy, float rx, float ry, float angle, float alpha,
               GXTexObj *banner, const char *title, float glow) {
	float old_fade = gfx_fade;
	gfx_fade *= alpha;
	float ca = cosf(angle), sa = sinf(angle);
	const float still[6] = { rx, 0, cx, 0, ry, cy };
	const float turned[6] = { rx * ca, -rx * sa, cx, ry * sa, ry * ca, cy };
	bool small = rx < 40;

	/* shadow and glow (they don't turn) */
	gfx_transform(still);
	gfx_dot(0.03f, 0.10f, 1.18f, rgba(0, 0, 0, 120));
	if (glow > 0.01f) {
		gfx_blend_add(true);
		gfx_ring(0, 0, 0.98f, 1.0f + 0.22f * glow, rgba(130, 110, 255, (int)(120 * glow)), rgba(90, 70, 255, 0));
		gfx_blend_add(false);
	}

	gfx_transform(turned);
	/* the edge: the silver of the data side showing through */
	gfx_ring(0, 0, LABEL, 1.0f, rgba(214, 218, 232, 255), rgba(150, 156, 178, 255));
	/* the label */
	gfx_ring(0, 0, HUB, LABEL, rgba(84, 70, 196, 255), rgba(36, 28, 104, 255));
	for (int i = 0; i < 12; i += 2)   /* faint printed sectors: they show it turning */
		gfx_arc(0, 0, HUB + 0.04f, LABEL - 0.03f, i * (float)M_PI / 6, (i + 1) * (float)M_PI / 6,
		        rgba(255, 255, 255, small ? 34 : 14));
	gfx_ring(0, 0, LABEL - 0.035f, LABEL - 0.025f, rgba(170, 160, 255, 120), rgba(170, 160, 255, 120));
	gfx_ring(0, 0, HUB + 0.025f, HUB + 0.032f, rgba(170, 160, 255, 90), rgba(170, 160, 255, 90));
	if (small) {
		/* a bright mark so a small disc visibly turns */
		gfx_arc(0, 0, HUB + 0.1f, LABEL - 0.1f, -0.25f, 0.25f, rgba(200, 190, 255, 200));
	}

	if (!small && banner) {
		/* the banner, printed above the hole */
		gfx_rrect(-0.46f, -0.83f, 0.92f, 0.34f, 0.04f, rgba(20, 16, 60, 220));
		gfx_image(banner, -0.43f, -0.81f, 0.86f, 0.29f, rgba(255, 255, 255, 255));
	}
	if (!small && title && *title && rx > 70) {
		/* the title under the hole, in pixels (unit = rx) */
		const float px[6] = { 1 / rx, 0, 0, 0, 1 / rx, 0 };
		float m[6];
		affine_mul(m, turned, px);
		gfx_transform(m);
		int size = (int)(rx * 0.085f);
		if (size < 10) size = 10;
		if (size > 20) size = 20;
		font_wrap(0, rx * 0.47f, size, size * 1.2f, rgba(236, 234, 255, 235), ALIGN_CENTER, rx * 1.05f, 2, title);
		font_mid(0, rx * 0.80f, size * 0.7f, rgba(170, 160, 255, 200), ALIGN_CENTER, 0, "CUBEMUSIC");
		gfx_transform(turned);
	}

	/* the clear hub with its stacking ring */
	gfx_ring(0, 0, HOLE, HUB, rgba(170, 178, 205, 120), rgba(120, 128, 160, 150));
	gfx_ring(0, 0, 0.235f, 0.25f, rgba(220, 225, 240, 110), rgba(220, 225, 240, 110));
	gfx_ring(0, 0, HOLE, HOLE + 0.012f, rgba(230, 235, 250, 160), rgba(230, 235, 250, 160));

	/* the shine, still */
	gfx_transform(still);
	gfx_blend_add(true);
	sheen(-2.2f);
	gfx_blend_add(false);

	gfx_identity();
	gfx_fade = old_fade;
}
