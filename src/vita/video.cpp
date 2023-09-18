#include "video.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/dmac.h> 
#include <vitaGL.h>

#include "../GPU.h"
#include "config.h"

#define PI 3.14159265

int video_layout = 0;
void *data;

GLuint frame_tex;

void video_Init(){
	glGenTextures(1, &frame_tex);
}

void video_Exit(){
	
}

void video_BeginDrawing(){
}

void video_EndDrawing(){
}

extern uint32_t top_screen_tex;
extern uint32_t bottom_screen_tex;
extern uint32_t top_changed;
extern uint32_t bottom_changed;

// Default Layout
float vtx_main_default[4 * 2] = {
	304, 544,
	304 + 352, 544,
	304,   0,
	304 + 352,   0
};
float vtx_3d_top_default[4 * 2] = {
	304, 272,
	304 + 352, 272,
	304,   0,
	304 + 352,   0
};

float vtx_3d_bottom_default[4 * 2] = {
	304, 544,
	304 + 352, 544,
	304,   272,
	304 + 352,   272
};
// Side by Side
float vtx_3d_top_side_by_side[4 * 2] = {
	0, 92 + 360,
	480, 92 + 360,
	0,   92,
	480, 92
};
float vtx_3d_bottom_side_by_side[4 * 2] = {
	480, 92 + 360,
	960, 92 + 360,
	480,   92,
	960, 92
};

void video_DrawFrame() {
	glViewport(0, 0, 960, 544);
	glClear(GL_COLOR_BUFFER_BIT);
	extern char fps_str[32];
	uint16_t *src = (uint16_t *)GPU->GetDisplayInfo().masterNativeBuffer;
	glBindTexture(GL_TEXTURE_2D, frame_tex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 512);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192 * 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, src);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	int old_prog;
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_TEXTURE_2D);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(0, 960, 544, 0, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	float *vtx;
	float *vtx_top;
	float *vtx_bottom;
	if (launched_rom->opt.layout == LAYOUT_DEFAULT) {
		vtx = vtx_main_default;
		vtx_top = vtx_3d_top_default;
		vtx_bottom = vtx_3d_bottom_default;
	} else if (launched_rom->opt.layout == LAYOUT_SIDE_BY_SIDE) {
		vtx = NULL;
		vtx_top = vtx_3d_top_side_by_side;
		vtx_bottom = vtx_3d_bottom_side_by_side;
	}
	float txcoord[4 * 2] = {
		0,   1,
		1,   1,
		0,   0,
		1,   0
	};
	float txcoord_flipped[4 * 2] = {
		0,   0,
		1,   0,
		0,   1,
		1,   1
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	if (vtx) {
		glVertexPointer(2, GL_FLOAT, 0, vtx);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	} else {
		float txcoord_top[4 * 2] = {
			0, 0.5f,
			1.0f, 0.5f,
			0, 0,
			1.0f, 0
		};
		float txcoord_bottom[4 * 2] = {
			0.0f, 1.0f,
			1.0f, 1.0f,
			0.0f, 0.5f,
			1.0f, 0.5f
		};
		glVertexPointer(2, GL_FLOAT, 0, vtx_top);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord_top);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glVertexPointer(2, GL_FLOAT, 0, vtx_bottom);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord_bottom);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (GPU->GetEngineMain()->WillRender3DLayer() && GPU->GetDisplayMain()->GetEngineID() == GPUEngineID_Main) {
		glBindTexture(GL_TEXTURE_2D, top_screen_tex);
		glVertexPointer(2, GL_FLOAT, 0, vtx_top);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord_flipped);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}else
	if (GPU->GetEngineMain()->WillRender3DLayer()) {
		glBindTexture(GL_TEXTURE_2D, bottom_screen_tex);
		glVertexPointer(2, GL_FLOAT, 0, vtx_bottom);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord_flipped);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glUseProgram(old_prog);
	glEnable(GL_DEPTH_TEST);
}