/* SDL3 backend for the platform layer (platform.h): window, files, input,
 * audio, and graphics on OpenGL ES 3.0 (Android) or OpenGL 4.1 core (macOS,
 * which has no GLES).
 *
 * The graphics reproduce what the port's Linux backend asks of OpenGL 1.x
 * fixed function, with one shader: per-vertex lighting with up to four lights
 * (GL's formulas: infinite viewer, colour material for ambient and diffuse,
 * spot lights left at a 180 degree cutoff), linear fog on the eye-space
 * depth |z| per fragment, texture modulate, alpha test at > 0.5. Vertices
 * are streamed into one buffer in GfxVertex layout. 2D is the original's
 * 640x480, scaled into the window with black bars, as on Linux. */
#if defined(__ANDROID__)
#define _BSD_SOURCE /* bionic declares funopen only with it */
#endif
#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_APPLE)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GLES3/gl3.h>
#endif

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#if defined(SDL_PLATFORM_ANDROID)
#include <android/log.h>
#include <unistd.h>
#endif

#include "../mixer.h"
#include "../platform.h"

#define VIRT_W 640
#define VIRT_H 480

static SDL_Window *g_win;
static SDL_GLContext g_gl;
static SDL_AudioStream *g_audio;
static int g_quit;
static int g_win_w = VIRT_W, g_win_h = VIRT_H; /* drawable size in pixels */

/* ------------------------------------------------------------------ files */

#define MAX_DATA_DIRS 8
static char g_data_dirs[MAX_DATA_DIRS][512];
static int g_ndata;
static char g_save_dir[512];

static void add_data_dir(const char *dir)
{
	if (dir && *dir && g_ndata < MAX_DATA_DIRS) {
		SDL_snprintf(g_data_dirs[g_ndata++], sizeof(g_data_dirs[0]), "%s", dir);
	}
}

/* Data folders, first match wins:
 * - Android: the app's external files folder (adb push start.txt, keys.txt,
 *   shots.txt or other files there to override), then the APK's assets/data;
 * - desktop: the folders in ACESPEEDER_DATA (separated by ':'), then data/
 *   next to the program (Contents/Resources/data in the macOS bundle).
 * Saves go to ACESPEEDER_SAVE, else SDL's per-user folder for the app. */
static void find_dirs(void)
{
#if defined(SDL_PLATFORM_ANDROID)
	add_data_dir(SDL_GetAndroidExternalStoragePath());
#endif
	const char *env = SDL_getenv("ACESPEEDER_DATA");
	if (env) {
		char list[2048];
		SDL_snprintf(list, sizeof(list), "%s", env);
		char *save = NULL;
		for (char *d = SDL_strtok_r(list, ":", &save); d; d = SDL_strtok_r(NULL, ":", &save)) {
			add_data_dir(d);
		}
	}
#if defined(SDL_PLATFORM_ANDROID)
	add_data_dir("data"); /* relative paths are APK assets for SDL_IOFromFile */
#else
	const char *base = SDL_GetBasePath();
	if (base) {
		char dir[512];
		SDL_snprintf(dir, sizeof(dir), "%sdata", base);
		add_data_dir(dir);
	}
#endif

	env = SDL_getenv("ACESPEEDER_SAVE");
	if (env && *env) {
		SDL_snprintf(g_save_dir, sizeof(g_save_dir), "%s", env);
		SDL_CreateDirectory(g_save_dir);
	}
	else {
		char *pref = SDL_GetPrefPath(NULL, "AceSpeeder");
		SDL_snprintf(g_save_dir, sizeof(g_save_dir), "%s", pref ? pref : ".");
		SDL_free(pref);
		size_t n = SDL_strlen(g_save_dir);
		if (n > 1 && g_save_dir[n - 1] == '/') {
			g_save_dir[n - 1] = 0;
		}
	}
}

/* stdio over an SDL_IOStream, so the port's FILE * code reads APK assets. */
static int io_read(void *cookie, char *buf, int n)
{
	SDL_IOStream *io = cookie;
	size_t got = SDL_ReadIO(io, buf, (size_t)n);
	if (got == 0 && SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR) {
		return -1;
	}
	return (int)got;
}

static fpos_t io_seek(void *cookie, fpos_t off, int whence)
{
	SDL_IOWhence w = whence == SEEK_SET ? SDL_IO_SEEK_SET : whence == SEEK_CUR ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
	Sint64 pos = SDL_SeekIO(cookie, (Sint64)off, w);
	return pos < 0 ? (fpos_t)-1 : (fpos_t)pos;
}

static int io_close(void *cookie)
{
	return SDL_CloseIO(cookie) ? 0 : -1;
}

