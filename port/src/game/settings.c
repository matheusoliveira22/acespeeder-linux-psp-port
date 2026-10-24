#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/lang.h"
#include "../platform/platform.h"

Settings g_settings = {1, 100, 60, 1};

static const float k_fov_scale[FOV_LEVELS] = {1.0f, 1.15f, 1.3f, 1.45f};
static const char *const k_fov_name[FOV_LEVELS] = {"Original", "Wide", "Wider", "Widest"};

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void apply(void)
{
	snd_set_volumes(g_settings.music / 100.0f, g_settings.sfx / 100.0f);
	lang_set_english(g_settings.english);
}

void Settings_Load(void)
{
	FILE *f = plat_open_save("settings.txt", "r");
	if (f) {
		char line[64];
		while (fgets(line, sizeof(line), f)) {
			int v;
			if (sscanf(line, "fov=%d", &v) == 1) {
				g_settings.fov = clamp(v, 0, FOV_LEVELS - 1);
			}
			else if (sscanf(line, "music=%d", &v) == 1) {
				g_settings.music = clamp(v, 0, 200);
			}
			else if (sscanf(line, "sfx=%d", &v) == 1) {
				g_settings.sfx = clamp(v, 0, 200);
			}
			else if (strncmp(line, "lang=", 5) == 0) {
				g_settings.english = line[5] != 'j';
			}
		}
		fclose(f);
	}
	/* ACESPEEDER_LANG=ja|en overrides the file (getenv is NULL on the PSP) */
	const char *env = getenv("ACESPEEDER_LANG");
	if (env && *env) {
		g_settings.english = env[0] != 'j';
	}
	apply();
}

void Settings_Save(void)
{
	FILE *f = plat_open_save("settings.txt", "w");
	if (f) {
		fprintf(f, "fov=%d\nmusic=%d\nsfx=%d\nlang=%s\n", g_settings.fov, g_settings.music, g_settings.sfx,
		        g_settings.english ? "en" : "ja");
		fclose(f);
	}
	apply();
}

float Settings_FovScale(void)
{
	return k_fov_scale[clamp(g_settings.fov, 0, FOV_LEVELS - 1)];
}

const char *Settings_FovName(void)
{
	return k_fov_name[clamp(g_settings.fov, 0, FOV_LEVELS - 1)];
}

const char *Settings_LangName(void)
{
	return g_settings.english ? "English" : "Japanese";
}
