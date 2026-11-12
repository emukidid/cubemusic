/* video.c -- see video.h
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include "video.h"
#include "settings.h"
#include "font.h"

#define FIFO_SIZE (256 * 1024)
#define XFB_BYTES (640 * 576 * 2)   /* big enough for any mode, PAL's 576 lines included */

float view_l = 0, view_t = 0, view_r = 640, view_b = 480;
float frame_time, frame_dt = 1 / 60.0f;
float gfx_fade = 1;

static GXRModeObj mode;
static void *xfb[2];
static int fb;
static void *fifo;
static bool mode_change;
static bool no_progressive;
static u64 start_ticks, last_ticks;
static GXTexObj dot_tex;
static u8 dot_data[32 * 32] __attribute__((aligned(32)));

Color col_alpha(Color c, float k) {
	if (k < 0) k = 0;
	if (k > 1) k = 1;
	c.a = (u8)(c.a * k);
	return c;
}

Color col_mix(Color a, Color b, float t) {
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

/* -------------------------------------------------------------- the mode */

static void pick_mode(void) {
	u32 tv = VIDEO_GetCurrentTvMode();
	bool component = VIDEO_HaveComponentCable() && !no_progressive;
	bool prog = component && settings.resolution != RES_480I;
	GXRModeObj *m;
	if (prog)
		m = tv == VI_PAL ? &TVPal576ProgScale : tv == VI_MPAL ? &TVMpal480Prog : &TVNtsc480Prog;
	else if (tv == VI_PAL)
		m = &TVPal576IntDfScale;
	else if (tv == VI_MPAL)
		m = &TVMpal480IntDf;
	else if (tv == VI_EURGB60)
		m = &TVEurgb60Hz480IntDf;
	else
		m = &TVNtsc480IntDf;
	mode = *m;
	mode.viWidth = settings.full_width ? 704 : 640;
	mode.viXOrigin = (VI_MAX_WIDTH_NTSC - mode.viWidth) / 2;
}

static void setup_gx(void) {
	f32 yscale = GX_GetYScaleFactor(mode.efbHeight, mode.xfbHeight);
	u32 xfb_h = GX_SetDispCopyYScale(yscale);
	GX_SetViewport(0, 0, mode.fbWidth, mode.efbHeight, 0, 1);
	GX_SetScissor(0, 0, mode.fbWidth, mode.efbHeight);
	GX_SetDispCopySrc(0, 0, mode.fbWidth, mode.efbHeight);
	GX_SetDispCopyDst(mode.fbWidth, xfb_h);
	GX_SetCopyFilter(mode.aa, mode.sample_pattern, GX_TRUE, mode.vfilter);
	GX_SetFieldMode(mode.field_rendering, (mode.viHeight == 2 * mode.xfbHeight) ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(mode.aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetCullMode(GX_CULL_NONE);
}

static void configure_vi(void) {
	VIDEO_Configure(&mode);
	VIDEO_ClearFrameBuffer(&mode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(&mode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (mode.viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	fb = 1;
}

static void retrace(u32 count) {
	PAD_ScanPads();
}

void video_init(bool force_interlaced) {
	no_progressive = force_interlaced;
	xfb[0] = MEM_K0_TO_K1(memalign(32, XFB_BYTES));
	xfb[1] = MEM_K0_TO_K1(memalign(32, XFB_BYTES));
	pick_mode();
	configure_vi();
	VIDEO_SetPostRetraceCallback(retrace);
	fifo = memalign(32, FIFO_SIZE);
	memset(fifo, 0, FIFO_SIZE);
	GX_Init(fifo, FIFO_SIZE);
	GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);
	setup_gx();
	GX_CopyDisp(xfb[0], GX_TRUE);

	/* the soft dot: a smooth falloff, full in the middle */
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++) {
			float dx = (x + 0.5f - 16) / 16, dy = (y + 0.5f - 16) / 16;
			float d = sqrtf(dx * dx + dy * dy);
			float v = d >= 1 ? 0 : (1 - d) * (1 - d) * (1 + 2 * d);   /* smoothstep-like */
			int off = ((y >> 2) * 4 + (x >> 3)) * 32 + ((y & 3) << 3) + (x & 7);
			dot_data[off] = (u8)(v * 255);
		}
	DCFlushRange(dot_data, sizeof(dot_data));
	gfx_tex_init(&dot_tex, dot_data, 32, 32, GX_TF_I8, true);

	start_ticks = last_ticks = gettime();
}

void video_settings_changed(void) {
	mode_change = true;
}

void video_shutdown(void) {
	GX_DrawDone();
	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}

/* ------------------------------------------------------------- the frame */

static enum { M_NONE, M_COLOR, M_TEX, M_ALPHA } cur_mode;
static GXTexObj *cur_tex;

static void view_bounds(void) {
	float w = settings.widescreen ? 640.0f * 4 / 3 : 640.0f, h = 480.0f;
	float k = 1.0f - settings.overscan / 100.0f;
	w /= k;
	h /= k;
	view_l = 320 - w / 2;
	view_r = 320 + w / 2;
	view_t = 240 - h / 2;
	view_b = 240 + h / 2;
}

void video_begin(void) {
	if (mode_change) {
		mode_change = false;
		GXRModeObj old = mode;
		pick_mode();
		if (memcmp(&old, &mode, sizeof(mode))) {
			GX_DrawDone();
			VIDEO_SetBlack(TRUE);
			VIDEO_Flush();
			VIDEO_WaitVSync();
			configure_vi();
			setup_gx();
		}
	}
	u64 now = gettime();
	frame_time = ticks_to_microsecs(now - start_ticks) / 1e6f;
	frame_dt = ticks_to_microsecs(now - last_ticks) / 1e6f;
	if (frame_dt > 0.1f)
		frame_dt = 0.1f;
	last_ticks = now;

	view_bounds();
	Mtx44 proj;
	guOrtho(proj, view_t, view_b, view_l, view_r, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	gfx_identity();
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTevStages(1);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetAlphaUpdate(GX_FALSE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetCullMode(GX_CULL_NONE);
	gfx_blend_add(false);
	gfx_unclip();
	GX_InvalidateTexAll();
	cur_mode = M_NONE;
	gfx_fade = 1;
	font_frame();
}

void video_end(void) {
	GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);   /* so the copy clears depth too */
	GX_CopyDisp(xfb[fb], GX_TRUE);
	GX_DrawDone();
	VIDEO_SetNextFramebuffer(xfb[fb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	fb ^= 1;
}

/* ------------------------------------------------------------- state */

void gfx_mode_color(void) {
	if (cur_mode == M_COLOR)
		return;
	cur_mode = M_COLOR;
	GX_SetNumTexGens(0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void tex_stage(GXTexObj *t) {
	GX_LoadTexObj(t, GX_TEXMAP0);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
}

void gfx_mode_tex(GXTexObj *t) {
	if (cur_mode == M_TEX && cur_tex == t)
		return;
	tex_stage(t);
	cur_tex = t;
	if (cur_mode != M_TEX)
		GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	cur_mode = M_TEX;
}

void gfx_mode_alpha(GXTexObj *t) {
	if (cur_mode == M_ALPHA && cur_tex == t)
		return;
	tex_stage(t);
	cur_tex = t;
	if (cur_mode != M_ALPHA) {
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
		GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	}
	cur_mode = M_ALPHA;
}

void gfx_blend_add(bool add) {
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, add ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

void gfx_clip(float x, float y, float w, float h) {
	float sx = mode.fbWidth / (view_r - view_l), sy = mode.efbHeight / (view_b - view_t);
	int x0 = (int)floorf((x - view_l) * sx), y0 = (int)floorf((y - view_t) * sy);
	int x1 = (int)ceilf((x + w - view_l) * sx), y1 = (int)ceilf((y + h - view_t) * sy);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > mode.fbWidth) x1 = mode.fbWidth;
	if (y1 > mode.efbHeight) y1 = mode.efbHeight;
	if (x1 <= x0 || y1 <= y0) {
		x1 = x0 + 1;
		y1 = y0 + 1;
	}
	GX_SetScissor(x0, y0, x1 - x0, y1 - y0);
}

void gfx_unclip(void) {
	GX_SetScissor(0, 0, mode.fbWidth, mode.efbHeight);
}

void gfx_transform(const float m[6]) {
	Mtx mt = {
		{ m[0], m[1], 0, m[2] },
		{ m[3], m[4], 0, m[5] },
		{ 0, 0, 1, 0 },
	};
	GX_LoadPosMtxImm(mt, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
}

void gfx_identity(void) {
	static const float id[6] = { 1, 0, 0, 0, 1, 0 };
	gfx_transform(id);
}

void affine_mul(float o[6], const float a[6], const float b[6]) {
	float r[6] = {
		a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
		a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5],
	};
	memcpy(o, r, sizeof(r));
}

void gfx_tex_init(GXTexObj *t, void *data, int w, int h, u8 fmt, bool clamp) {
	GX_InitTexObj(t, data, w, h, fmt, clamp ? GX_CLAMP : GX_REPEAT, clamp ? GX_CLAMP : GX_REPEAT, GX_FALSE);
	GX_InitTexObjLOD(t, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

/* ------------------------------------------------------------- shapes */

void gfx_rect(float x, float y, float w, float h, Color c) {
	gfx_rect_vgrad(x, y, w, h, c, c);
}

void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom) {
	gfx_mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	gfx_v(x, y, top, 0, 0);
	gfx_v(x + w, y, top, 0, 0);
	gfx_v(x + w, y + h, bottom, 0, 0);
	gfx_v(x, y + h, bottom, 0, 0);
	GX_End();
}

void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right) {
	gfx_mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	gfx_v(x, y, left, 0, 0);
	gfx_v(x + w, y, right, 0, 0);
	gfx_v(x + w, y + h, right, 0, 0);
	gfx_v(x, y + h, left, 0, 0);
	GX_End();
}

#define CORNER_SEGS 6
#define PERIM_MAX   (4 * (CORNER_SEGS + 1))

/* the outline of a rounded rectangle, clockwise from the top-left corner */
static int rrect_points(float x, float y, float w, float h, float r, float *px, float *py) {
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r < 0) r = 0;
	static const float cx[4] = { 0, 1, 1, 0 }, cy[4] = { 0, 0, 1, 1 };
	int n = 0;
	for (int c = 0; c < 4; c++) {
		float ox = x + (cx[c] ? w - r : r), oy = y + (cy[c] ? h - r : r);
		float a0 = (float)M_PI + c * (float)M_PI / 2;
		for (int s = 0; s <= CORNER_SEGS; s++) {
			float a = a0 + s * ((float)M_PI / 2) / CORNER_SEGS;
			px[n] = ox + cosf(a) * r;
			py[n] = oy + sinf(a) * r;
			n++;
		}
	}
	return n;
}

void gfx_rrect(float x, float y, float w, float h, float r, Color c) {
	gfx_rrect_vgrad(x, y, w, h, r, c, c);
}

void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom) {
	if (w <= 0 || h <= 0)
		return;
	float px[PERIM_MAX], py[PERIM_MAX];
	int n = rrect_points(x, y, w, h, r, px, py);
	gfx_mode_color();
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, n + 2);
	gfx_v(x + w / 2, y + h / 2, col_mix(top, bottom, 0.5f), 0, 0);
	for (int i = 0; i <= n; i++) {
		int k = i % n;
		gfx_v(px[k], py[k], col_mix(top, bottom, (py[k] - y) / h), 0, 0);
	}
	GX_End();
}

static void rrect_band(float x, float y, float w, float h, float r, float in, float out, Color cin, Color cout) {
	float ix[PERIM_MAX], iy[PERIM_MAX], ox[PERIM_MAX], oy[PERIM_MAX];
	int n = rrect_points(x + in, y + in, w - 2 * in, h - 2 * in, r - in, ix, iy);
	rrect_points(x - out, y - out, w + 2 * out, h + 2 * out, r + out, ox, oy);
	gfx_mode_color();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (n + 1));
	for (int i = 0; i <= n; i++) {
		int k = i % n;
		gfx_v(ox[k], oy[k], cout, 0, 0);
		gfx_v(ix[k], iy[k], cin, 0, 0);
	}
	GX_End();
}

void gfx_rrect_line(float x, float y, float w, float h, float r, float thick, Color c) {
	rrect_band(x, y, w, h, r, thick, 0, c, c);
}

void gfx_rrect_glow(float x, float y, float w, float h, float r, float size, Color c) {
	Color c0 = c;
	c0.a = 0;
	rrect_band(x, y, w, h, r, 0, size, c, c0);
}

#define CIRCLE_SEGS 48

void gfx_circle(float cx, float cy, float r, Color c) {
	gfx_mode_color();
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, CIRCLE_SEGS + 2);
	gfx_v(cx, cy, c, 0, 0);
	for (int i = 0; i <= CIRCLE_SEGS; i++) {
		float a = i * 2 * (float)M_PI / CIRCLE_SEGS;
		gfx_v(cx + cosf(a) * r, cy + sinf(a) * r, c, 0, 0);
	}
	GX_End();
}

void gfx_ring(float cx, float cy, float r0, float r1, Color inner, Color outer) {
	gfx_mode_color();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (CIRCLE_SEGS + 1));
	for (int i = 0; i <= CIRCLE_SEGS; i++) {
		float a = i * 2 * (float)M_PI / CIRCLE_SEGS, ca = cosf(a), sa = sinf(a);
		gfx_v(cx + ca * r1, cy + sa * r1, outer, 0, 0);
		gfx_v(cx + ca * r0, cy + sa * r0, inner, 0, 0);
	}
	GX_End();
}

void gfx_arc(float cx, float cy, float r0, float r1, float a0, float a1, Color c) {
	int segs = (int)(fabsf(a1 - a0) / (2 * (float)M_PI) * CIRCLE_SEGS) + 2;
	gfx_mode_color();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (segs + 1));
	for (int i = 0; i <= segs; i++) {
		float a = a0 + (a1 - a0) * i / segs, ca = cosf(a), sa = sinf(a);
		gfx_v(cx + ca * r1, cy + sa * r1, c, 0, 0);
		gfx_v(cx + ca * r0, cy + sa * r0, c, 0, 0);
	}
	GX_End();
}

void gfx_line(float x0, float y0, float x1, float y1, float thick, Color c) {
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len <= 0)
		return;
	float nx = -dy / len * thick / 2, ny = dx / len * thick / 2;
	gfx_mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	gfx_v(x0 + nx, y0 + ny, c, 0, 0);
	gfx_v(x1 + nx, y1 + ny, c, 0, 0);
	gfx_v(x1 - nx, y1 - ny, c, 0, 0);
	gfx_v(x0 - nx, y0 - ny, c, 0, 0);
	GX_End();
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c0, Color c1, Color c2) {
	gfx_mode_color();
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
	gfx_v(x0, y0, c0, 0, 0);
	gfx_v(x1, y1, c1, 0, 0);
	gfx_v(x2, y2, c2, 0, 0);
	GX_End();
}

void gfx_quad(const float xy[8], const Color c[4]) {
	gfx_mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	for (int i = 0; i < 4; i++)
		gfx_v(xy[2 * i], xy[2 * i + 1], c[i], 0, 0);
	GX_End();
}

void gfx_dot(float cx, float cy, float r, Color c) {
	gfx_mode_alpha(&dot_tex);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	gfx_v(cx - r, cy - r, c, 0, 0);
	gfx_v(cx + r, cy - r, c, 1, 0);
	gfx_v(cx + r, cy + r, c, 1, 1);
	gfx_v(cx - r, cy + r, c, 0, 1);
	GX_End();
}

void gfx_image(GXTexObj *t, float x, float y, float w, float h, Color tint) {
	gfx_image_part(t, x, y, w, h, 0, 0, 1, 1, tint);
}

void gfx_image_part(GXTexObj *t, float x, float y, float w, float h, float s0, float t0, float s1, float t1, Color tint) {
	gfx_mode_tex(t);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	gfx_v(x, y, tint, s0, t0);
	gfx_v(x + w, y, tint, s1, t0);
	gfx_v(x + w, y + h, tint, s1, t1);
	gfx_v(x, y + h, tint, s0, t1);
	GX_End();
}
