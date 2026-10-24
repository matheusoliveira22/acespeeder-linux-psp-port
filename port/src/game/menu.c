/* The menus, game state 200: Menu_Update (0x41f300), Menu_Render (0x422990),
 * set up by 0x421a80 and freed by 0x41f1c0.
 *
 * The menu is a stack of up to three visible layers, each a screen with a
 * state and a timer: 300 while it is shown and takes input, 400 while it
 * leaves after a choice, 500 while it leaves going back, 100 when free. The
 * selection screens are carousels of objects turning around the centre. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../engine/font.h"
#include "../engine/lang.h"
#include "../engine/rm.h"
#include "../engine/sprite.h"
#include "../platform/platform.h"
#include "doors.h"
#include "fx.h"
#include "hud.h"
#include "machine.h"
#include "menu.h"
#include "race.h"
#include "save.h"
#include "settings.h"

/* screens */
enum {
	SC_NONE = 100, SC_DOORS = 200, SC_TITLE = 300, SC_MODE = 400, SC_MACHINE = 500, SC_STAGE = 600,
	SC_COURSE = 700, SC_START = 800, SC_DEMO = 900, SC_OPTIONS = 1000, SC_PASSWORD = 1100, SC_EXIT = 1200,
};
/* layer states */
enum { LS_FREE = 100, LS_SHOWN = 300, LS_DONE = 400, LS_BACK = 500 };

Menu g_menu;

static struct {
	RMFrame *scene, *camera, *sun;
	RMViewport *view;
	RMFrame *title;          /* 0x4517d8 */
	RMFrame *back;           /* 0x4517dc */
	RMFrame *stage;          /* 0x4517e0 */
	RMFrame *board;          /* 0x4517e8 the options and password panel */
	RMFrame *machines[8];    /* 0x4517ec */
	RMFrame *boxes[6];       /* 0x45180c the game modes */
	RMFrame *stages[6];      /* 0x451824 */
	RMFrame *courses[6];     /* 0x45183c */
	RMFrame *fx;             /* 0x45185c */
	Sprite menu;             /* 0x451b80 the three menu captions */
	Sprite null_blu;         /* 0x451d10 */
} M;

static const char *const k_modes[6] = {"AceSpeeder!", "Practice", "Replay", "Password", "Setting", "Exit"};
static const char *const k_machines[8] = {"Fire", "Mirage", "Dragon", "Shark", "Mouse", "Bear", "Surf", "Iron"};
static const char *const k_ranks[7] = {"Rank A", "Rank B", "Rank C", "Rank D", "Rank E", "Rank F", "Rank -"};
static const char *const k_letters[6] = {"Ａ", "Ｂ", "Ｃ", "Ｄ", "Ｅ", "Ｆ"};
static const char *const k_sounds[2][2] = {{"OFF", "SE"}, {"BGM", "BGM&SE"}};
static const char *const k_graphics[8] = {"SKY&OBJ&LIT", "SKY&OBJ", "SKY&LIT", "OBJ&LIT", "SKY", "OBJ", "LIT", "OFF"};

/* ---- layers (0x421a00, 0x421a40, 0x421a60) ---- */

static void layer_push(Menu *m, int screen)
{
	for (int i = 0; i < 5; i++) {
		if (m->layer[i].state == LS_FREE) {
			m->layer[i].screen = (int16_t)screen;
			m->layer[i].state = LS_SHOWN;
			m->layer[i].t = 0.0f;
			return;
		}
	}
}

static void layer_set_state(Menu *m, int i, int state)
{
	m->layer[i].state = (int16_t)state;
	m->layer[i].t = 0.0f;
}

static void layer_clear(Menu *m, int i)
{
	m->layer[i].screen = SC_NONE;
	m->layer[i].state = LS_FREE;
	m->layer[i].t = 0.0f;
}

/* Leave this screen for another one. */
static void go(Menu *m, int i, int screen, int state)
{
	layer_push(m, screen);
	layer_set_state(m, i, state);
	m->cooldown = 70.0f;
}

static int pressed(int key)
{
	return g_input[key] != 0 && g_inputPrev[key] == 0;
}

/* ---- scene ---- */

