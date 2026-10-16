#ifndef GE_FONT_H
#define GE_FONT_H

#include "ge_gfx.h"

/* Built-in 5x7 pixel font: A-Z (lower case is drawn as upper case), 0-9 and
 * ! ? . , : - + / ' % ( ). Glyphs advance 6px * scale. */

void ge_text(GeImage *dst, int x, int y, const char *s, uint32_t color, int scale);
/* same, with a 1px (scaled) dark outline so it reads over any background */
void ge_text_outlined(GeImage *dst, int x, int y, const char *s, uint32_t color, uint32_t outline, int scale);
int  ge_text_width(const char *s, int scale);

#endif
