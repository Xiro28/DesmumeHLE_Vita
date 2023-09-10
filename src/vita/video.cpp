#include "video.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/dmac.h> 
#include <vitaGL.h>

#include "../GPU.h"

#define PI 3.14159265

int video_layout = 0;
void *data;

void video_Init(){
	
}

void video_Exit(){
	
}

void video_BeginDrawing(){
	
}

void video_EndDrawing(){
	vglSwapBuffers(GL_FALSE);
}

void video_DrawFrame(){

	extern char fps_str[32];

	video_BeginDrawing();

	
	video_EndDrawing();

}