static void set_pose(RMFrame *f, Vec3 p, Mat3 r)
{
	rm_frame_set_position(f, M.scene, p.x, p.y, p.z);
	rm_frame_set_orientation(f, M.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
}

static void hide(RMFrame *f)
{
	rm_frame_set_position(f, M.scene, 0.0f, 10000.0f, 0.0f);
}

/* Objects slowly turn to follow the menu clock. */
static void face(Menu *m, RMFrame *f)
{
	double a = -m->time * 0.03;
	rm_frame_set_orientation(f, M.scene, (float)sin(a), 0.0f, (float)cos(a), 0.0f, 1.0f, 0.0f);
}

static void place(Menu *m, RMFrame *f, double x, double y, double z)
{
	rm_frame_set_position(f, M.scene, (float)x, (float)y, (float)z);
	face(m, f);
}

static RMMeshBuilder *load_mesh(const char *name, float scale, uint32_t quality)
{
	RMMeshBuilder *mb = rm_mb_create();
	rm_mb_load(mb, name);
	rm_mb_scale(mb, scale, scale, scale);
	rm_mb_set_quality(mb, quality);
	rm_mb_set_perspective(mb, 1);
	return mb;
}

static RMFrame *frame_with(RMMeshBuilder *mb, Vec3 pos)
{
	RMFrame *f = rm_frame_create(M.scene);
	set_pose(f, pos, Mat3_Identity());
	rm_frame_add_visual(f, mb);
	rm_release(mb);
	return f;
}

static void set_material(RMMeshBuilder *mb, float power, float spec, float emis)
{
	RMMaterial *mat = rm_material_create(power);
	rm_material_set_power(mat, power);
	rm_material_set_specular(mat, spec, spec, spec);
	rm_material_set_emissive(mat, emis, emis, emis);
	rm_mb_set_material(mb, mat);
	rm_release(mat);
}

/* 0x423c10: red -> green -> blue over t = 0..1 */
static void hue(double t, double *r, double *g, double *b)
{
	if (t <= 0.333) {
		double d = t * 3.003003003003003;
		*r = 1.0 - d, *g = d, *b = 0.0;
	}
	else if (t <= 0.666) {
		double d = (t - 0.333) * 3.003003003003003;
		*r = 0.0, *g = 1.0 - d, *b = d;
	}
	else {
		double d = (t - 0.666) * 2.994011976047904;
		*r = d, *g = 0.0, *b = 1.0 - d;
	}
}

/* 0x421a80 */
static void menu_setup(Menu *m)
{
	char name[32];
	Save_Reset(&g_save);
	m->state = 10;
	m->cooldown = 0.0f;
	m->cursor = 0;
	memset(m->code, 0, sizeof(m->code));
	g_phaseTime = 0.0;
	for (int i = 0; i < 5; i++) {
		layer_clear(m, i);
	}
	layer_push(m, SC_DOORS);

	rm_set_render_mode(1);
	M.scene = rm_frame_create(NULL);
	M.camera = rm_frame_create(M.scene);
	set_pose(M.camera, v3(0.0f, 45.0f, -200.0f), Mat3_Rotate(0.3f, 0));
	M.view = rm_viewport_create(M.camera, 0, 0, gfx_screen_w(), gfx_screen_h());
	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(M.scene, amb);
	rm_release(amb);
	M.sun = rm_frame_create(M.scene);
	set_pose(M.sun, v3(100.0f, 100.0f, 100.0f), Mat3_LookDir(v3(80.0f, 100.0f, -100.0f), v3(0, 0, 0)));
	RMLight *sun = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(M.sun, sun);
	rm_release(sun);

	RMMeshBuilder *mb = load_mesh("title.x", 40.0f, 0x89);
	set_material(mb, 2.0f, 1.2f, 0.5f);
	M.title = frame_with(mb, v3(0, 0, 0));
	mb = rm_mb_create();
	rm_mb_load(mb, "menuback.x");
	rm_mb_scale(mb, 300.0f, 1200.0f, 300.0f);
	rm_mb_set_quality(mb, 0x80);
	rm_mb_set_perspective(mb, 1);
	M.back = frame_with(mb, v3(0.0f, -250.0f, 0.0f));
	mb = load_mesh("stage.x", 120.0f, 0x89);
	RMTexture *tex = rm_texture_load("stage.bmp");
	rm_texture_set_decal_transparency(tex, 1);
	rm_texture_set_decal_transparent_color(tex, 0);
	rm_mb_set_texture(mb, tex);
	rm_release(tex);
	M.stage = frame_with(mb, v3(0.0f, -40.0f, 0.0f));
	M.board = frame_with(load_mesh("board.x", 60.0f, 0x88), v3(0.0f, -40.0f, 0.0f));
	for (int i = 0; i < 6; i++) {
		mb = rm_mb_create();
		rm_mb_load(mb, "box.x");
		rm_mb_scale(mb, 10.0f, 10.0f, 10.0f);
		rm_mb_set_quality(mb, 0x88);
		double r, g, b;
		hue(i * (1.0 / 6.0), &r, &g, &b);
		rm_mb_set_color_rgb(mb, (float)(r * 0.7 + 0.3), (float)(g * 0.7 + 0.3), (float)(b * 0.7 + 0.3));
		M.boxes[i] = frame_with(mb, v3(0.0f, 10000.0f, 0.0f));
	}
	for (int i = 0; i < 8; i++) {
		snprintf(name, sizeof(name), "machine%d.x", i + 1);
		mb = rm_mb_create();
		rm_mb_load(mb, name);
		rm_mb_scale(mb, 16.0f, 16.0f, 16.0f);
		rm_mb_set_quality(mb, 0x89);
		set_material(mb, 2.0f, 1.2f, 0.5f);
		M.machines[i] = frame_with(mb, v3(0.0f, 10000.0f, 0.0f));
	}
	for (int i = 0; i < 6; i++) {
		double k = i * (1.0 / 6.0);
		uint32_t hi = (uint32_t)(int)(((1.0 - k) * 0.6 + 0.4) * 255.0), lo = (uint32_t)(int)((k * 0.6 + 0.4) * 255.0);
		snprintf(name, sizeof(name), "board%d.x", i + 1);
		mb = load_mesh(name, 18.0f, 0x88);
		rm_mb_set_color(mb, 0xff00004cu | hi << 16 | lo << 8);
		M.stages[i] = frame_with(mb, v3(0.0f, 10000.0f, 0.0f));
		mb = load_mesh(name, 18.0f, 0x88);
		rm_mb_set_color(mb, 0xff004c00u | hi << 16 | lo);
		M.courses[i] = frame_with(mb, v3(0.0f, 10000.0f, 0.0f));
	}
	Doors_Init(M.camera, 1.0f);
	Fx_Init();
	M.fx = rm_frame_create(M.scene);
	set_pose(M.fx, v3(0, 0, 0), Mat3_Identity());
	RMUserVisual *uv = Fx_CreateVisual();
	rm_frame_add_visual(M.fx, uv);
	rm_release(uv);
	HudMsg_Init(&g_hudMsg);
	rm_viewport_set_back(M.view, 5000.0f);
	rm_viewport_set_field(M.view, 1.0f);
	sprite_load(&M.menu, "menu.bmp");
	sprite_load(&M.null_blu, "null_blu.bmp");
	Sound_PlayLoop(0);
}

/* 0x41f1c0 */
static void menu_free(Menu *m)
{
	m->state = 20;
	Doors_Free();
	Fx_Free();
	sprite_free(&M.menu);
	sprite_free(&M.null_blu);
	RMFrame **one[] = {&M.title, &M.back, &M.stage, &M.board, &M.fx, &M.sun, &M.camera};
	for (size_t i = 0; i < sizeof(one) / sizeof(one[0]); i++) {
		RM_RELEASE(*one[i]);
	}
	for (int i = 0; i < 8; i++) {
		RM_RELEASE(M.machines[i]);
	}
	for (int i = 0; i < 6; i++) {
		RM_RELEASE(M.boxes[i]);
		RM_RELEASE(M.stages[i]);
		RM_RELEASE(M.courses[i]);
	}
	RM_RELEASE(M.view);
	RM_RELEASE(M.scene);
}

/* ---- carousels ---- */

typedef struct {
	RMFrame **f;
	int n;
	float step;     /* angle between items */
	float turn;     /* ticks a turn to the next item takes */
	int16_t *sel;
} Carousel;

/* The items drop in one after another, the selected one in front; left and
 * right turn the ring by one item. */
static void carousel_shown(Menu *m, const Carousel *c, float t)
{
	for (int k = 0; k < c->n; k++) {
		if (!(t > k * 3.0)) {
			hide(c->f[k]);
			continue;
		}
		float a = (float)((k - *c->sel) * (double)c->step + 3.14);
		if (m->rotdir != 0) {
			int d = m->rotdir;
			a = (float)(-d * (double)c->step * m->rottime * (1.0 / c->turn) + a);
			if (m->rottime > c->turn) {
				m->rotdir = 0;
				int s = (d + *c->sel + c->n) % c->n;
				*c->sel = (int16_t)s;
			}
		}
		place(m, c->f[k], sin(a) * 100.0, Wave_Decay((float)((t - k * 3.0) * 0.03)) * 200.0, cos(a) * 100.0);
	}
}

/* Leaving: the others fly apart and drop; after a choice the chosen one rises
 * towards the camera. */
static void carousel_leave(Menu *m, const Carousel *c, float t, int chosen)
{
	for (int k = 0; k < c->n; k++) {
		int d = k - *c->sel;
		float a = (float)(d * (double)c->step + 3.14);
		if (chosen && d % c->n == 0) {
			place(m, c->f[k], 0.0, t * 1.3, -(t + t + 100.0));
		}
		else {
			place(m, c->f[k], sin(a) * (t + 100.0), Wave_Bounce((float)(t * 0.005)) * 400.0 - 400.0, cos(a) * (t + 100.0));
		}
	}
}

/* left/right while the ring is still */
static int carousel_input(Menu *m, float cooldown)
{
	if (g_input[IN_RIGHT]) {
		Sound_Play(4);
		m->rotdir = 1;
	}
	else if (g_input[IN_LEFT]) {
		Sound_Play(4);
		m->rotdir = -1;
	}
	else {
		return 0;
	}
	m->rottime = 0.0f;
	m->cooldown = cooldown;
	return 1;
}

/* The options and password panel, swaying slightly. */
static void board_pose(Menu *m, float t, int shown)
{
	Mat3 r = Mat3_Mul(Mat3_Rotate((float)(sin(m->time * 0.05) * 0.1), 1), Mat3_Rotate(0.3f, 0));
	Vec3 p = shown ? v3(0.0f, (float)(Wave_Decay((float)(t * 0.03)) * 200.0 + 20.0), -120.0f)
	               : v3(0.0f, (float)(Wave_Bounce((float)(t * 0.005)) * 400.0 - 380.0), (float)(-120.0 - t));
	rm_frame_set_position(M.board, M.scene, p.x, p.y, p.z);
	rm_frame_set_orientation(M.board, M.scene, -r.m[6], -r.m[7], -r.m[8], r.m[3], r.m[4], r.m[5]);
}

static void apply_sound_options(void)
{
	if (g_save.opt2[1] == 0) {
		Sound_Stop(0);
	}
	if (g_save.opt2[0] == 0) {
		Music_Stop();
	}
	g_soundOn = g_save.opt2[1];
	g_musicOn = g_save.opt2[0];
	if (g_save.opt2[1] != 0) {
		Sound_PlayLoop(0);
	}
	if (g_save.opt2[0] != 0) {
		Music_Play(0);
	}
}

static void options_input(Menu *m, int i)
{
	SaveData *s = &g_save;
	if (pressed(IN_RIGHT)) {
		Sound_Play(4);
		if (m->cursor == 0) { /* OFF <- SE <- BGM <- BGM&SE */
			if (--s->opt2[1] == -1) {
				s->opt2[0]--;
				s->opt2[1] = 1;
			}
			if (s->opt2[0] == -1) {
				s->opt2[0] = 1;
			}
			apply_sound_options();
		}
		else if (m->cursor == 1) {
			s->opt[3] = (int16_t)((s->opt[3] + 1) & 7);
		}
		else if (m->cursor == 2) {
			if (g_settings.fov < FOV_LEVELS - 1) {
				g_settings.fov++;
			}
		}
		else {
			g_settings.english = !g_settings.english;
			lang_set_english(g_settings.english);
		}
	}
	else if (pressed(IN_LEFT)) {
		Sound_Play(4);
		if (m->cursor == 0) {
			if (++s->opt2[1] == 2) {
				s->opt2[1] = 0;
				s->opt2[0]++;
			}
			if (s->opt2[0] == 2) {
				s->opt2[0] = 0;
			}
			apply_sound_options();
		}
		else if (m->cursor == 1) {
			s->opt[3] = (int16_t)((s->opt[3] + 7) & 7);
		}
		else if (m->cursor == 2) {
			if (g_settings.fov > 0) {
				g_settings.fov--;
			}
		}
		else {
			g_settings.english = !g_settings.english;
			lang_set_english(g_settings.english);
		}
	}
	else if (pressed(IN_UP) || pressed(IN_DOWN)) {
		/* the original has two rows; the port adds the field of view and the language */
		Sound_Play(4);
		m->cursor = (int16_t)((m->cursor + (pressed(IN_UP) ? 3 : 1)) % 4);
	}
	else if (pressed(IN_A)) {
		Sound_Play(7);
		Save_Write(s);
		Settings_Save();
		Save_Commit();
		m->cursor = 0;
		go(m, i, SC_MODE, LS_DONE);
	}
	else if (pressed(IN_B)) {
		Sound_Play(7);
		Save_Write(s);
		Settings_Save();
		Save_Commit();
		m->cursor = 0;
		go(m, i, SC_MODE, LS_BACK);
	}
}

static void password_reset(Menu *m, int i)
{
	Sound_Play(7);
	memset(m->code, 0, sizeof(m->code));
	m->cursor = 0;
	go(m, i, SC_MODE, LS_BACK);
}

/* Eight letters A-F; up/down change the letter at the cursor, A/right move on,
 * B/left move back, A on the last one checks the code. */
static void password_input(Menu *m, int i)
{
	if ((m->cursor == 0 && g_save.extra != 0) || m->cursor == 8) {
		if (pressed(IN_A) || pressed(IN_B)) {
			password_reset(m, i);
		}
		return;
	}
	if (pressed(IN_RIGHT)) {
		Sound_Play(4);
		if (m->cursor <= 6) {
			m->cursor++;
		}
	}
	else if (pressed(IN_LEFT)) {
		Sound_Play(4);
		if (m->cursor >= 1) {
			m->cursor--;
		}
	}
	else if (pressed(IN_A)) {
		Sound_Play(4);
		if (m->cursor <= 6) {
			m->cursor++;
		}
		else {
			m->cursor = 8;
			Save_CheckCode(&g_save, m->code);
			if (g_save.extra == 1) {
				Save_Write(&g_save);
				Save_Commit();
			}
		}
	}
	else if (pressed(IN_B)) {
		Sound_Play(4);
		if (m->cursor >= 1) {
			m->cursor--;
		}
	}
	else if (pressed(IN_UP)) {
		Sound_Play(4);
		m->code[m->cursor] = (int16_t)((m->code[m->cursor] + 5) % 6);
	}
	else if (pressed(IN_DOWN)) {
		Sound_Play(4);
		m->code[m->cursor] = (int16_t)((m->code[m->cursor] + 1) % 6);
	}
}

/* Stage 1 is always open; with the extra stages unlocked a stage needs the
 * previous one cleared. */
static int stage_open(void)
{
	if (g_save.stage == 0) {
		return 1;
	}
	return g_save.extra == 1 && g_save.stage_rank[g_save.stage - 1] != 6;
}

/* Attract mode after idling on the title (0x4217e8): replay a recorded race,
 * cycling through the courses and machines. */
static void start_demo(Menu *m)
{
	SaveData *s = &g_save;
	Music_Stop();
	menu_free(m);
	s->machine = s->opt[1];
	s->stage = (int16_t)floor(s->opt[0] * (1.0 / 6.0));
	s->course = (int16_t)(s->opt[0] % 6);
	s->opt[1] = (int16_t)((s->opt[1] + crt_rand() % 3 + 1) % 8);
	s->opt[0] = (int16_t)((s->opt[0] + crt_rand() % 4 + 1) % 36);
	Save_Write(s);
	if ((s->opt[2] % 2) != 0 && s->has_replay[s->stage][s->course] != 0) {
		s->opt[2]++;
		g_gameState = GS_RACE;
		s->mode = MODE_REPLAY;
		s->lives = 5;
		s->power = 100.0f;
	}
	else {
		g_gameState = GS_DEMO;
		s->opt[2]++;
	}
}

static void layer_update(Menu *m, int i)
{
	MenuLayer *l = &m->layer[i];
	float t = l->t;
	int ready = m->cooldown < 0.0f;
	Carousel modes = {M.boxes, 6, 1.0466666666666666f, 18.0f, &g_save.mode};
	Carousel machines = {M.machines, 8, 0.785f, 16.0f, &g_save.machine};
	Carousel stages = {M.stages, 6, 1.0466666666666666f, 15.0f, &g_save.stage};
	Carousel courses = {M.courses, 6, 1.0466666666666666f, 15.0f, &g_save.course};
	switch (l->screen) {
	case SC_DOORS:
		if (l->state == LS_SHOWN) {
			if (t < 0.01f) {
				Music_Play(0);
			}
			Doors_Open(t);
			if (t > 30.0f) {
				layer_push(m, SC_TITLE);
				l->state = LS_DONE;
			}
		}
		else if (l->state == LS_DONE) {
			Doors_Open(t);
			if (t > 100.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_TITLE:
		if (l->state == LS_FREE) {
			hide(M.title);
		}
		else if (l->state == LS_SHOWN) {
			place(m, M.title, 0.0, Wave_Bounce((float)(t * 0.014)) * 200.0, -50.0);
			if (t > 50.0f && pressed(IN_A)) {
				Sound_Play(7);
				layer_set_state(m, i, LS_DONE);
				layer_push(m, SC_MODE);
				m->cooldown = 70.0f;
			}
			if (t > 500.0f) {
				go(m, i, SC_DEMO, LS_DONE);
			}
		}
		else if (l->state == LS_DONE) {
			place(m, M.title, 0.0, Wave_Bounce((float)(t * 0.005)) * 400.0 - 400.0, -50.0 - t * 7.0);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_MODE:
		if (l->state == LS_FREE) {
			for (int k = 0; k < 5; k++) { /* the original misses the sixth box */
				hide(M.boxes[k]);
			}
		}
		else if (l->state == LS_SHOWN) {
			carousel_shown(m, &modes, t);
			if (!ready || carousel_input(m, 18.0f)) {
				break;
			}
			if (pressed(IN_A)) {
				static const int next[6] = {SC_MACHINE, SC_MACHINE, SC_STAGE, SC_PASSWORD, SC_OPTIONS, SC_EXIT};
				Sound_Play(7);
				go(m, i, next[g_save.mode], LS_DONE);
				g_save.machine = g_save.stage = g_save.course = 0;
			}
			else if (pressed(IN_B)) {
				Sound_Play(5);
				go(m, i, SC_TITLE, LS_BACK);
			}
		}
		else if (l->state == LS_DONE || l->state == LS_BACK) {
			carousel_leave(m, &modes, t, l->state == LS_DONE);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_MACHINE:
		if (l->state == LS_SHOWN) {
			carousel_shown(m, &machines, t);
			if (!ready || carousel_input(m, 16.0f)) {
				break;
			}
			if (pressed(IN_A)) {
				Sound_Play(7);
				if (g_save.extra == 1 || g_save.machine < 4) {
					go(m, i, SC_STAGE, LS_DONE);
				}
			}
			else if (pressed(IN_B)) {
				Sound_Play(5);
				go(m, i, SC_MODE, LS_BACK);
			}
		}
		else if (l->state == LS_DONE || l->state == LS_BACK) {
			carousel_leave(m, &machines, t, l->state == LS_DONE);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_STAGE:
		if (l->state == LS_SHOWN) {
			carousel_shown(m, &stages, t);
			if (!ready || carousel_input(m, 15.0f)) {
				break;
			}
			if (pressed(IN_A)) {
				Sound_Play(7);
				if (g_save.mode == MODE_REPLAY) {
					go(m, i, SC_COURSE, LS_DONE);
				}
				else if (stage_open()) {
					go(m, i, g_save.mode == MODE_GRANDPRIX ? SC_START : SC_COURSE, LS_DONE);
				}
			}
			else if (pressed(IN_B)) {
				Sound_Play(5);
				go(m, i, g_save.mode == MODE_REPLAY ? SC_MODE : SC_MACHINE, LS_BACK);
			}
		}
		else if (l->state == LS_DONE || l->state == LS_BACK) {
			carousel_leave(m, &stages, t, l->state == LS_DONE);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_COURSE:
		if (l->state == LS_SHOWN) {
			carousel_shown(m, &courses, t);
			if (!ready || carousel_input(m, 15.0f)) {
				break;
			}
			if (pressed(IN_A)) {
				Sound_Play(7);
				if (g_save.mode == MODE_TIMEATTACK && (g_save.extra == 1 || g_save.course < 3)) {
					go(m, i, SC_START, LS_DONE);
				}
				else if (g_save.mode == MODE_REPLAY && g_save.has_replay[g_save.stage][g_save.course] == 1) {
					go(m, i, SC_START, LS_DONE);
				}
			}
			else if (pressed(IN_B)) {
				Sound_Play(5);
				go(m, i, SC_STAGE, LS_BACK);
			}
		}
		else if (l->state == LS_DONE || l->state == LS_BACK) {
			carousel_leave(m, &courses, t, l->state == LS_DONE);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	case SC_START:
	case SC_DEMO:
	case SC_EXIT:
		if (l->state != LS_SHOWN) {
			break;
		}
		Doors_Close(t);
		if (t > 100.0f) {
			int screen = l->screen;
			layer_clear(m, i);
			if (screen == SC_DEMO) {
				start_demo(m);
				return;
			}
			Music_Stop();
			menu_free(m);
			if (screen == SC_EXIT) {
				g_gameState = GS_QUIT;
			}
			else if (g_save.mode <= MODE_REPLAY) {
				g_gameState = GS_RACE;
				g_save.lives = 5;
				g_save.power = 100.0f;
			}
		}
		break;
	case SC_OPTIONS:
	case SC_PASSWORD:
		if (l->state == LS_SHOWN) {
			board_pose(m, t, 1);
			if (ready) {
				if (l->screen == SC_OPTIONS) {
					options_input(m, i);
				}
				else {
					password_input(m, i);
				}
			}
		}
		else if (l->state == LS_DONE || l->state == LS_BACK) {
			board_pose(m, t, 0);
			if (t > 80.0f) {
				layer_clear(m, i);
			}
		}
		break;
	}
}

void Menu_Update(Menu *m)
{
	if (m->state == 20) {
		menu_setup(m);
	}
	Mat3 r = Mat3_Rotate(m->time * -0.0075f, 1);
	rm_frame_set_orientation(M.back, M.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
	r = Mat3_Rotate(m->time * 0.0225f, 1);
	rm_frame_set_orientation(M.stage, M.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
	for (int i = 0; i < 3; i++) {
		layer_update(m, i);
		if (m->state != 10) {
			return; /* left the menu */
		}
		m->layer[i].t = (float)(m->layer[i].t + 1.65);
	}
	m->time = (float)(m->time + 1.65);
	m->cooldown = (float)(m->cooldown - 1.65);
	m->rottime = (float)(m->rottime + 1.65);
}

/* ---- rendering ---- */

static uint32_t rgbf(float r, float g, float b)
{
	return (uint32_t)(int)(r * 255.0f) << 16 | (uint32_t)(int)(g * 255.0f) << 8 | (uint32_t)(int)(b * 255.0f);
}

static void text(int x, int y, int size, uint32_t rgb, int align, const char *s)
{
	gfx_begin_2d();
	gfx_set_blend(GFX_BLEND_ALPHA);
	font_draw(x, y, size, rgb, align, s);
	gfx_end_2d();
}

/* the caption under each selection screen */
static void heading(const char *s)
{
	text(320, 390, 12, 0xffffff, FONT_CENTER, s);
}

static void note(const char *s)
{
	text(320, 220, 10, rgbf(0.5f, 0.5f, 1.0f), FONT_CENTER, s);
}

static void render_password(Menu *m)
{
	text(320, 140, 12, 0xffffff, FONT_CENTER, "パスワード入力");
	if (m->cursor != 0 || g_save.extra == 0) {
		for (int k = 0; k < 8; k++) {
			if (m->code[k] >= 0 && m->code[k] <= 5) {
				text(215 + 30 * k, 200, 12, 0xffffff, FONT_CENTER, k_letters[m->code[k]]);
			}
		}
	}
	if (m->cursor == 0 && g_save.extra != 0) {
		text(320, 220, 12, 0xffffff, FONT_CENTER, "すでにパスワードは入力されています");
	}
	else if (m->cursor == 8) {
		text(320, 250, 12, 0xffffff, FONT_CENTER,
		     g_save.extra ? "パスワードは正しく入力されました" : "パスワードが間違っています");
	}
	else {
		text(215 + 30 * m->cursor, 220, 12, 0xffffff, FONT_CENTER, "￣");
	}
	text(320, 310, 12, 0xffffff, FONT_CENTER, "www.fitweb.or.jp/~nata");
	text(320, 340, 12, 0xffffff, FONT_CENTER, "www.vector.co.jp/authors/VA015095");
}

static void render_options(Menu *m)
{
	text(320, 140, 12, 0xffffff, FONT_CENTER, "オプション設定");
	text(120, 220 + m->cursor * 50, 12, 0xffffff, FONT_LEFT, "》");
	text(150, 220, 12, 0xffffff, FONT_LEFT, "サウンド");
	if (g_save.opt2[0] >= 0 && g_save.opt2[0] <= 1 && g_save.opt2[1] >= 0 && g_save.opt2[1] <= 1) {
		text(500, 220, 12, 0xffffff, FONT_RIGHT, k_sounds[g_save.opt2[0]][g_save.opt2[1]]);
	}
	text(150, 270, 12, 0xffffff, FONT_LEFT, "グラフィック");
	if (g_save.opt[3] >= 0 && g_save.opt[3] <= 7) {
		text(500, 270, 12, 0xffffff, FONT_RIGHT, k_graphics[g_save.opt[3]]);
	}
	text(150, 320, 12, 0xffffff, FONT_LEFT, "FOV");
	text(500, 320, 12, 0xffffff, FONT_RIGHT, Settings_FovName());
	text(150, 370, 12, 0xffffff, FONT_LEFT, "Language");
	text(500, 370, 12, 0xffffff, FONT_RIGHT, Settings_LangName());
}

static void render_texts(Menu *m, const MenuLayer *l)
{
	char buf[64];
	const SaveData *s = &g_save;
	switch (l->screen) {
	case SC_TITLE:
		heading("キーを押してください");
		break;
	case SC_MODE:
		heading("ゲームモードを選んでください");
		if (s->mode >= 0 && s->mode <= 5) {
			text(320, 300, 32, 0xffffff, FONT_CENTER, k_modes[s->mode]);
		}
		break;
	case SC_MACHINE:
		heading("マシンを選んでください");
		if (s->machine >= 0 && s->machine <= 7) {
			text(320, 300, 32, 0xffffff, FONT_CENTER, k_machines[s->machine]);
		}
		if (s->extra != 1 && s->machine > 3) {
			note("パスワードが必要です");
		}
		break;
	case SC_STAGE:
		heading("ステージを選んでください");
		snprintf(buf, sizeof(buf), "Stage %d", s->stage + 1);
		text(320, 300, 32, 0xffffff, FONT_CENTER, buf);
		if (s->mode == MODE_GRANDPRIX || s->mode == MODE_TIMEATTACK) {
			if (s->extra == 1) {
				if (s->stage != 0 && s->stage_rank[s->stage - 1] == 6) {
					snprintf(buf, sizeof(buf), "プレイ条件:Stage%dをクリア", s->stage);
					note(buf);
				}
			}
			else if (s->stage != 0) {
				note("パスワードが必要です");
			}
		}
		if (s->mode == MODE_GRANDPRIX && s->stage_rank[s->stage] >= 0 && s->stage_rank[s->stage] <= 6) {
			text(320, 260, 15, rgbf(0.5f, 1.0f, 0.0f), FONT_CENTER, k_ranks[s->stage_rank[s->stage]]);
		}
		break;
	case SC_COURSE:
		heading("ステージを選んでください");
		snprintf(buf, sizeof(buf), "Stage %d-%d", s->stage + 1, s->course + 1);
		text(320, 300, 32, 0xffffff, FONT_CENTER, buf);
		if (s->mode == MODE_TIMEATTACK && s->extra != 1 && s->course > 2) {
			note("パスワードが必要です");
		}
		else if (s->mode == MODE_REPLAY && s->has_replay[s->stage][s->course] == 0) {
			note("リプレイファイルがありません");
		}
		break;
	case SC_PASSWORD:
		if (l->t > 50.0f) {
			render_password(m);
		}
		break;
	case SC_OPTIONS:
		if (l->t > 50.0f) {
			render_options(m);
		}
		break;
	}
}

void Menu_Render(Menu *m)
{
	rm_viewport_clear(M.view);
	gfx_begin_2d();
	sprite_stretch(&M.null_blu, 0, 0, 160, 120, 0, 0, 640, 480);
	gfx_end_2d();
	rm_viewport_render(M.view, M.back);
	rm_viewport_render(M.view, M.stage);
	for (int i = 0; i < 3; i++) {
		const MenuLayer *l = &m->layer[i];
		if (l->state == LS_FREE) {
			continue;
		}
		switch (l->screen) {
		case SC_MACHINE:
			for (int k = 0; k < 8; k++) {
				rm_viewport_render(M.view, M.machines[k]);
			}
			break;
		case SC_MODE:
			for (int k = 0; k < 6; k++) {
				rm_viewport_render(M.view, M.boxes[k]);
			}
			break;
		case SC_TITLE:
			rm_viewport_render(M.view, M.title);
			break;
		case SC_STAGE:
			for (int k = 0; k < 6; k++) {
				rm_viewport_render(M.view, M.stages[k]);
			}
			break;
		case SC_COURSE:
			for (int k = 0; k < 6; k++) {
				rm_viewport_render(M.view, M.courses[k]);
			}
			break;
		case SC_OPTIONS:
		case SC_PASSWORD:
			rm_viewport_render(M.view, M.board);
			break;
		}
	}
	/* captions and the title logo */
	for (int i = 0; i < 3; i++) {
		const MenuLayer *l = &m->layer[i];
		if (l->state != LS_SHOWN) {
			continue;
		}
		int caption = l->screen == SC_MODE ? 0 : l->screen == SC_MACHINE ? 1
		              : (l->screen == SC_STAGE || l->screen == SC_COURSE) ? 2 : -1;
		if (caption >= 0) {
			gfx_begin_2d();
			gfx_set_blend(GFX_BLEND_ALPHA);
			sprite_blit(&M.menu, 0, caption * 60, 420, 60, 110, 80);
			gfx_end_2d();
		}
		else if (l->screen == SC_TITLE && l->t > 30.0f) {
			HudMsg_Menu(&g_hudMsg, (float)((l->t - 30.0f) * 0.01));
			Fx_SetMode(FX_ACE);
			rm_viewport_render(M.view, M.fx);
		}
	}
	for (int i = 0; i < 3; i++) {
		if (m->layer[i].state == LS_SHOWN) {
			render_texts(m, &m->layer[i]);
		}
	}
	for (int i = 0; i < 3; i++) {
		int sc = m->layer[i].screen;
		if (sc == SC_DOORS || sc == SC_START || sc == SC_DEMO || sc == SC_EXIT) {
			Doors_Render(M.view, M.scene, M.sun, Mat3_Rotate(0.3f, 0));
		}
	}
}

void Menu_Free(Menu *m)
{
	if (M.scene) {
		menu_free(m);
	}
}