static FILE *open_path(const char *path, const char *mode)
{
	if (mode[0] != 'r') {
		return fopen(path, mode);
	}
	SDL_IOStream *io = SDL_IOFromFile(path, "rb");
	if (!io) {
		return NULL;
	}
	FILE *f = funopen(io, io_read, NULL, io_seek, io_close);
	if (!f) {
		SDL_CloseIO(io);
	}
	return f;
}

/* The game's names do not always match the files' case ("Mapping1.bmp"), and
 * APK assets are case sensitive: retry in lower case. */
static FILE *open_in(const char *dir, const char *name, const char *mode)
{
	char path[1024];
	SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = open_path(path, mode);
	if (f || mode[0] != 'r') {
		return f;
	}
	char lower[256];
	size_t i = 0;
	for (; name[i] && i < sizeof(lower) - 1; i++) {
		lower[i] = (char)tolower((unsigned char)name[i]);
	}
	lower[i] = 0;
	if (SDL_strcmp(lower, name) == 0) {
		return NULL;
	}
	SDL_snprintf(path, sizeof(path), "%s/%s", dir, lower);
	return open_path(path, mode);
}

FILE *plat_open_data(const char *name, const char *mode)
{
	for (int i = 0; i < g_ndata; i++) {
		FILE *f = open_in(g_data_dirs[i], name, mode);
		if (f) {
			return f;
		}
	}
	return NULL;
}

FILE *plat_open_save(const char *name, const char *mode)
{
	FILE *f = open_in(g_save_dir, name, mode);
	if (!f && mode[0] == 'r') {
		f = plat_open_data(name, mode); /* the shipped replays and gamedata.dat */
	}
	return f;
}

void plat_save_load(void)
{
}

void plat_save_commit(void)
{
}

/* -------------------------------------------------------------- lifecycle */

static void SDLCALL audio_cb(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
	(void)userdata;
	(void)total;
	int16_t buf[1024 * 2];
	while (additional > 0) {
		int frames = additional / 4;
		frames = frames < 1 ? 1 : frames > 1024 ? 1024 : frames;
		mixer_render(buf, frames);
		SDL_PutAudioStreamData(stream, buf, frames * 4);
		additional -= frames * 4;
	}
}

void mixer_lock(void)
{
	if (g_audio) {
		SDL_LockAudioStream(g_audio);
	}
}

void mixer_unlock(void)
{
	if (g_audio) {
		SDL_UnlockAudioStream(g_audio);
	}
}

#if defined(SDL_PLATFORM_ANDROID)
/* SDL_Log writes to logcat and then to stderr as well, so a thread that
 * feeds stderr back into SDL_Log loops forever. Give SDL a log function that
 * only writes to logcat, and send the port's own stderr (missing files) to
 * logcat directly. */
static void SDLCALL log_to_logcat(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	(void)userdata;
	(void)category;
	__android_log_write(priority >= SDL_LOG_PRIORITY_ERROR ? ANDROID_LOG_ERROR :
	                    priority == SDL_LOG_PRIORITY_WARN ? ANDROID_LOG_WARN : ANDROID_LOG_INFO,
	                    "SDL/APP", message);
}

static int SDLCALL stderr_to_log(void *fd)
{
	char line[512];
	size_t n = 0;
	char c;
	while (read((int)(intptr_t)fd, &c, 1) == 1) {
		if (c != '\n' && n < sizeof(line) - 1) {
			line[n++] = c;
			continue;
		}
		line[n] = 0;
		__android_log_write(ANDROID_LOG_INFO, "SDL/APP", line);
		n = 0;
	}
	return 0;
}

static void redirect_stderr(void)
{
	int fds[2];
	SDL_SetLogOutputFunction(log_to_logcat, NULL);
	if (pipe(fds) == 0 && dup2(fds[1], 2) >= 0) {
		setvbuf(stderr, NULL, _IOLBF, 0);
		SDL_DetachThread(SDL_CreateThread(stderr_to_log, "stderr", (void *)(intptr_t)fds[0]));
	}
}
#endif

static int gfx_init(void);

int plat_init(void)
{
#if defined(SDL_PLATFORM_ANDROID)
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	redirect_stderr();
#endif
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return -1;
	}
	find_dirs();
#if defined(SDL_PLATFORM_ANDROID)
	/* shot_<tick>.ppm and other relative writes land where adb can pull them */
	if (SDL_GetAndroidExternalStoragePath()) {
		chdir(SDL_GetAndroidExternalStoragePath());
	}
#endif

