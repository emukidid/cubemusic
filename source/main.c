/* main.c -- CubeMusic main entry point.
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
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include "gfx.h"
#include "IPLFontWrite.h"
#include "logic.h"
#include "draw.h"
#include "exi.h"

#define DEFAULT_FIFO_SIZE    (256*1024)//(64*1024) minimum
char txtbuffer[2048] __attribute__((aligned(32)));
char IPLInfo[256] __attribute__((aligned(32)));
GXRModeObj *vmode = NULL;			//Graphics Mode Object
void *gp_fifo = NULL;
u32 *xfb[2] = { NULL, NULL };   	//Framebuffers
int whichfb = 0;       		 	    //Frame buffer toggle

static void ProperScanPADS()	{
	PAD_ScanPads(); 
}
void *Initialise();


void initialise_video(GXRModeObj *m) {
	VIDEO_Configure (m);
	if(xfb[0]) free(MEM_K1_TO_K0(xfb[0]));
	if(xfb[1]) free(MEM_K1_TO_K0(xfb[1]));
	xfb[0] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (m));
	xfb[1] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (m));
	VIDEO_ClearFrameBuffer (m, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer (m, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer (xfb[0]);
	VIDEO_SetPostRetraceCallback (ProperScanPADS);
	VIDEO_SetBlack (0);
	VIDEO_Flush ();
	VIDEO_WaitVSync ();
	if (m->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
	else while (VIDEO_GetNextField())   VIDEO_WaitVSync();
	
	// setup the fifo and then init GX
	if(gp_fifo == NULL) {
		gp_fifo = MEM_K0_TO_K1 (memalign (32, DEFAULT_FIFO_SIZE));
		memset (gp_fifo, 0, DEFAULT_FIFO_SIZE);
		GX_Init (gp_fifo, DEFAULT_FIFO_SIZE);
	}
	// clears the bg to color and clears the z buffer
	GX_SetCopyClear ((GXColor) {0, 0, 0, 0xFF}, GX_MAX_Z24);
	// init viewport
	GX_SetViewport (0, 0, m->fbWidth, m->efbHeight, 0, 1);
	// Set the correct y scaling for efb->xfb copy operation
	GX_SetDispCopyYScale ((f32) m->xfbHeight / (f32) m->efbHeight);
	GX_SetDispCopySrc (0, 0, m->fbWidth, m->efbHeight);
	GX_SetDispCopyDst (m->fbWidth, m->xfbHeight);
	GX_SetCopyFilter (m->aa, m->sample_pattern, GX_TRUE, m->vfilter);
	GX_SetFieldMode (m->field_rendering, ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
	if (m->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCullMode (GX_CULL_NONE); // default in rsp init
	GX_CopyDisp (xfb[0], GX_TRUE); // This clears the efb
	GX_CopyDisp (xfb[0], GX_TRUE); // This clears the xfb
}


int main(int argc, char **argv) {

	void *fb;
	fb = Initialise();
	if(!fb) {
		return -1;
	}

	u64 prevtime = gettime();
	u64 vsyncwaittime = gettime();
	u32 idletime = 0;
	int framespersec = 0;
	int framerate = 0;
	
	// Init everything
	logic_init();
	draw_init();
	audio_init();
	
	while(1) {
		
		// Draw everything
		draw_frame_begin();
		// Update all logic for a frame
		logic_update();
#ifdef _UI_DEBUG
		// Print FPS
		sprintf(txtbuffer,"%i FPS %.1fMB - idle %.2f", 
				framerate, ((SYS_GetArenaHi()-SYS_GetArenaLo()) / (1024.0*1024.0)),
				((float)idletime/(float)diff_msec(prevtime, gettime())) * 100.0f );
		WriteFont(280, 10 , txtbuffer);
#endif
		// Update FPS
		if(diff_msec(prevtime, gettime()) >=1000) {
			framerate = framespersec;
			framespersec = 0;
			idletime = 0;
			prevtime = gettime();
		}
		
		vsyncwaittime = gettime();
		draw_frame_end();	// Waits for vsync
		idletime += diff_msec(vsyncwaittime, gettime());
		framespersec++;
	}

	return 0;
}

void * Initialise() {
	VIDEO_Init ();
	PAD_Init ();  
	DVD_Init(); 
	*(volatile unsigned long*)0xcc00643c = 0x00000000; //allow 32mhz exi bus
	
	// Disable IPL modchips to allow access to IPL ROM fonts
	ipl_set_config(6); 	
	__SYS_ReadROM(IPLInfo,256,0);	// Read IPL tag
	
	int retPAD = 0, retCnt = 10000;
	while(retPAD <= 0 && retCnt >= 0) { retPAD = PAD_ScanPads(); usleep(100); retCnt--; }
	// L Trigger held down ignores the fact that there's a component cable plugged in.
	if(VIDEO_HaveComponentCable() && !(PAD_ButtonsDown(0) & PAD_TRIGGER_L)) {
		if(strstr(IPLInfo,"MPAL")!=NULL) {
			vmode = &TVMpal480Prog; //Progressive 480p
		}
		else if((strstr(IPLInfo,"PAL")!=NULL)) {
			vmode = &TVPal576ProgScale; //Progressive 576p
		}
		else {
			vmode = &TVNtsc480Prog; //Progressive 480p
		}
	}
	else {
		//try to use the IPL region
		if(strstr(IPLInfo,"MPAL")!=NULL) {
			vmode = &TVMpal480IntDf;        //PAL-M
		}
		else if(strstr(IPLInfo,"PAL")!=NULL) {
			vmode = &TVPal576IntDfScale;         //PAL
		}
		else {
			vmode = &TVNtsc480IntDf;        //NTSC
		}
	}
	initialise_video(vmode);
	init_font();
	init_textures();
	whichfb = 0;
	
	if(usb_isgeckoalive(1)) {
		usb_flush(1);
		CON_EnableGecko(1, 1);
		printf("Enabled Console over USB Gecko\r\n");
	}
	
	return xfb[0];
}
