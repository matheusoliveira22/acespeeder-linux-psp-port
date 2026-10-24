/* Entry point and MainFrame (0x41be50): logic runs in fixed 33 ms ticks (at
 * most five per frame, then the clock is resynchronised), and one frame is
 * drawn after each batch of ticks.
 *
 * It starts with the title, or straight into a race: main [stage course machine mode],
 * 1-based numbers, mode 0 grand prix / 1 time attack / 2 replay (also read from
 * a file start.txt in the data folder, for the PSP).
 *
 * Debugging: ACESPEEDER_SHOTS="t1,t2,..." (or the same list in a file shots.txt
 * in the data folder, for the PSP) runs one logic tick per frame without waiting,
 * saves shot_<tick>.ppm at those ticks and quits after the last. ACESPEEDER_KEYS=
 * "t:k,t1-t2:k,..." presses key k (u d l r a b, s START, f meter, 1-4 views F1-F4; keys.txt on
 * the PSP) during tick t or ticks t1..t2. ACESPEEDER_RATE (or a file
 * rate.txt) logs ticks and frames per second to stderr or rate.log. ACESPEEDER_STATE
 * starts in another game state, e.g. 700 for the staff roll.
 *
 * Port additions: START (P on the PC) pauses and resumes a race and confirms
 * elsewhere; SELECT (F12) shows a performance meter. When a state has nothing
 * to draw yet (a course loading), no frame is shown, so the closed doors stay
 * on screen instead of a black one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/font.h"
#include "engine/prof.h"
#include "engine/rm.h"
#include "game/machine.h"
#include "game/race.h"
#include "game/save.h"
#include "game/settings.h"
#include "game/ending.h"
#include "game/menu.h"
#include "game/title.h"
#include "platform/platform.h"
#ifdef ACESPEEDER_SDL3
#include <SDL3/SDL_main.h> /* makes main what SDL3 expects (SDL_main on Android) */
#endif
#ifdef REPLAYCHECK_IN_GAME
int replaycheck_from_file(void); /* tools/replaycheck.c */
#endif

void Sound_LoadAll(void);

static void start_race(void)
{
	g_gameState = GS_RACE;
	g_race.state = 20;
}

static const char *g_keys; /* scripted key presses for debugging */
static int g_tick;
static int g_perf; /* performance meter shown */

static void scripted_keys(void)
{
	static const char names[] = "udrlab";
	static const int extra_keys[2] = {IN_START, IN_PERF}; /* s, f */
	for (const char *p = g_keys; p && *p;) {
		int t = atoi(p), t2 = t;
		const char *c = strchr(p, ':'), *dash = strchr(p, '-');
		if (dash && c && dash < c) {
			t2 = atoi(dash + 1);
		}
		if (c && g_tick >= t && g_tick <= t2 && c[1]) {
			if (strchr(names, c[1])) {
				g_input[strchr(names, c[1]) - names] = 1;
			}
			else if (c[1] == 's' || c[1] == 'f') {
				g_input[extra_keys[c[1] == 'f']] = 1;
			}
			else if (c[1] >= '1' && c[1] <= '4') {
				g_input[IN_F1 + c[1] - '1'] = 1;
			}
		}
		p = strchr(p, ',');
		p = p ? p + 1 : NULL;
	}
}

static void tick(void)
{
	memcpy(g_inputPrev, g_input, sizeof(g_input));
	memset(g_input, 0, sizeof(g_input));
	plat_read_input(g_input);
	g_tick++;
	scripted_keys();
	if (g_input[IN_START]) {
		int racing = g_gameState == GS_RACE && (g_racePhase == PH_RACING || g_racePhase == PH_PAUSED);
		g_input[racing ? IN_B : IN_A] = 1;
	}
	if (g_input[IN_PERF] && !g_inputPrev[IN_PERF]) {
		g_perf = !g_perf;
	}
	switch (g_gameState) {
	case GS_RACE:
		Race_Update(&g_race);
		break;
	case GS_TITLE:
		Title_Update(&g_title);
		break;
	case GS_MENU:
		Menu_Update(&g_menu);
		break;
	case GS_ENDING:
		Ending_Update(&g_ending);
		break;
	case GS_DEMO:
		Race_UpdateDemo(&g_race);
		break;
	}
}