#if defined(SDL_PLATFORM_ANDROID)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
	int scale = 1;
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	/* Window size in pixels as on Linux (ACESPEEDER_SCALE times 640x480), so
	 * screenshots from both backends line up. */
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	const char *env = SDL_getenv("ACESPEEDER_SCALE");
	int scale = env ? SDL_atoi(env) : 1;
	scale = scale < 1 ? 1 : scale;
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	g_win = SDL_CreateWindow("AceSpeeder!", VIRT_W * scale, VIRT_H * scale, flags);
	if (!g_win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return -1;
	}
	g_gl = SDL_GL_CreateContext(g_win);
	if (!g_gl) {
		fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
		return -1;
	}
	SDL_GL_MakeCurrent(g_win, g_gl);
	SDL_GL_SetSwapInterval(1);
	SDL_GetWindowSizeInPixels(g_win, &g_win_w, &g_win_h);
	SDL_DisableScreenSaver(); /* Android: keeps the screen on */
	if (gfx_init() != 0) {
		return -1;
	}

	mixer_init();
	SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, MIXER_RATE};
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
	g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
	if (g_audio) {
		SDL_ResumeAudioStreamDevice(g_audio);
	}
	else {
		fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
	}
	return 0;
}

#define MAX_PADS 4
static SDL_Gamepad *g_pads[MAX_PADS];

void plat_shutdown(void)
{
	if (g_audio) {
		SDL_AudioStream *s = g_audio;
		g_audio = NULL;
		SDL_DestroyAudioStream(s);
	}
	for (int i = 0; i < MAX_PADS; i++) {
		if (g_pads[i]) {
			SDL_CloseGamepad(g_pads[i]);
			g_pads[i] = NULL;
		}
	}
	SDL_GL_DestroyContext(g_gl);
	SDL_DestroyWindow(g_win);
	SDL_Quit();
}

uint32_t plat_ticks_ms(void)
{
	return (uint32_t)SDL_GetTicks();
}

uint32_t plat_time_us(void)
{
	return (uint32_t)(SDL_GetTicksNS() / 1000);
}

void plat_sleep_ms(int ms)
{
	SDL_Delay((Uint32)ms);
}

static void pad_added(SDL_JoystickID id)
{
	for (int i = 0; i < MAX_PADS; i++) {
		if (!g_pads[i]) {
			g_pads[i] = SDL_OpenGamepad(id);
			if (g_pads[i]) {
				SDL_Log("gamepad \"%s\" vid %04x pid %04x mapping %s", SDL_GetGamepadName(g_pads[i]),
				        SDL_GetGamepadVendor(g_pads[i]), SDL_GetGamepadProduct(g_pads[i]),
				        SDL_GetGamepadMapping(g_pads[i]));
			}
			return;
		}
	}
}

static void pad_removed(SDL_JoystickID id)
{
	for (int i = 0; i < MAX_PADS; i++) {
		if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
			SDL_CloseGamepad(g_pads[i]);
			g_pads[i] = NULL;
		}
	}
}

static void pump(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_EVENT_QUIT:
		case SDL_EVENT_TERMINATING:
			g_quit = 1;
			break;
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			SDL_GetWindowSizeInPixels(g_win, &g_win_w, &g_win_h);
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			pad_added(e.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			pad_removed(e.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			SDL_Log("pad button %s", SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button));
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			if (e.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER && e.gaxis.value > 16000) {
				SDL_Log("pad axis %s %d", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)e.gaxis.axis), e.gaxis.value);
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			SDL_Log("key %s", SDL_GetKeyName(e.key.key));
			break;
		case SDL_EVENT_DID_ENTER_BACKGROUND:
			if (g_audio) {
				SDL_PauseAudioStreamDevice(g_audio);
			}
			break;
		case SDL_EVENT_DID_ENTER_FOREGROUND:
			if (g_audio) {
				SDL_ResumeAudioStreamDevice(g_audio);
			}
			break;
		}
	}
}

int plat_should_quit(void)
{
	pump();
	return g_quit;
}

/* ------------------------------------------------------------------ input */

