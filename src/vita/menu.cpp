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

game_entry *list = NULL;

const char *layout_names[] = {
	"Default",
	"Side by Side"
};

void load_cfg(const char *cfg, game_options *opt) {
	char buffer[128];
	int value;
	FILE *config = fopen(cfg, "r");
	
	// Default values
	opt->threaded_2d_render = 1;
	opt->depth_resolve_mode = 0;
	opt->has_dynarec = 1;
	opt->has_sound = 1;
	opt->frameskip = 1;
	opt->layout = 0;

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%d\n", buffer, &value)) {
			if (strcmp("threaded_2d_render", buffer) == 0) opt->threaded_2d_render = value;
			else if (strcmp("depth_resolve_mode", buffer) == 0) opt->depth_resolve_mode = value;
			else if (strcmp("has_dynarec", buffer) == 0) opt->has_dynarec = value;
			else if (strcmp("has_sound", buffer) == 0) opt->has_sound = value;
			else if (strcmp("layout", buffer) == 0) opt->layout = value;
		}
		fclose(config);
	}
}

void save_cfg(game_entry *e) {
	char cfg_file[256];
	size_t zero_spot = strlen(e->name) - 4;
	e->name[zero_spot] = 0;
	sprintf(cfg_file, "ux0:data/desmume/%s.cfg", e->name);
	e->name[zero_spot] = '.';
	FILE *f = fopen(cfg_file, "w");
	fprintf(f, "%s=%hhu\n", "threaded_2d_render", (int)e->opt.threaded_2d_render);
	fprintf(f, "%s=%hhu\n", "depth_resolve_mode", (int)e->opt.depth_resolve_mode);
	fprintf(f, "%s=%hhu\n", "has_dynarec", (int)e->opt.has_dynarec);
	fprintf(f, "%s=%hhu\n", "has_sound", (int)e->opt.has_sound);
	fprintf(f, "%s=%d\n", "frameskip", (int)e->opt.frameskip);
	fprintf(f, "%s=%d\n", "layout", (int)e->opt.layout);
	fclose(f);
}

game_entry *launched_rom = NULL;
char rom_to_launch[256];
char *menu_FileBrowser() {
	ImGui::CreateContext();
	ImGui_ImplVitaGL_Init();
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	ImGui_ImplVitaGL_MouseStickUsage(false);
	ImGui::GetIO().MouseDrawCursor = false;
	
	SceUID fd = sceIoDopen("ux0:data/desmume");
	SceIoDirent g_dir;
	while (sceIoDread(fd, &g_dir) > 0) {
		if (!strcmp(&g_dir.d_name[strlen(g_dir.d_name) - 4], ".nds")) {
			game_entry *entry = (game_entry *)malloc(sizeof(game_entry));
			strcpy(entry->name, g_dir.d_name);
			entry->next = list;
			char cfg_name[256];
			g_dir.d_name[strlen(g_dir.d_name) - 4] = 0;
			sprintf(cfg_name, "ux0:data/desmume/%s.cfg", g_dir.d_name);
			load_cfg(cfg_name, &entry->opt);
			list = entry;
		}
	}
	sceIoDclose(fd);
	
	game_entry *r = NULL;
	game_entry *focused = NULL;
	uint32_t oldpad;
	SceCtrlData pad;
	int focus = 0;
	int focus_changed = 0;
	printf("Starting menu loop\n");
	while (!r) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if ((pad.buttons & SCE_CTRL_LTRIGGER) && !(oldpad & SCE_CTRL_LTRIGGER)) {
			focus = 0;
			focus_changed = 1;
		} else if ((pad.buttons & SCE_CTRL_RTRIGGER) && !(oldpad & SCE_CTRL_RTRIGGER)) {
			focus = 1;
			focus_changed = 1;
		}
		oldpad = pad.buttons;
		ImGui_ImplVitaGL_NewFrame();
		game_entry *e = list;
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(600, 544), ImGuiSetCond_Always);
		if (focus == 0 && focus_changed) {
			ImGui::SetNextWindowFocus();
			focus_changed = 0;
		}
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		while (e) {
			if (ImGui::Button(e->name, ImVec2(-1.0f, 0.0f))) {
				r = e;
			}
			if (ImGui::IsItemFocused()) {
				focused = e;
			}
			e = e->next;
		}
		ImGui::End();
		if (focus == 1 && focus_changed) {
			ImGui::SetNextWindowFocus();
			focus_changed = 0;
		}
		ImGui::SetNextWindowPos(ImVec2(600, 0), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(360, 544), ImGuiSetCond_Always);
		ImGui::Begin("##options", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
		if (focused) {
			ImGui::Checkbox("Use Dynarec", (bool *)&focused->opt.has_dynarec);
			ImGui::Checkbox("Emulate Sound", (bool *)&focused->opt.has_sound);
			ImGui::PushItemWidth(100.0f);
			ImGui::SliderInt("Frameskip", &focused->opt.frameskip, 0, 9);
			ImGui::PopItemWidth();
			ImGui::Separator();
			ImGui::PushItemWidth(150.0f);
			if (ImGui::BeginCombo("Screens Layout", layout_names[focused->opt.layout])) {
				for (int n = 0; n < sizeof(layout_names) / sizeof(*layout_names); n++) {
					bool is_selected = focused->opt.layout == n;
					if (ImGui::Selectable(layout_names[n], is_selected))
						focused->opt.layout = n;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
			ImGui::Checkbox("Alternate Depth Resolve Mode", (bool *)&focused->opt.depth_resolve_mode);
			ImGui::Checkbox("Threaded 2D Rendering", (bool *)&focused->opt.threaded_2d_render);
		}
		ImGui::End();
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
	}
	
	sprintf(rom_to_launch, "ux0:data/desmume/%s", r->name);
	launched_rom = r;
	save_cfg(r);
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