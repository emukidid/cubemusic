/*  gfx.c - Graphics convenience functions with GX
 *	      - by emu_kidid & sepp256
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/exi.h>
#include <gctypes.h>
#include "gfx.h"
#include "IPLFontWrite.h"

#define GUI_MSGBOX_ALPHA 200


void init_textures() 
{
}

void drawInit()
{
	Mtx44 GXprojection2D;
	Mtx GXmodelView2D;

	// Reset various parameters from gfx plugin
	GX_SetCoPlanar(GX_DISABLE);
	GX_SetClipMode(GX_CLIP_ENABLE);
//	GX_SetScissor(0,0,vmode->fbWidth,vmode->efbHeight);
	GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);

	guMtxIdentity(GXmodelView2D);
	GX_LoadTexMtxImm(GXmodelView2D,GX_TEXMTX0,GX_MTX2x4);
	GX_LoadPosMtxImm(GXmodelView2D,GX_PNMTX0);
	guOrtho(GXprojection2D, 0, 479, 0, 639, 0, 700);
	GX_LoadProjectionMtx(GXprojection2D, GX_ORTHOGRAPHIC);
//	GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);

	GX_SetZMode(GX_DISABLE,GX_ALWAYS,GX_TRUE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_PNMTX0);
	GX_SetVtxDesc(GX_VA_TEX0MTXIDX, GX_TEXMTX0);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	//set vertex attribute formats here
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	//enable textures
	GX_SetNumChans (1);
	GX_SetNumTexGens (1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_SetNumTevStages (1);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);

	//set blend mode
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); //Fix src alpha
	GX_SetColorUpdate(GX_ENABLE);
//	GX_SetAlphaUpdate(GX_ENABLE);
//	GX_SetDstAlpha(GX_DISABLE, 0xFF);
	//set cull mode
	GX_SetCullMode (GX_CULL_NONE);
}

void drawRect(int x, int y, int width, int height, int depth, GXColor color, float s0, float s1, float t0, float t1)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t0);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t0);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(color.r, color.g, color.b, color.a);
		GX_TexCoord2f32(s0,t1);
	GX_End();
}

void DrawSimpleBox(int x, int y, int width, int height, int depth, GXColor fillColor, GXColor borderColor) 
{
	//Adjust for blank texture border
	x-=4; y-=4; width+=8; height+=8;
	
	drawInit();
	GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
	GX_InvalidateTexAll();
	//GX_LoadTexObj(&boxinnerTexObj, GX_TEXMAP0);

	drawRect(x, y, width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	drawRect(x+(width/2), y, width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	drawRect(x, y+(height/2), width/2, height/2, depth, fillColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, fillColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);

	GX_InvalidateTexAll();
	//GX_LoadTexObj(&boxouterTexObj, GX_TEXMAP0);

	drawRect(x, y, width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), 0.0f, ((float)height/32));
	drawRect(x+(width/2), y, width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, 0.0f, ((float)height/32));
	drawRect(x, y+(height/2), width/2, height/2, depth, borderColor, 0.0f, ((float)width/32), ((float)height/32), 0.0f);
	drawRect(x+(width/2), y+(height/2), width/2, height/2, depth, borderColor, ((float)width/32), 0.0f, ((float)height/32), 0.0f);
}

void DrawImage(int textureId, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawInit();
	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_InvalidateTexAll();

//	GX_LoadTexObj(&backdropTexObj, GX_TEXMAP0);

	if(centered)
	{
		x = (int) x - width/2;
	}
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

void DrawTexObj(GXTexObj *texObj, int x, int y, int width, int height, int depth, float s1, float s2, float t1, float t2, int centered)
{
	drawInit();
	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_InvalidateTexAll();
	GX_LoadTexObj(texObj, GX_TEXMAP0);
	if(centered)
		x = (int) x - width/2;
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32((float) x,(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t1);
		GX_Position3f32((float) (x+width),(float) y,(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t1);
		GX_Position3f32((float) (x+width),(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s2,t2);
		GX_Position3f32((float) x,(float) (y+height),(float) depth );
		GX_Color4u8(255, 255, 255, 255);
		GX_TexCoord2f32(s1,t2);
	GX_End();
}

void _DrawBackdrop() 
{
	char ver[32];
	memset(ver,0,16);
	//DrawImage(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	WriteFont(10,10, "CubeMusic v0.1");
	sprintf(ver, "commit: %s rev: %s", GITREVISION, GITVERSION);
	WriteFontStyled(210,60, ver, 0.55f, false, defaultColor);
}

// Call this when starting a screen
void DrawFrameStart() {
  whichfb ^= 1;
  _DrawBackdrop();
}

// Call this at the end of a screen
void DrawFrameFinish() {
	//Copy EFB->XFB
	GX_SetCopyClear((GXColor){0, 0, 0, 0xFF}, GX_MAX_Z24);
	GX_CopyDisp(xfb[whichfb],GX_TRUE);
	GX_Flush();

	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_Flush();
 	VIDEO_WaitVSync();
}

void DrawProgressBar(int percent, char *message) {
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int middleY = (y2+y1)/2;
	float scale = GetTextScaleToFitInWidth(message, x2-x1);
  	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
  	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	GXColor progressBarColor = (GXColor) {255,128,0,GUI_MSGBOX_ALPHA}; //orange
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 
	int multiplier = (PROGRESS_BOX_WIDTH-20)/100;
	int progressBarWidth = multiplier*100;
	DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*100), 20, 0, noColor, borderColor); 
	DrawSimpleBox( (640/2 - progressBarWidth/2), y1+20,
			(multiplier*percent), 20, 0, progressBarColor, noColor); 

	WriteFontStyled(640/2, middleY, message, scale, true, defaultColor);
	sprintf(txtbuffer,"%d%% percent complete",percent);
	WriteFontStyled(640/2, middleY+30, txtbuffer, 1.0f, true, defaultColor);
}

void DrawMessageBox(int type, char *message) 
{
	int x1 = ((640/2) - (PROGRESS_BOX_WIDTH/2));
	int x2 = ((640/2) + (PROGRESS_BOX_WIDTH/2));
	int y1 = ((480/2) - (PROGRESS_BOX_HEIGHT/2));
	int y2 = ((480/2) + (PROGRESS_BOX_HEIGHT/2));
	int middleY = y2-y1 < 23 ? y1+3 : (y2+y1)/2-12;
	
  	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor); 

	char *tok = strtok(message,"\n");
	while(tok != NULL) {
		WriteFontStyled(640/2, middleY, tok, 1.0f, true, defaultColor);
		tok = strtok(NULL,"\n");
		middleY+=24;
	}
}

void DrawRawFont(int x, int y, char *message) {
  WriteFont(x, y, message);
}

void DrawSelectableButton(int x1, int y1, int x2, int y2, char *message, int mode, u32 color) 
{
	int middleY, borderSize;
	color = (color == -1) ? BUTTON_COLOUR_INNER : color; //never used

	borderSize = (mode==B_SELECTED) ? 6 : 4;
	middleY = (((y2-y1)/2)-12)+y1;

	//determine length of the text ourselves if x2 == -1
	x1 = (x2 == -1) ? x1+2:x1;
	x2 = (x2 == -1) ? GetTextSizeInPixels(message)+x1+(borderSize*2)+6 : x2;

	if(middleY+24 > y2) {
		middleY = y1+3;
	}

	GXColor selectColor = (GXColor) {96,107,164,GUI_MSGBOX_ALPHA}; //bluish
	GXColor noColor = (GXColor) {0,0,0,0}; //black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	//Draw Text and backfill (if selected)
	if(mode==B_SELECTED) {
		DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, selectColor, borderColor);
		WriteFontStyled(x1 + borderSize+3, middleY, message, 1.0f, false, defaultColor);
	}
	else {
		DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
		WriteFontStyled(x1 + borderSize+3, middleY, message, 1.0f, false, defaultColor);
	}
}

void DrawEmptyBox(int x1, int y1, int x2, int y2, int color) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;

	GXColor fillColor = (GXColor) {0,0,0,GUI_MSGBOX_ALPHA}; //Black
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //Silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, fillColor, borderColor);
}

void DrawTransparentBox(int x1, int y1, int x2, int y2) 
{
	int borderSize;
	borderSize = (y2-y1) <= 30 ? 3 : 10;
	x1-=borderSize;x2+=borderSize;y1-=borderSize;y2+=borderSize;

	GXColor noColor 	= (GXColor) {0,0,0,0};
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //Silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
}

int DrawYesNoDialog(char *message) {
	int sel = 0;
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A)){ VIDEO_WaitVSync (); }
	while(1) {
		DrawEmptyBox(75,190, vmode->fbWidth-78, 330, COLOR_BLACK);
		WriteFontStyled(640/2, 215, message, 1.0f, true, defaultColor);
		DrawSelectableButton(100, 280, -1, 310, "Yes", (sel==1) ? B_SELECTED:B_NOSELECT,-1);
		DrawSelectableButton(380, 280, -1, 310, "No", (!sel) ? B_SELECTED:B_NOSELECT,-1);
		DrawFrameFinish();
		while (!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B)&& !(PAD_ButtonsHeld(0) & PAD_BUTTON_A))
			{ VIDEO_WaitVSync (); }
		u16 btns = PAD_ButtonsHeld(0);
		if((btns & PAD_BUTTON_RIGHT) || (btns & PAD_BUTTON_LEFT)) {
			sel^=1;
		}
		if((btns & PAD_BUTTON_A) || (btns & PAD_BUTTON_B))
			break;
		while (!(!(PAD_ButtonsHeld(0) & PAD_BUTTON_RIGHT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_LEFT) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_B) && !(PAD_ButtonsHeld(0) & PAD_BUTTON_A)))
			{ VIDEO_WaitVSync (); }
	}
	while ((PAD_ButtonsHeld(0) & PAD_BUTTON_A)){ VIDEO_WaitVSync (); }
	return sel;
} 

void DrawVertScrollBar(int x, int y, int width, int height, float scrollPercent, int scrollHeight) {
	int x1 = x;
	int x2 = x+width;
	int y1 = y;
	int y2 = y+height;
	int scrollStartY = y+3 + (int)(height*scrollPercent);
	scrollHeight = scrollHeight < 10 ? 10:scrollHeight;

	if(scrollStartY + scrollHeight +3 > y2)
		scrollStartY = y2-scrollHeight-3;
	
	GXColor fillColor = (GXColor) {46,57,104,GUI_MSGBOX_ALPHA}; 	//bluish
  	GXColor noColor = (GXColor) {0,0,0,0}; //blank
	GXColor borderColor = (GXColor) {200,200,200,GUI_MSGBOX_ALPHA}; //silver
	
	DrawSimpleBox( x1, y1, x2-x1, y2-y1, 0, noColor, borderColor);
	
	DrawSimpleBox( x1, scrollStartY,
			width, scrollHeight, 0, fillColor, borderColor); 
}