void plat_read_input(int32_t keys[IN_COUNT])
{
	pump();
	/* Keyboard: the original's DirectInput keys, plus the port's P (START)
	 * and F12 (meter), as on Linux. Android's back key is Escape. */
	const bool *k = SDL_GetKeyboardState(NULL);
	if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_KP_8]) keys[IN_UP] = 1;
	if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_KP_2]) keys[IN_DOWN] = 1;
	if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_KP_6]) keys[IN_RIGHT] = 1;
	if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_KP_4]) keys[IN_LEFT] = 1;
	if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) keys[IN_A] = 1;
	if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) keys[IN_B] = 1;
	if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_AC_BACK]) keys[IN_ESC] = 1;
	if (k[SDL_SCANCODE_F1]) keys[IN_F1] = 1;
	if (k[SDL_SCANCODE_F2]) keys[IN_F2] = 1;
	if (k[SDL_SCANCODE_F3]) keys[IN_F3] = 1;
	if (k[SDL_SCANCODE_F4]) keys[IN_F4] = 1;
	if (k[SDL_SCANCODE_P]) keys[IN_START] = 1;
	if (k[SDL_SCANCODE_F12]) keys[IN_PERF] = 1;

	/* Gamepads (the Thor's built-in one): D-pad or left stick, A accelerate
	 * and confirm, B back, R2 also accelerates, Start pauses, Select shows the
	 * meter, Y/X/L1/R1 the views F1-F4. Buttons by position (Xbox names). */
	const int dz = 32767 / 20; /* the original's joystick threshold, 50 of 1000 */
	for (int i = 0; i < MAX_PADS; i++) {
		SDL_Gamepad *p = g_pads[i];
		if (!p) {
			continue;
		}
		int ax = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX);
		int ay = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY);
		if (ay < -dz || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP)) keys[IN_UP] = 1;
		if (ay > dz || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) keys[IN_DOWN] = 1;
		if (ax > dz || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) keys[IN_RIGHT] = 1;
		if (ax < -dz || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) keys[IN_LEFT] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)) keys[IN_A] = 1;
		if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000) keys[IN_A] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST)) keys[IN_B] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_START)) keys[IN_START] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_BACK)) keys[IN_PERF] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_NORTH)) keys[IN_F1] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST)) keys[IN_F2] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) keys[IN_F3] = 1;
		if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) keys[IN_F4] = 1;
	}
}

/* --------------------------------------------------------------- graphics */

#if defined(SDL_PLATFORM_APPLE)
#define GLSL_HEADER "#version 410 core\n"
#else
#define GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#endif

static const char k_vertex_shader[] =
	"layout(location = 0) in vec2 a_uv;\n"
	"layout(location = 1) in vec4 a_color;\n"
	"layout(location = 2) in vec3 a_normal;\n"
	"layout(location = 3) in vec3 a_pos;\n"
	"uniform mat4 u_mvp, u_mv;\n"
	"uniform mat3 u_nmat;\n"
	"uniform int u_vcolor, u_lighting, u_nlights;\n"
	"uniform vec3 u_ambient;\n"
	"uniform vec4 u_lpos[4];\n" /* eye space; w = 0: directional, xyz towards the light */
	"uniform vec3 u_lcol[4], u_latt[4];\n"
	"uniform vec3 u_emissive, u_specular;\n"
	"uniform float u_power;\n"
	"out vec4 v_color;\n"
	"out vec2 v_uv;\n"
	"out float v_fogz;\n"
	"void main() {\n"
	"  vec4 eye = u_mv * vec4(a_pos, 1.0);\n"
	"  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
	"  vec4 c = u_vcolor != 0 ? a_color : vec4(1.0);\n"
	"  if (u_lighting != 0) {\n"
	/* GL 2.1 section 2.14.1 with the material's ambient and diffuse = c */
	"    vec3 n = normalize(u_nmat * a_normal);\n"
	"    vec3 sum = u_emissive + c.rgb * u_ambient;\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"      if (i >= u_nlights) break;\n"
	"      vec3 l = u_lpos[i].xyz;\n"
	"      float att = 1.0;\n"
	"      if (u_lpos[i].w != 0.0) {\n"
	"        l -= eye.xyz;\n"
	"        float d = length(l);\n"
	"        att = 1.0 / (u_latt[i].x + u_latt[i].y * d + u_latt[i].z * d * d);\n"
	"      }\n"
	"      l = normalize(l);\n"
	"      float ndl = max(dot(n, l), 0.0);\n"
	"      float spec = 0.0;\n"
	"      if (ndl > 0.0) {\n"
	"        float ndh = max(dot(n, normalize(l + vec3(0.0, 0.0, 1.0))), 0.0);\n"
	"        spec = u_power > 0.0 ? pow(ndh, u_power) : 1.0;\n"
	"      }\n"
	"      sum += att * u_lcol[i] * (ndl * c.rgb + spec * u_specular);\n"
	"    }\n"
	"    c = clamp(vec4(sum, c.a), 0.0, 1.0);\n"

	"  }\n"
	"  v_color = c;\n"
	"  v_uv = a_uv;\n"
	"  v_fogz = eye.z; /* abs() per fragment: a triangle may reach behind the eye */\n"
	"}\n";

