/* main.c - this file is part of DeSmuME
*
* Copyright (C) 2006,2007 DeSmuME Team
* Copyright (C) 2007 Pascal Giard (evilynux)
* Copyright (C) 2009 Yoshihiro (DsonPSP)
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This file is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>

#include <gpu_es4/psp2_pvr_hint.h>

#include "video.h"
#include "input.h"
#include "menu.h"
#include "sound.h"
#include "config.h"

#include <stdio.h>
#include <malloc.h>

#include <pib.h>

#include "../MMU.h"
#include "../NDSSystem.h"
#include "../debug.h"
#include "../OGLES2Renderer.h"
#include "../render3D.h"
#include "../rasterize.h"
#include "../saves.h"
#include "../mic.h"
#include "../SPU.h"

volatile bool execute = FALSE;

GPU3DInterface *core3DList[] = {
	&gpu3DNull,
	&gpu3DRasterize,
	&gpu3DglES,
	NULL
};

SoundInterface_struct *SNDCoreList[] = {
  &SNDDummy,
  &SNDDummy,
  &SNDVITA,
  NULL
};

const char * save_type_names[] = {
	"Autodetect",
	"EEPROM 4kbit",
	"EEPROM 64kbit",
	"EEPROM 512kbit",
	"FRAM 256kbit",
	"FLASH 2mbit",
	"FLASH 4mbit",
	NULL
};

static void desmume_cycle()
{
	input_UpdateKeypad();
	input_UpdateTouch();

    NDS_exec<false>();

    if(UserConfiguration.soundEnabled)
    	SPU_Emulate_user();
}

extern "C" {
	int scePowerSetArmClockFrequency(int freq);
}

#define FPS_CALC_INTERVAL 1000000

static unsigned int frames = 0;

u32 frame_time = 0;

static inline void calc_fps(char fps_str[32])
{
	static SceKernelSysClock old = 0;
	static u32 fps_base_tick = 0;

	bool cleanFrames = false;

	SceKernelSysClock now  = sceKernelGetProcessTimeLow();
	SceKernelSysClock diff = (now - old);

	if (diff >= FPS_CALC_INTERVAL) {
		const float fps = frames / ((diff/1000)/1000.0f);
		sprintf(fps_str, "FPS: %.2f", fps);
		frames = 0;
		old = now;
		cleanFrames = true;
	}

	int delay = (fps_base_tick + frames*FPS_CALC_INTERVAL/60) - frame_time;

	if (delay < -500000 || delay > 100000)
		fps_base_tick = now;
	else if (delay > 0)
		sceKernelDelayThread(delay);

	if (cleanFrames)
	{
		cleanFrames = false;
		frames = 0;
	}
}

void PVR_PSP2Init()
{
	PVRSRV_PSP2_APPHINT hint;
  	PVRSRVInitializeAppHint(&hint);
  	PVRSRVCreateVirtualAppHint(&hint);
	printf("PVE_PSP2 init OK.");
}

void init_egl(){
	const EGLint attribs[] = {
            EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 16,
			EGL_STENCIL_SIZE, 8,
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE 
    };
	EGLint major, minor;
    EGLint w, h, format;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;

	EGLBoolean eRetStatus;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    eRetStatus = eglInitialize(display, &major, &minor);

	if( eRetStatus != EGL_TRUE )
		printf("eglInitialize %d\n", eglGetError());

	eRetStatus = eglGetConfigs (display, &config, 1, &numConfigs);
	if( eRetStatus != EGL_TRUE )
		printf("eglGetConfigs %d\n", eglGetError());

    eRetStatus = eglChooseConfig(display, attribs, &config, 1, &numConfigs);

	if( eRetStatus != EGL_TRUE )
		printf("eglChooseConfig %d\n", eglGetError());

	const EGLint surfaceAttribs[] = {
            EGL_WIDTH, 256,
			EGL_HEIGHT, 256,
			EGL_LARGEST_PBUFFER, EGL_FALSE,
			EGL_NONE
    };
	
    surface = eglCreatePbufferSurface(display, config, surfaceAttribs);

	eRetStatus = eglBindAPI(EGL_OPENGL_ES_API);
	if (eRetStatus != EGL_TRUE)
		printf("eglBindAPI %d\n", eglGetError());

	const EGLint contextAttribs[] = {
			EGL_CONTEXT_CLIENT_VERSION, 2,
			EGL_NONE
    };

    context = eglCreateContext(display, config, NULL, contextAttribs);

	if (context == EGL_NO_CONTEXT)
		printf("eglCreateContext %d\n", eglGetError());


    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
		printf("OpenGLES2: Could not initialize %d\n", eglGetError());
}

char fps_str[32] = {0};
int main()
{
	sceKernelLoadStartModule("vs0:sys/external/libfios2.suprx", 0, NULL, 0, NULL, NULL);
	sceKernelLoadStartModule("vs0:sys/external/libc.suprx", 0, NULL, 0, NULL, NULL);
	sceKernelLoadStartModule("ux0:data/desmume/plugins/libgpu_es4_ext.suprx", 0, NULL, 0, NULL, NULL);
	sceKernelLoadStartModule("ux0:data/desmume/plugins/libIMGEGL.suprx", 0, NULL, 0, NULL, NULL);

	PVR_PSP2Init();
	init_egl();
	video_Init(); 

	scePowerSetArmClockFrequency(444);
	scePowerSetGpuClockFrequency(222);

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	

	char *filename = menu_FileBrowser();

	if(!filename)
		goto exit;

	struct NDS_fw_config_data fw_config;
	NDS_FillDefaultFirmwareConfigData(&fw_config);
  	NDS_Init();
	NDS_3D_ChangeCore(1);
	backup_setManualBackupType(0);

	if(UserConfiguration.jitEnabled){
		CommonSettings.use_jit = true;
		CommonSettings.jit_max_block_size = 100; // Some games can be higher but let's not make it even more unstable
	}

	if (NDS_LoadROM(filename) < 0) {
		goto exit;
	}

	execute = true;

	int i;

	if(UserConfiguration.soundEnabled)
		SPU_ChangeSoundCore(SNDCORE_VITA, 735 * 4);

	while (execute) {

		frame_time = sceKernelGetProcessTimeLow();

		for (i = 0; i < UserConfiguration.frameSkip; i++) {
			NDS_SkipNextFrame();
			desmume_cycle();
		}

		desmume_cycle();

		frames += 1 + UserConfiguration.frameSkip;

		calc_fps(fps_str);
	}

exit:
	video_Exit();

	sceKernelExitProcess(0);
	return 0;
}