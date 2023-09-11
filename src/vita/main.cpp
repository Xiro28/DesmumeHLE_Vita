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

#include <vitasdk.h>

#include "video.h"
#include "input.h"
#include "menu.h"
#include "sound.h"
#include "config.h"

#include <stdio.h>
#include <malloc.h>

#include "../MMU.h"
#include "../NDSSystem.h"
#include "../debug.h"
#include "../OGLES2Renderer.h"
#include "../render3D.h"
#include "../rasterize.h"
#include "../saves.h"
#include "../mic.h"
#include "../SPU.h"

extern "C" {
void vglSwapBuffers(GLboolean has_commondialog);
};

extern "C" int _newlib_heap_size_user = 256 * 1024 * 1024;

extern "C" GLboolean vglInitExtended(int legacy_pool_size, int width, int height, int ram_threshold, SceGxmMultisampleMode msaa);
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

char fps_str[32] = {0};
int main(int argc, char *argv[])
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_RAZOR_CAPTURE);
	vglInitExtended(0, 960, 544, 32 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
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
	NDS_3D_ChangeCore(2);
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
		
		video_DrawFrame();
		vglSwapBuffers(GL_FALSE);
	}

exit:
	video_Exit();

	sceKernelExitProcess(0);
	return 0;
}