static const char k_fragment_shader[] =
	"uniform sampler2D u_texture;\n"
	"uniform int u_textured, u_fog, u_alpha_test;\n"
	"uniform vec3 u_fog_color;\n"
	"uniform float u_fog_start, u_fog_end;\n"
	"in vec4 v_color;\n"
	"in vec2 v_uv;\n"
	"in float v_fogz;\n"
	"out vec4 o_color;\n"
	"void main() {\n"
	"  vec4 c = v_color;\n"
	"  if (u_textured != 0) c *= texture(u_texture, v_uv);\n"
	"  if (u_fog != 0) {\n"
	"    float f = clamp((u_fog_end - abs(v_fogz)) / (u_fog_end - u_fog_start), 0.0, 1.0);\n"
	"    c.rgb = mix(u_fog_color, c.rgb, f);\n"
	"  }\n"
	"  if (u_alpha_test != 0 && c.a <= 0.5) discard;\n"
	"  o_color = c;\n"
	"}\n";

static GLuint g_prog, g_vao, g_vbo;
static size_t g_vbo_size, g_vbo_pos;

static struct {
	GLint mvp, mv, nmat, vcolor, lighting, nlights, ambient, lpos, lcol, latt;
	GLint emissive, specular, power, texture, textured, fog, alpha_test, fog_color, fog_start, fog_end;
} u;

/* Fixed-function state, uploaded before a draw when it changed. */
static struct {
	float proj[16], mv[16], nmat[9];
	int lighting, nlights, textured, fog, alpha_test;
	float ambient[3], lpos[4][4], lcol[4][3], latt[4][3];
	float emissive[3], specular[3], power;
	float fog_color[3], fog_start, fog_end;
} st;
static int g_dirty = 1;
static int g_vcolor = -1;

#define MAX_TEX 1024
static GLuint g_tex[MAX_TEX];
static int g_tex_w[MAX_TEX], g_tex_h[MAX_TEX];

static GLuint compile(GLenum type, const char *src)
{
	const char *parts[2] = {GLSL_HEADER, src};
	GLuint s = glCreateShader(type);
	glShaderSource(s, 2, parts, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader: %s\n", log);
	}
	return s;
}

static int gfx_init(void)
{
	SDL_Log("GL %s, GLSL %s, %s", (const char *)glGetString(GL_VERSION),
	        (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION), (const char *)glGetString(GL_RENDERER));
	g_prog = glCreateProgram();
	GLuint vs = compile(GL_VERTEX_SHADER, k_vertex_shader);
	GLuint fs = compile(GL_FRAGMENT_SHADER, k_fragment_shader);
	glAttachShader(g_prog, vs);
	glAttachShader(g_prog, fs);
	glLinkProgram(g_prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetProgramInfoLog(g_prog, sizeof(log), NULL, log);
		fprintf(stderr, "shader link: %s\n", log);
		return -1;
	}
	glUseProgram(g_prog);
#define U(name) u.name = glGetUniformLocation(g_prog, "u_" #name)
	U(mvp), U(mv), U(nmat), U(vcolor), U(lighting), U(nlights), U(ambient), U(lpos), U(lcol), U(latt);
	U(emissive), U(specular), U(power), U(texture), U(textured), U(fog), U(alpha_test), U(fog_color);
	U(fog_start), U(fog_end);
#undef U
	glUniform1i(u.texture, 0);

	glGenVertexArrays(1, &g_vao);
	glBindVertexArray(g_vao);
	glGenBuffers(1, &g_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	g_vbo_size = 4u << 20;
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_vbo_size, NULL, GL_STREAM_DRAW);
	GLsizei stride = sizeof(GfxVertex);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(GfxVertex, u));
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)offsetof(GfxVertex, color));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(GfxVertex, nx));
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(GfxVertex, x));
	for (int i = 0; i < 4; i++) {
		glEnableVertexAttribArray((GLuint)i);
	}

	/* GL defaults as the Linux backend leaves them after its setup */
	glFrontFace(GL_CW);
	glCullFace(GL_BACK);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (int i = 0; i < 16; i++) {
		st.proj[i] = st.mv[i] = i % 5 == 0 ? 1.0f : 0.0f;
	}
	for (int i = 0; i < 9; i++) {
		st.nmat[i] = i % 4 == 0 ? 1.0f : 0.0f;
	}
	st.ambient[0] = st.ambient[1] = st.ambient[2] = 0.2f; /* GL's default scene ambient */
	st.fog_end = 1.0f;
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		fprintf(stderr, "GL setup error 0x%x\n", err);
	}
	return 0;
}

