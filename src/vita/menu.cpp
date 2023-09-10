#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <psp2/io/dirent.h>

#include <psp2/kernel/processmgr.h>

#include <vector>
#include <malloc.h>
#include <stdio.h>

#include "video.h"
#include "config.h"

#include "../NDSSystem.h"

//Very rough implementation of a rom selector... Yeah I know its ugly
char* menu_FileBrowser() {
	return "ux0:data/kart.nds";
}

//Uninplemented
int menu_Init(){
	return 0;
}

//Uninplemented
//This is where all the tabs(rom selection, save states, settings, etc) will be handled
int menu_Display(){
	return 0;
}