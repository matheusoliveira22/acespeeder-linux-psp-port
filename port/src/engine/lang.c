/* English text for the game's Japanese strings. Every string goes through
 * font_draw, which looks it up here when English is on and draws the result
 * with font_draw_text (engine/font.c). Strings that are not in the tables
 * (all the ASCII ones: times, "Stage 1-2", ...) are drawn as they are.
 * English is a port setting (Language on the Setting screen, settings.txt). */
#include "lang.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

static int g_english = 1;

void lang_set_english(int on)
{
	g_english = on != 0;
}

int lang_english(void)
{
	return g_english;
}

typedef struct {
	const char *ja;
	const char *en;
} Pair;

/* Sizes are in the font's units: ASCII is `size` wide, full-width `2*size`,
 * so English at the same size is roughly as wide as the Japanese was. */
static const Pair k_pairs[] = {
	/* game/scene.c: pause menu */
	{"ＰＡＵＳＥ", "PAUSE"},
	{"ゲームを続ける", "Resume Game"},
	{"リプレイを続ける", "Resume Replay"},
	{"メニューに戻る", "Back to Menu"},
	/* game/scene.c: after a replay-less race */
	{"リプレイデータを保存しますか？", "Save replay data?"},
	{"保存する", "Save"},
	{"保存しない", "Don't Save"},
	/* game/scene.c: end of the demo stages */
	{"これから先のステージは", "The stages from here on"},
	{"パスワード入力をしてお楽しみください", "need a password to play."},
	/* game/menu.c: password and options screens */
	{"パスワード入力", "Enter Password"},
	{"すでにパスワードは入力されています", "Password already entered"},
	{"パスワードは正しく入力されました", "Password accepted"},
	{"パスワードが間違っています", "Wrong password"},
	{"オプション設定", "Options"},
	{"サウンド", "Sound"},
	{"グラフィック", "Graphics"},
	/* game/menu.c: selection screens */
	{"キーを押してください", "Press any key"},
	{"ゲームモードを選んでください", "Select a game mode"},
	{"マシンを選んでください", "Select a machine"},
	{"ステージを選んでください", "Select a stage"},
	{"パスワードが必要です", "Password required"},
	{"リプレイファイルがありません", "No replay file"},
	/* kept as they are: the password digits and the cursors are symbols, not
	 * text; listing them keeps them out of the "unknown string" log */
	{"Ａ", "Ａ"},
	{"Ｂ", "Ｂ"},
	{"Ｃ", "Ｃ"},
	{"Ｄ", "Ｄ"},
	{"Ｅ", "Ｅ"},
	{"Ｆ", "Ｆ"},
	{"￣", "￣"},
	{"》", "》"},
};

/* The staff roll's Japanese cards (game/ending.c, picked at random half the
 * time) show the English cards' text, at the English cards' places and sizes
 * (dy from the Japanese card's y, size 0 = unchanged). */
typedef struct {
	const char *text;
	int dy, size;
} Line;

typedef struct {
	const char *ja;
	Line lines[3];
} Card;

static const Card k_cards[] = {
	{"中谷崇宏作品", {{"A", -35, 22}, {"Takahiro Nakatani", 0, 22}, {"Work", 35, 22}}},
	{"監 督", {{"Directed by", 5, 22}}},
	{"中 谷 崇 宏", {{"Takahiro Nakatani", -5, 26}}},
	{"美 術", {{"Graphic by", 5, 22}}},
	{"川 上 亘", {{"Wataru Kawakami", -5, 26}}},
	{"音 響", {{"Sounds by", 5, 22}}},
	{"小 原 一 将", {{"Kazumasa Kohara", -5, 26}}},
	{"作 曲", {{"Composed by", 5, 22}}},
};

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* Strings with bytes >= 0x80 that are in no table, logged once each in debug
 * builds (or with ACESPEEDER_LANG_LOG set), so a missed string shows up. */
static void log_unknown(const char *s)
{
#ifdef NDEBUG
	if (!getenv("ACESPEEDER_LANG_LOG")) {
		return;
	}
#endif
	static char seen[32][128];
	static int nseen;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p < 0x80) {
			continue;
		}
		for (int i = 0; i < nseen; i++) {
			if (strcmp(seen[i], s) == 0) {
				return;
			}
		}
		if (nseen < (int)NELEM(seen)) {
			snprintf(seen[nseen++], sizeof(seen[0]), "%s", s);
		}
		fprintf(stderr, "lang: no translation for \"%s\"\n", s);
		return;
	}
}

static const char *find_pair(const char *s)
{
	for (size_t i = 0; i < NELEM(k_pairs); i++) {
		if (strcmp(s, k_pairs[i].ja) == 0) {
			return k_pairs[i].en;
		}
	}
	return NULL;
}

/* "プレイ条件:Stage%dをクリア" (the number is already formatted in) */
static const char *find_pattern(const char *s, char *buf, size_t n)
{
	static const char prefix[] = "プレイ条件:Stage";
	static const char suffix[] = "をクリア";
	if (strncmp(s, prefix, sizeof(prefix) - 1) != 0) {
		return NULL;
	}
	char *end;
	long stage = strtol(s + sizeof(prefix) - 1, &end, 10);
	if (end == s + sizeof(prefix) - 1 || strcmp(end, suffix) != 0) {
		return NULL;
	}
	snprintf(buf, n, "Clear Stage %ld to unlock", stage);
	return buf;
}

/* "《x》": the pause menu's blinking selection */
static const char *find_bracketed(const char *s, char *buf, size_t n)
{
	static const char open[] = "《", close[] = "》";
	size_t len = strlen(s), lo = sizeof(open) - 1, lc = sizeof(close) - 1;
	if (len < lo + lc || strncmp(s, open, lo) != 0 || strcmp(s + len - lc, close) != 0) {
		return NULL;
	}
	char inner[64];
	if (len - lo - lc >= sizeof(inner)) {
		return NULL;
	}
	memcpy(inner, s + lo, len - lo - lc);
	inner[len - lo - lc] = 0;
	const char *en = find_pair(inner);
	if (!en) {
		return NULL;
	}
	snprintf(buf, n, "%s%s%s", open, en, close);
	return buf;
}

void font_draw(int x, int y, int size, uint32_t rgb, int align, const char *text)
{
	if (!text || !g_english) {
		font_draw_text(x, y, size, rgb, align, text);
		return;
	}
	for (size_t i = 0; i < NELEM(k_cards); i++) {
		if (strcmp(text, k_cards[i].ja) == 0) {
			for (int j = 0; j < 3 && k_cards[i].lines[j].text; j++) {
				const Line *l = &k_cards[i].lines[j];
				font_draw_text(x, y + l->dy, l->size ? l->size : size, rgb, align, l->text);
			}
			return;
		}
	}
	char buf[96];
	const char *en = find_pair(text);
	if (!en) {
		en = find_pattern(text, buf, sizeof(buf));
	}
	if (!en) {
		en = find_bracketed(text, buf, sizeof(buf));
	}
	if (!en) {
		log_unknown(text);
		en = text;
	}
	font_draw_text(x, y, size, rgb, align, en);
}