static void upload_state(int use_vertex_color)
{
	if (g_vcolor != use_vertex_color) {
		g_vcolor = use_vertex_color;
		glUniform1i(u.vcolor, use_vertex_color);
	}
	if (!g_dirty) {
		return;
	}
	g_dirty = 0;
	/* projection times modelview on the CPU, as fixed function does: the
	 * shader then rounds positions as the Linux backend does, and polygon
	 * edges cover the same pixels */
	float mvp[16];
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			float sum = 0.0f;
			for (int k = 0; k < 4; k++) {
				sum += st.proj[k * 4 + r] * st.mv[c * 4 + k];
			}
			mvp[c * 4 + r] = sum;
		}
	}
	glUniformMatrix4fv(u.mvp, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(u.mv, 1, GL_FALSE, st.mv);
	glUniformMatrix3fv(u.nmat, 1, GL_FALSE, st.nmat);
	glUniform1i(u.lighting, st.lighting);
	glUniform1i(u.nlights, st.nlights);
	glUniform3fv(u.ambient, 1, st.ambient);
	glUniform4fv(u.lpos, 4, &st.lpos[0][0]);
	glUniform3fv(u.lcol, 4, &st.lcol[0][0]);
	glUniform3fv(u.latt, 4, &st.latt[0][0]);
	glUniform3fv(u.emissive, 1, st.emissive);
	glUniform3fv(u.specular, 1, st.specular);
	glUniform1f(u.power, st.power);
	glUniform1i(u.textured, st.textured);
	glUniform1i(u.fog, st.fog);
	glUniform1i(u.alpha_test, st.alpha_test);
	glUniform3fv(u.fog_color, 1, st.fog_color);
	glUniform1f(u.fog_start, st.fog_start);
	glUniform1f(u.fog_end, st.fog_end);
}

/* Copies vertices into the stream buffer; returns the index of the first. */
static GLint stream_vertices(const GfxVertex *v, int count)
{
	size_t bytes = (size_t)count * sizeof(GfxVertex);
	if (g_vbo_pos + bytes > g_vbo_size) {
		while (bytes > g_vbo_size) {
			g_vbo_size *= 2;
		}
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_vbo_size, NULL, GL_STREAM_DRAW); /* orphan */
		g_vbo_pos = 0;
	}
	/* glBufferSubData, not glMapBufferRange: a map and unmap per draw is
	 * very slow on Adreno (3 fps in a race). */
	glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)g_vbo_pos, (GLsizeiptr)bytes, v);
	GLint first = (GLint)(g_vbo_pos / sizeof(GfxVertex)); /* every draw is whole vertices */
	g_vbo_pos += bytes;
	return first;
}

static void draw(GLenum mode, const GfxVertex *v, int count, int use_vertex_color)
{
	if (count <= 0) {
		return;
	}
	upload_state(use_vertex_color);
	GLint first = stream_vertices(v, count);
	if (first >= 0) {
		glDrawArrays(mode, first, count);
	}
}

int gfx_screen_w(void) { return VIRT_W; }
int gfx_screen_h(void) { return VIRT_H; }
float gfx_pixel_aspect(void) { return 1.0f; } /* square pixels: 4:3 with black bars */

/* A rectangle of the 640x480 screen in window pixels (GL's origin is bottom
 * left), at the largest scale that fits, centred; rounded as on Linux, so
 * both backends cover the same pixels. */
static void to_window(int x, int y, int w, int h, int r[4])
{
	float sx = (float)g_win_w / VIRT_W, sy = (float)g_win_h / VIRT_H;
	float s = sy < sx ? sy : sx;
	int left = (int)((g_win_w - VIRT_W * s) / 2);
	int bottom = (int)((g_win_h - VIRT_H * s) / 2);
	r[0] = left + (int)(x * s);
	r[1] = bottom + (int)((VIRT_H - y - h) * s);
	r[2] = (int)(w * s);
	r[3] = (int)(h * s);
}

static uint32_t g_swap_us;

