#ifndef CONFIG_H__
#define CONFIG_H__

typedef struct game_options {
	uint8_t threaded_2d_render;
	uint8_t depth_resolve_mode;
	uint8_t has_dynarec;
	uint8_t has_sound;
	int frameskip;
	int layout;
} game_options;

typedef struct game_entry {
	char name[256];
	game_options opt;
	struct game_entry *next;
} game_entry;

extern game_entry *launched_rom;

enum {
	LAYOUT_DEFAULT,
	LAYOUT_SIDE_BY_SIDE
};

#endif