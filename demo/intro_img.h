#ifndef INTRO_IMG_H
#define INTRO_IMG_H

/* The picture shown for three seconds before the game starts (intro_img.c, made by tools/make_intro.py). */
#define INTRO_W 180
#define INTRO_H 240
extern const unsigned char g_intro_bg[3];                  /* the plain colour around it */
extern const unsigned short g_intro_pix[INTRO_W * INTRO_H]; /* 5-6-5, row by row */

#endif
