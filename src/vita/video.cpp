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

void video_DrawFrame(){
	extern char fps_str[32];
	uint16_t *src = (uint16_t *)GPU->GetDisplayInfo().masterNativeBuffer;
	glBindTexture(GL_TEXTURE_2D, frame_tex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 512);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192 * 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, src);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glUseProgram(0);
	glEnable(GL_TEXTURE_2D);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(0, 960, 544, 0, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	float vtx[4 * 2] = {
		0, 544,
		960, 544,
		0,   0,
		960,   0
	};
	float txcoord[4 * 2] = {
		0,   1,
		1,   1,
		0,   0,
		1,   0
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vtx);
	glTexCoordPointer(2, GL_FLOAT, 0, txcoord);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}