/* Without the game data every screen would stay black: say what is missing. */
static int check_data(void)
{
	static const char *const required[] = {"tt.x", "title.x", "machine1.x", "course1.bmp", "engine.wav",
	                                       "track_1_1.bin"};
	const char *missing[8];
	int n = 0;
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		FILE *f = plat_open_data(required[i], "rb");
		if (f) {
			fclose(f);
		}
		else {
			missing[n++] = required[i];
		}
	}
	if (n == 0) {
		return 1;
	}
	while (!plat_should_quit()) {
		gfx_begin_frame();
		gfx_clear(0x000000, 1, 1);
		gfx_begin_2d();
		gfx_set_blend(GFX_BLEND_ALPHA);
		font_draw(320, 60, 14, 0xffff60, FONT_CENTER, "AceSpeeder!: game files not found");
		for (int i = 0; i < n; i++) {
			font_draw(320, 120 + 34 * i, 12, 0xffffff, FONT_CENTER, missing[i]);
		}
		font_draw(320, 360, 10, 0xc0c0ff, FONT_CENTER, "Copy the whole folder made by tools/make_dist.sh");
		font_draw(320, 390, 10, 0xc0c0ff, FONT_CENTER, "(dist/PSP/GAME/AceSpeeder), not only EBOOT.PBP.");
		gfx_end_2d();
		if (getenv("ACESPEEDER_SHOTS")) {
			plat_screenshot("shot_missing.ppm");
			gfx_end_frame();
			break;
		}
		gfx_end_frame();
		plat_sleep_ms(30);
	}
	plat_shutdown();
	return 0;
}

/* whether the current game state has something to draw */
static int can_render(void)
{
	switch (g_gameState) {
	case GS_RACE:
	case GS_DEMO:
		return g_race.state == 10;
	case GS_TITLE:
		return g_title.state == 10;
	case GS_MENU:
		return g_menu.state == 10;
	case GS_ENDING:
		return g_ending.state == 10;
	}
	return 0;
}

static void render(void)
{
	if (!can_render()) {
		return;
	}
	switch (g_gameState) {
	case GS_RACE:
		Race_Render(&g_race);
		break;
	case GS_TITLE:
		Title_Render(&g_title);
		break;
	case GS_MENU:
		Menu_Render(&g_menu);
		break;
	case GS_DEMO:
		Race_RenderDemo(&g_race);
		break;
	case GS_ENDING:
		Ending_Render(&g_ending);
		break;
	}
}

/* Averages over 30 frames of the time spent in the logic ticks, in building
 * the frame, waiting for the GPU, and between frames. */
static struct {
	uint32_t logic, draw, gpu, frame, last;
	int n;
	char text[96];
} g_meter;

static void meter_add(uint32_t logic, uint32_t draw)
{
	uint32_t now = plat_time_us();
	g_meter.logic += logic;
	g_meter.draw += draw;
	g_meter.gpu += gfx_gpu_wait_us();
	g_meter.frame += now - g_meter.last;
	g_meter.last = now;
	if (++g_meter.n == 30) {
		float k = 1.0f / (30 * 1000.0f);
		snprintf(g_meter.text, sizeof(g_meter.text), "logic %.1f  draw %.1f  gpu %.1f ms  %.1f fps",
		         g_meter.logic * k, g_meter.draw * k, g_meter.gpu * k,
		         g_meter.frame ? 30e6f / (float)g_meter.frame : 0.0f);
		g_meter.logic = g_meter.draw = g_meter.gpu = g_meter.frame = 0;
		g_meter.n = 0;
	}
}

static void meter_draw(void)
{
	if (!g_perf || !g_meter.text[0]) {
		return;
	}
	gfx_begin_2d();
	gfx_set_blend(GFX_BLEND_ALPHA);
	gfx_fill_rect_2d(0, 452, 640, 28, 0xa0000000u);
	font_draw(8, 456, 10, 0xffff80, FONT_LEFT, g_meter.text);
	gfx_end_2d();
}