void gfx_begin_frame(void)
{
	g_vbo_pos = g_vbo_size; /* orphan at the first draw: the GPU may still read last frame's */
	glViewport(0, 0, g_win_w, g_win_h);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_end_frame(void)
{
	uint32_t t = plat_time_us();
	SDL_GL_SwapWindow(g_win);
	g_swap_us = plat_time_us() - t;
}

uint32_t gfx_gpu_wait_us(void)
{
	return g_swap_us; /* the swap, which waits for the GPU and the display */
}

void plat_screenshot(const char *path)
{
	int w = g_win_w, h = g_win_h;
	unsigned char *px = malloc((size_t)w * h * 4);
	if (!px) {
		return;
	}
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	FILE *f = fopen(path, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		unsigned char *row = malloc((size_t)w * 3);
		for (int y = h - 1; y >= 0 && row; y--) {
			const unsigned char *src = px + (size_t)y * w * 4;
			for (int x = 0; x < w; x++) {
				memcpy(row + x * 3, src + x * 4, 3);
			}
			fwrite(row, 1, (size_t)w * 3, f);
		}
		free(row);
		fclose(f);
	}
	free(px);
}

void gfx_set_viewport(int x, int y, int w, int h)
{
	int r[4];
	to_window(x, y, w, h, r);
	glViewport(r[0], r[1], r[2], r[3]);
	glScissor(r[0], r[1], r[2], r[3]);
	glEnable(GL_SCISSOR_TEST);
}

void gfx_clear(uint32_t rgb, int color, int depth)
{
	glClearColor((rgb >> 16 & 0xff) / 255.0f, (rgb >> 8 & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, 1.0f);
	glDepthMask(GL_TRUE); /* left on afterwards, as on Linux */
	glClear((color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0));
}

void gfx_set_projection(float fx, float fy, float n, float f)
{
	/* left-handed view space, +z forward, to clip space; column major */
	memset(st.proj, 0, sizeof(st.proj));
	st.proj[0] = 1.0f / fx;
	st.proj[5] = 1.0f / fy;
	st.proj[10] = (f + n) / (f - n);
	st.proj[11] = 1.0f;
	st.proj[14] = -2.0f * f * n / (f - n);
	g_dirty = 1;
}

/* Normal matrix: inverse transpose of the upper 3x3, i.e. its cofactors over
 * the determinant (the shader normalises, as GL_NORMALIZE). */
static void update_normal_matrix(void)
{
	const float *m = st.mv; /* column major: element (row r, col c) is m[c * 4 + r] */
#define A(r, c) m[(c) * 4 + (r)]
	float cof[3][3];
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3, c2 = (c + 2) % 3;
			cof[r][c] = A(r1, c1) * A(r2, c2) - A(r1, c2) * A(r2, c1);
		}
	}
	float det = A(0, 0) * cof[0][0] + A(0, 1) * cof[0][1] + A(0, 2) * cof[0][2];
#undef A
	float inv = det != 0.0f ? 1.0f / det : 1.0f;
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			st.nmat[c * 3 + r] = cof[r][c] * inv;
		}
	}
}

void gfx_set_modelview(const float m[4][4])
{
	/* A row-major matrix for row vectors is laid out like a column-major
	 * one for column vectors. */
	memcpy(st.mv, &m[0][0], sizeof(st.mv));
	update_normal_matrix();
	g_dirty = 1;
}

void gfx_set_lighting(int on)
{
	st.lighting = on != 0;
	g_dirty = 1;
}

void gfx_set_lights(GfxColor ambient, const GfxLight *lights, int count)
{
	st.ambient[0] = ambient.r, st.ambient[1] = ambient.g, st.ambient[2] = ambient.b;
	st.nlights = count < GFX_MAX_LIGHTS ? count : GFX_MAX_LIGHTS;
	for (int i = 0; i < st.nlights; i++) {
		const GfxLight *l = &lights[i];
		if (l->type == GFX_LIGHT_DIRECTIONAL) {
			st.lpos[i][0] = -l->dir[0], st.lpos[i][1] = -l->dir[1], st.lpos[i][2] = -l->dir[2];
			st.lpos[i][3] = 0.0f;
		}
		else { /* point, spot (no cutoff, as on Linux) and ambient lights alike */
			st.lpos[i][0] = l->pos[0], st.lpos[i][1] = l->pos[1], st.lpos[i][2] = l->pos[2];
			st.lpos[i][3] = 1.0f;
		}
		st.lcol[i][0] = l->color.r, st.lcol[i][1] = l->color.g, st.lcol[i][2] = l->color.b;
		st.latt[i][0] = l->att0 > 0 ? l->att0 : 1.0f;
		st.latt[i][1] = l->att1;
		st.latt[i][2] = l->att2;
	}
	g_dirty = 1;
}

void gfx_set_material(GfxColor diffuse, GfxColor emissive, GfxColor specular, float power)
{
	(void)diffuse; /* ambient and diffuse are the vertex colours */
	st.emissive[0] = emissive.r, st.emissive[1] = emissive.g, st.emissive[2] = emissive.b;
	st.specular[0] = specular.r, st.specular[1] = specular.g, st.specular[2] = specular.b;
	st.power = power > 128.0f ? 128.0f : power;
	g_dirty = 1;
}

void gfx_set_fog(int on, uint32_t rgb, float start, float end)
{
	st.fog = on != 0;
	if (on) {
		st.fog_color[0] = (rgb >> 16 & 0xff) / 255.0f;
		st.fog_color[1] = (rgb >> 8 & 0xff) / 255.0f;
		st.fog_color[2] = (rgb & 0xff) / 255.0f;
		st.fog_start = start;
		st.fog_end = end;
	}
	g_dirty = 1;
}

void gfx_set_depth(int test, int write)
{
	if (test) {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
	}
	else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void gfx_set_depth_offset(int on)
{
	if (on) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -4.0f);
	}
	else {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}

void gfx_set_cull(int on)
{
	if (on) {
		glEnable(GL_CULL_FACE);
	}
	else {
		glDisable(GL_CULL_FACE);
	}
}

