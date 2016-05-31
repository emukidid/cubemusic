/* splash.c -- 3D Splash Screen
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
#include <math.h>
#include "draw.h"
#include "menu.h"
#include "gfx.h"
#include "IPLFontWrite.h"
#include "splash.h"

#include "logo_tpl.h"
#include "logo.h"

TPLFile logoTPL;
GXTexObj texture;
static float rquad = 0.0f;
static int splash_initialised = 0;

void splash_init() {
	if(splash_initialised) return;
	
	// TPL init
	TPL_OpenTPLFromMemory(&logoTPL, (void *)logo_tpl,logo_tpl_size);
	TPL_GetTexture(&logoTPL,logo,&texture);
	
	splash_initialised = 1;
}

void splash_render(int fadepercent) {
	Mtx view;
	Mtx44 perspective;
	f32 yscale;
	Mtx model, modelview;

	splash_init();
	
	// other gx setup
	GX_SetViewport(0,0,vmode->fbWidth,vmode->efbHeight,0,1);
	yscale = GX_GetYScaleFactor(vmode->efbHeight,vmode->xfbHeight);

	GX_SetDispCopyGamma(GX_GM_1_0);
 
	// setup the vertex descriptor
	// tells the flipper to expect direct data
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT); 
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	// setup the vertex attribute table
	// describes the data
	// args: vat location 0-7, type of data, data format, size, scale
	// so for ex. in the first call we are sending position data with
	// 3 values X,Y,Z of size F32. scale sets the number of fractional
	// bits for non float data.
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGB8, 0);
 
	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	
	// setup texture coordinate generation
	// args: texcoord slot 0-7, matrix type, source to generate texture coordinates from, matrix to use
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	
	GX_InvVtxCache();
	GX_InvalidateTexAll();
	
	GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

	GX_LoadTexObj(&texture, GX_TEXMAP0);

	// setup our camera at the origin
	// looking down the -z axis with y up
	guVector cam = {0.0F, 0.0F, 0.0F},
			up = {0.0F, 1.0F, 0.0F},
		  look = {0.0F, 0.0F, -1.0F};
	guLookAt(view, &cam, &up, &look);
 

	// setup our projection matrix
	// this creates a perspective matrix with a view angle of 90,
	// and aspect ratio based on the display resolution
	f32 w = vmode->viWidth;
	f32 h = vmode->viHeight;
	guPerspective(perspective, 45, (f32)w/h, 0.1F, 300.0F);
	GX_LoadProjectionMtx(perspective, GX_PERSPECTIVE);

	guVector Xaxis = {1,0,0};
	
	guMtxIdentity(model);
	guMtxRotAxisDeg(model, &Xaxis, rquad);
	guMtxTransApply(model, model, 0.0f,0.0f,-3.0f);
	guMtxConcat(view,model,modelview);
	// load the modelview matrix into matrix memory
	GX_LoadPosMtxImm(modelview, GX_PNMTX0);

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);			// Draw A Quad
		GX_Position3f32(-1.0f, 1.0f, 0.0f);	// Top Left
		GX_Color3f32(0.9f,0.9f,1.0f);			// Set The Color To Blue
		GX_TexCoord2f32(0.0f,0.0f);
		GX_Position3f32( 1.0f, 1.0f, 0.0f);		// Top Right
		GX_Color3f32(0.9f,0.9f,1.0f);			// Set The Color To Blue
		GX_TexCoord2f32(1.0f,0.0f);
		GX_Position3f32( 1.0f,-1.0f, 0.0f);	// Bottom Right
		GX_Color3f32(0.9f,0.9f,1.0f);			// Set The Color To Blue
		GX_TexCoord2f32(1.0f,1.0f);
		GX_Position3f32(-1.0f,-1.0f, 0.0f);	// Bottom Left
		GX_Color3f32(0.9f,0.9f,1.0f);			// Set The Color To Blue
		GX_TexCoord2f32(0.0f,1.0f);
	GX_End();									// Done Drawing The Quad 
	rquad-=0.175f;			// Decrease The Rotation Variable For The Quad     ( NEW )

#ifdef _UI_DEBUG
	sprintf(txtbuffer, "Splash (fade %i)", fadepercent);
	WriteFont(10, 400 , txtbuffer);
#endif
}

void splash_draw() {
	splash_render(0);
}

void splash_fade(int fadepercent) {
	splash_render(fadepercent);
}