int main(int argc, char **argv)
{
#ifdef REPLAYCHECK_IN_GAME
	/* Android: replaycheck.txt in the external files folder runs the replay check instead */
	int replaycheck = replaycheck_from_file();
	if (replaycheck >= 0) {
		return replaycheck;
	}
#endif
	if (plat_init() != 0) {
		return 1;
	}
	rm_init();
	font_init();
	if (!check_data()) {
		return 1;
	}
	FILE *shots_file = plat_open_data("shots.txt", "r");
	if (shots_file) {
		fclose(shots_file); /* unattended debug run: no dialog waiting for a button */
	}
	else {
		plat_save_load();
	}
	Sound_LoadAll();
	Save_Defaults(&g_save);
	Save_Load(&g_save);
	Settings_Load();
	Save_MarkKept();
	int start[4] = {0, 0, 0, -1};
	FILE *f = plat_open_data("start.txt", "r");
	if (f) {
		if (fscanf(f, "%d %d %d %d", &start[0], &start[1], &start[2], &start[3]) < 1) {
			start[0] = 0;
		}
		fclose(f);
	}
	for (int i = 1; i < argc && i <= 4; i++) {
		start[i - 1] = atoi(argv[i]);
	}
	if (start[0] > 0) {
		g_save.stage = (int16_t)(start[0] - 1);
	}
	if (start[1] > 0) {
		g_save.course = (int16_t)(start[1] - 1);
	}
	if (start[2] > 0) {
		g_save.machine = (int16_t)(start[2] - 1);
	}
	if (start[3] >= 0) {
		g_save.mode = (int16_t)start[3];
	}
	/* MainFrame's state 100 */
	g_soundOn = g_save.opt2[1];
	g_musicOn = g_save.opt2[0];
	g_title.state = 20;
	g_menu.state = 20;
	g_ending.state = 20;
	g_race.state = 20;
	if (start[0] > 0) {
		start_race();
	}
	else {
		g_gameState = GS_TITLE;
	}
	if (getenv("ACESPEEDER_STATE")) { /* debugging: start in another game state */
		g_gameState = atoi(getenv("ACESPEEDER_STATE"));
	}

	g_keys = getenv("ACESPEEDER_KEYS");
	if (!g_keys) { /* the same list in a file keys.txt, for the PSP */
		static char keys_buf[1024];
		FILE *kf = plat_open_data("keys.txt", "r");
		if (kf) {
			size_t n = fread(keys_buf, 1, sizeof(keys_buf) - 1, kf);
			keys_buf[n] = 0;
			fclose(kf);
			g_keys = keys_buf;
		}
	}
	char list[256] = "";
	const char *shots = getenv("ACESPEEDER_SHOTS");
	FILE *sf = shots ? NULL : plat_open_data("shots.txt", "r");
	if (sf) {
		shots = fgets(list, sizeof(list), sf) ? list : NULL;
		fclose(sf);
	}
	if (shots) {
		FILE *log = plat_open_save("ticks.log", "w");
		for (int t = 1; *shots && !plat_should_quit(); t++) {
			tick();
			if (log && (t % 50 == 0 || (g_machines[0].sp_active && g_machines[0].sp_timer < 1.0f))) {
				fprintf(log, "%d: state %d race %d phase %d time %.3f sub %d %.3f lap %d s %.3f sp %d/%d %.1f\n", t,
				        (int)g_gameState, g_race.state, (int)g_racePhase, g_phaseTime, g_race.sub, g_race.sub_time,
				        g_machines[0].lap, g_machines[0].tp.seg + g_machines[0].tp.t, g_machines[0].sp_active,
				        g_machines[0].sp_kind, g_machines[0].sp_timer);
			}
			uint32_t t1 = plat_time_us();
			gfx_begin_frame();
			render();
			meter_draw();
			meter_add(0, plat_time_us() - t1);
			if (atoi(shots) == t) {
				char path[64];
#ifdef __PSP__
				snprintf(path, sizeof(path), "save/shot_%d.ppm", t);
#else
				snprintf(path, sizeof(path), "shot_%d.ppm", t);
#endif
				plat_screenshot(path);
				shots = strchr(shots, ',') ? strchr(shots, ',') + 1 : "";
			}
			gfx_end_frame();
		}
		if (log) {
			fclose(log);
		}
		g_gameState = GS_QUIT;
	}
	uint32_t last = plat_ticks_ms();
	uint32_t rate_start = last;
	int rate_ticks = 0, rate_frames = 0;
	FILE *rate_file = plat_open_data("rate.txt", "r"); /* the PSP has no environment */
	int show_rate = getenv("ACESPEEDER_RATE") != NULL || rate_file != NULL;
	if (rate_file) {
		fclose(rate_file);
	}
	g_prof_on = show_rate;
	while (!plat_should_quit() && g_gameState != GS_QUIT) {
		uint32_t now = plat_ticks_ms();
		if (now < last) {
			last = now;
		}
		uint32_t elapsed = now - last;
		if (elapsed <= 32) {
			plat_sleep_ms(1);
			continue;
		}
		uint32_t t0 = plat_time_us();
		for (int n = 1;; n++) {
			tick();
			rate_ticks++;
			last += 33;
			if (n > 4) {
				last = plat_ticks_ms();
				break;
			}
			elapsed -= 33;
			if (elapsed < 33) {
				break;
			}
		}
		uint32_t t1 = plat_time_us();
		if (!can_render()) {
			continue; /* keep the last frame on screen */
		}
		gfx_begin_frame();
		render();
		meter_draw();
		uint32_t t2 = plat_time_us();
		gfx_end_frame();
		meter_add(t1 - t0, t2 - t1);
		rate_frames++;
		if (show_rate && plat_ticks_ms() - rate_start >= 5000) {
			FILE *out = plat_open_save("rate.log", "a");
			char prof[160];
			prof_report(prof, sizeof(prof), rate_frames);
			fprintf(out ? out : stderr, "%.1f ticks/s, %.1f frames/s, ms per frame: %s\n", rate_ticks / 5.0,
			        rate_frames / 5.0, prof);
			if (out) {
				fclose(out);
			}
			rate_start = plat_ticks_ms();
			rate_ticks = rate_frames = 0;
		}
	}
	Title_Free(&g_title);
	Menu_Free(&g_menu);
	Ending_Free(&g_ending);
	if (g_race.state == 10) {
		Race_Cleanup(&g_race);
	}
	plat_shutdown();
	return 0;
}
