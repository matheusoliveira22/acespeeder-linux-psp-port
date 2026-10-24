/* Plays shipped replays without drawing and prints how each one finishes:
 * the determinism check for a build (the time each replay should reach is on
 * the second line of its file).
 *
 * Usage: replaycheck [stage course] ... (1-based; no arguments: all 36)
 * Data and saves come from the platform layer as for the game, so
 * ACESPEEDER_DATA and ACESPEEDER_SAVE apply. One line per replay:
 *   <stage>-<course>: machine <m> <time> s, lap <lap>/<laps> <finished|replay ended>
 * Exits nonzero if a replay ends before its last lap.
 *
 * It sets up a race the way the port's main.c does for "main <stage> <course>
 * <machine> 2" and then calls Race_Update once per tick until the race is over;
 * the replay supplies the inputs. On Android, where an executable has no
 * window, main.c calls replaycheck_main when replaycheck.txt is in the app's
 * external files folder (see port/android/README.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <unistd.h>
#endif

#include "../engine/font.h"
#include "../engine/rm.h"
#include "../game/machine.h"
#include "../game/race.h"
#include "../game/save.h"
#include "../game/settings.h"
#include "../game/track.h"
#include "../platform/platform.h"

void Sound_LoadAll(void);

static int run(int stage, int course)
{
	Save_Defaults(&g_save);
	Save_Load(&g_save);
	g_save.stage = (int16_t)(stage - 1);
	g_save.course = (int16_t)(course - 1);
	g_save.mode = MODE_REPLAY;
	g_soundOn = 0;
	g_musicOn = 0;
	g_gameState = GS_RACE;
	g_race.state = 20;
	g_racePhase = PH_SETUP;
	int ticks = 0;
	while (ticks < 20000 && g_gameState == GS_RACE && !plat_should_quit()) {
		memcpy(g_inputPrev, g_input, sizeof(g_input));
		memset(g_input, 0, sizeof(g_input));
		Race_Update(&g_race);
		ticks++;
		if (g_race.state == 10 && g_racePhase != PH_SETUP && g_racePhase != PH_ROLLING &&
		    g_racePhase != PH_RACING) {
			break;
		}
	}
	const Machine *p = &g_machines[0];
	int finished = g_racePhase == PH_FINISHED && p->lap > g_track.laps;
	printf("%d-%d: machine %d %8.3f s, lap %d/%d %s\n", stage, course, g_save.machine, p->time, p->lap,
	       g_track.laps, finished ? "finished" : g_racePhase == PH_FINISHED ? "replay ended" : "stopped");
	fflush(stdout);
	if (g_race.state == 10) {
		Race_Cleanup(&g_race);
	}
	return finished;
}

#ifdef REPLAYCHECK_IN_GAME
int replaycheck_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
	if (plat_init() != 0) {
		return 1;
	}
#ifdef __ANDROID__
	dup2(2, 1); /* stdout to logcat, through the backend's stderr pipe */
#endif
	rm_init();
	font_init();
	Sound_LoadAll();
	Settings_Load();
	int failures = 0;
	if (argc > 2) {
		for (int i = 1; i + 1 < argc; i += 2) {
			failures += !run(atoi(argv[i]), atoi(argv[i + 1]));
		}
	}
	else {
		for (int s = 1; s <= 6; s++) {
			for (int c = 1; c <= 6; c++) {
				failures += !run(s, c);
			}
		}
	}
	plat_shutdown();
	return failures != 0;
}

#ifdef REPLAYCHECK_IN_GAME
/* Runs the check if replaycheck.txt exists in the app's external files folder:
 * its words are the stage/course pairs ("1 3 2 4"), none for all 36. Returns
 * the exit code, or -1 when there is no such file and the game should start. */
int replaycheck_from_file(void)
{
	const char *dir = SDL_GetAndroidExternalStoragePath();
	if (!dir) {
		return -1;
	}
	char path[1024], text[256];
	snprintf(path, sizeof(path), "%s/replaycheck.txt", dir);
	FILE *f = fopen(path, "r");
	if (!f) {
		return -1;
	}
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = 0;
	char *args[34] = {"replaycheck"};
	int argc = 1;
	char *save = NULL;
	for (char *w = strtok_r(text, " \t\r\n", &save); w && argc < 33; w = strtok_r(NULL, " \t\r\n", &save)) {
		args[argc++] = w;
	}
	args[argc] = NULL;
	return replaycheck_main(argc, args);
}
#endif
