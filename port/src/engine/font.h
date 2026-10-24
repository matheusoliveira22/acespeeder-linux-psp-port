/* Text drawing in place of the original's GDI TextOut with "MS Gothic". */
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

enum { FONT_LEFT, FONT_CENTER, FONT_RIGHT };

void font_init(void);
/* UTF-8 text at (x, y) of the 640x480 screen; size as GDI's CreateFont width:
 * half-width characters are size wide and 2*size tall; rgb is 0xRRGGBB. Call between
 * gfx_begin_2d and gfx_end_2d. */
void font_draw(int x, int y, int size, uint32_t rgb, int align, const char *text);
/* font_draw without the translation (engine/lang.c defines font_draw, which
 * swaps the game's Japanese strings for English when that is chosen) */
void font_draw_text(int x, int y, int size, uint32_t rgb, int align, const char *text);
int font_width(int size, const char *text);

#endif
