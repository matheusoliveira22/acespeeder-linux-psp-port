/* Settings of the port that the original does not have, kept apart from its
 * gamedata.dat in settings.txt: the field of view, the volumes and the
 * language of the text. */
#ifndef SETTINGS_H
#define SETTINGS_H

#define FOV_LEVELS 4

typedef struct {
	int fov;       /* 0 original .. FOV_LEVELS-1 */
	int music;     /* volume in percent */
	int sfx;
	int english;   /* 1: the game's Japanese text in English */
} Settings;

extern Settings g_settings;

void Settings_Load(void);  /* also applies the volumes */
void Settings_Save(void);
float Settings_FovScale(void);       /* multiplier of the original field of view */
const char *Settings_FovName(void);
const char *Settings_LangName(void);

#endif
