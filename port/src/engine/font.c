#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"
#include "font.h"
#include "font_data.h"

#define ATLAS_W 512
/* Transparent gap around each glyph. The PSP shrinks the atlas with a box
 * filter and samples it linearly, which without it blends the bottom of the
 * glyph above into the top of a letter (a line over it). */
#define PAD 8

static int tex = -1;
static int atlas_h;
static short glyph_x[FONT_NGLYPHS], glyph_y[FONT_NGLYPHS];

void font_init(void)
{
	int x = PAD, y = PAD;
	for (int i = 0; i < FONT_NGLYPHS; i++) {
		if (x + font_glyphs[i].w + PAD > ATLAS_W) {
			x = PAD;
			y += FONT_CELL + PAD;
		}
		glyph_x[i] = (short)x, glyph_y[i] = (short)y;
		x += font_glyphs[i].w + PAD;
	}
	atlas_h = 1;
	while (atlas_h < y + FONT_CELL + PAD) {
		atlas_h *= 2;
	}
	uint32_t *px = calloc((size_t)ATLAS_W * atlas_h, 4);
	for (int i = 0; i < FONT_NGLYPHS; i++) {
		const unsigned char *b = font_bits + font_glyphs[i].offset;
		int w = font_glyphs[i].w;
		for (int j = 0; j < w * FONT_CELL; j++) {
			unsigned a = (b[j / 4] >> (2 * (j % 4)) & 3) * 85;
			px[(glyph_y[i] + j / w) * ATLAS_W + glyph_x[i] + j % w] = 0x00ffffffu | a << 24;
		}
	}
	tex = gfx_texture_create(ATLAS_W, atlas_h, px, 0); /* a 2D image */
	free(px);
}

static unsigned next_code(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	unsigned c = *p++;
	if (c >= 0xf0) {
		c = (c & 7) << 18 | (p[0] & 0x3fu) << 12 | (p[1] & 0x3fu) << 6 | (p[2] & 0x3fu), p += 3;
	}
	else if (c >= 0xe0) {
		c = (c & 15) << 12 | (p[0] & 0x3fu) << 6 | (p[1] & 0x3fu), p += 2;
	}
	else if (c >= 0xc0) {
		c = (c & 31) << 6 | (p[0] & 0x3fu), p += 1;
	}
	*s = (const char *)p;
	return c;
}

static int find(unsigned code)
{
	int lo = 0, hi = FONT_NGLYPHS - 1;
	while (lo <= hi) {
		int m = (lo + hi) / 2;
		if (font_glyphs[m].code == code) {
			return m;
		}
		if (font_glyphs[m].code < code) {
			lo = m + 1;
		}
		else {
			hi = m - 1;
		}
	}
	return -1;
}

int font_width(int size, const char *text)
{
	int w = 0;
	while (*text) {
		unsigned c = next_code(&text);
		int g = find(c);
		w += g >= 0 && font_glyphs[g].w == FONT_CELL ? 2 * size : size;
	}
	return w;
}

void font_draw_text(int x, int y, int size, uint32_t rgb, int align, const char *text)
{
	if (align == FONT_CENTER) {
		x -= font_width(size, text) / 2;
	}
	else if (align == FONT_RIGHT) {
		x -= font_width(size, text);
	}
	/* 0xRRGGBB -> vertex colour 0xAABBGGRR */
	uint32_t color = 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
	while (*text) {
		int g = find(next_code(&text));
		if (g < 0) {
			x += size;
			continue;
		}
		int w = font_glyphs[g].w == FONT_CELL ? 2 * size : size;
		gfx_draw_quad_2d(tex, glyph_x[g], glyph_y[g], font_glyphs[g].w, FONT_CELL, (float)x, (float)y, (float)w,
		                 (float)(2 * size), color);
		x += w;
	}
}