void gfx_set_blend(int mode)
{
	if (mode == GFX_BLEND_OFF) {
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	if (mode == GFX_BLEND_ADD) {
		glBlendFunc(GL_ONE, GL_ONE);
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

void gfx_set_alpha_test(int on)
{
	st.alpha_test = on != 0;
	g_dirty = 1;
}

static void set_sampling(int linear, int repeat)
{
	GLint f = linear ? GL_LINEAR : GL_NEAREST, w = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, w);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, w);
}

int gfx_texture_create(int w, int h, const uint32_t *rgba, int filter_linear)
{
	int id = 0;
	while (id < MAX_TEX && g_tex[id]) {
		id++;
	}
	if (id == MAX_TEX) {
		return -1;
	}
	glGenTextures(1, &g_tex[id]);
	glBindTexture(GL_TEXTURE_2D, g_tex[id]);
	set_sampling(filter_linear, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	g_tex_w[id] = w;
	g_tex_h[id] = h;
	return id;
}

void gfx_texture_destroy(int tex)
{
	if (tex >= 0 && tex < MAX_TEX && g_tex[tex]) {
		glDeleteTextures(1, &g_tex[tex]);
		g_tex[tex] = 0;
	}
}

void gfx_texture_bind(int tex)
{
	int on = tex >= 0 && tex < MAX_TEX && g_tex[tex];
	if (on) {
		glBindTexture(GL_TEXTURE_2D, g_tex[tex]);
	}
	if (st.textured != on) {
		st.textured = on;
		g_dirty = 1;
	}
}

void gfx_draw_triangles(const GfxVertex *v, int count, int use_vertex_color)
{
	draw(GL_TRIANGLES, v, count, use_vertex_color);
}

void gfx_draw_triangles_temp(const GfxVertex *v, int count, int use_vertex_color)
{
	draw(GL_TRIANGLES, v, count, use_vertex_color); /* streamed: always a copy */
}

void gfx_draw_triangles_box(const GfxVertex *v, int count, int use_vertex_color, const float box[6])
{
	(void)box;
	draw(GL_TRIANGLES, v, count, use_vertex_color);
}

void gfx_draw_lines(const GfxVertex *v, int count)
{
	draw(GL_LINES, v, count, 1);
}

void gfx_begin_2d(void)
{
	gfx_set_viewport(0, 0, VIRT_W, VIRT_H);
	/* orthographic 640x480, y down, z -1..1; identity modelview */
	memset(st.proj, 0, sizeof(st.proj));
	st.proj[0] = 2.0f / VIRT_W;
	st.proj[5] = -2.0f / VIRT_H;
	st.proj[10] = -1.0f;
	st.proj[12] = -1.0f;
	st.proj[13] = 1.0f;
	st.proj[15] = 1.0f;
	static const float identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
	gfx_set_modelview(identity);
	st.lighting = 0;
	st.fog = 0;
	st.alpha_test = 1;
	g_dirty = 1;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
}

static void quad(GfxVertex q[6], float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 uint32_t color)
{
	/* two triangles: top left, top right, bottom right; top left, bottom right, bottom left */
	const float xs[6] = {x, x + w, x + w, x, x + w, x}, ys[6] = {y, y, y + h, y, y + h, y + h};
	const float us[6] = {u0, u1, u1, u0, u1, u0}, vs[6] = {v0, v0, v1, v0, v1, v1};
	for (int i = 0; i < 6; i++) {
		q[i] = (GfxVertex){us[i], vs[i], color, 0, 0, 1, xs[i], ys[i], 0};
	}
}

void gfx_draw_quad_2d(int tex, float sx, float sy, float sw, float sh,
                      float dx, float dy, float dw, float dh, uint32_t color)
{
	if (tex < 0 || tex >= MAX_TEX || !g_tex[tex]) {
		return;
	}
	float tw = (float)g_tex_w[tex], th = (float)g_tex_h[tex];
	GfxVertex q[6];
	quad(q, dx, dy, dw, dh, sx / tw, sy / th, (sx + sw) / tw, (sy + sh) / th, color);
	gfx_texture_bind(tex);
	set_sampling(0, 0);
	draw(GL_TRIANGLES, q, 6, 1);
	/* The Linux backend leaves every texture drawn in 2D linear and
	 * repeating afterwards, whatever it was created with; so does this. */
	set_sampling(1, 1);
}

void gfx_fill_rect_2d(float x, float y, float w, float h, uint32_t color)
{
	GfxVertex q[6];
	quad(q, x, y, w, h, 0, 0, 0, 0, color);
	gfx_texture_bind(-1);
	draw(GL_TRIANGLES, q, 6, 1);
}

void gfx_end_2d(void)
{
	st.alpha_test = 0;
	g_dirty = 1;
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}
