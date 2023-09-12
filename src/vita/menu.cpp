#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <psp2/io/dirent.h>
#include <imgui_vita.h>

#include <psp2/kernel/processmgr.h>

#include <vector>
#include <malloc.h>
#include <stdio.h>

#include "video.h"
#include "config.h"

#include "../NDSSystem.h"

typedef struct game_entry {
	char name[256];
	struct game_entry *next;
} game_entry;

game_entry *list = NULL;

char rom_to_launch[256];
char *menu_FileBrowser() {
	ImGui::CreateContext();
	ImGui_ImplVitaGL_Init();
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	ImGui_ImplVitaGL_MouseStickUsage(false);
	
	SceUID fd = sceIoDopen("ux0:data/desmume");
	SceIoDirent g_dir;
	while (sceIoDread(fd, &g_dir) > 0) {
		game_entry *entry = (game_entry *)malloc(sizeof(game_entry));
		strcpy(entry->name, g_dir.d_name);
		entry->next = list;
		list = entry;
	}
	sceIoDclose(fd);
	
	game_entry *r = NULL;
	while (!r) {
		ImGui_ImplVitaGL_NewFrame();
		game_entry *e = list;
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(960, 544), ImGuiSetCond_Always);
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		while (e) {
			if (ImGui::Button(e->name, ImVec2(-1.0f, 0.0f))) {
				r = e;
			}
			e = e->next;
		}
		ImGui::End();
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
	}
	
	sprintf(rom_to_launch, "ux0:data/desmume/%s", r->name);
	while (list) {
		game_entry *old = list;
		list = list->next;
		free(old);
	}
	
	return rom_to_launch;
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