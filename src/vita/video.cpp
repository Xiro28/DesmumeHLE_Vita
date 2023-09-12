#include "video.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/dmac.h> 
#include <vitaGL.h>

#include "../GPU.h"

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

void video_DrawFrame(){
	glViewport(0, 0, 960, 544);
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
	float vtx[4 * 2] = {
		304, 544,
		304 + 352, 544,
		304,   0,
		304 + 352,   0
	};
	float txcoord[4 * 2] = {
		0,   1,
		1,   1,
		0,   0,
		1,   0
	};
	float txcoord2[4 * 2] = {
		0,   0,
		1,   0,
		0,   1,
		1,   1
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vtx);
	glTexCoordPointer(2, GL_FLOAT, 0, txcoord);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (top_changed) {
		glBindTexture(GL_TEXTURE_2D, top_screen_tex);
		float vtx_top[4 * 2] = {
			304, 272,
			304 + 352, 272,
			304,   0,
			304 + 352,   0
		};
		glVertexPointer(2, GL_FLOAT, 0, vtx_top);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord2);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		top_changed--;
	}
	if (bottom_changed) {
		glBindTexture(GL_TEXTURE_2D, bottom_screen_tex);
		float vtx_bottom[4 * 2] = {
			304, 544,
			304 + 352, 544,
			304,   272,
			304 + 352,   272
		};
		glVertexPointer(2, GL_FLOAT, 0, vtx_bottom);
		glTexCoordPointer(2, GL_FLOAT, 0, txcoord2);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		bottom_changed--;
	}
	glUseProgram(old_prog);
	glEnable(GL_DEPTH_TEST);
}