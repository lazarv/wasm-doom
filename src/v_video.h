//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Gamma correction LUT.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//


#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"

// Needed because we are refering to patches.
#include "v_patch.h"

//
// VIDEO
//

#define CENTERY			(SCREENHEIGHT/2)


extern int dirtybox[4];

extern byte *tinttable;

// haleyjd 08/28/10: implemented for Strife support
// haleyjd 08/28/10: Patch clipping callback, implemented to support Choco
// Strife.
typedef boolean (*vpatchclipfunc_t)(patch_t *, int, int);
void V_SetPatchClipCallback(vpatchclipfunc_t func);

// Varying bit-depth support -POPE
//
// For bilinear filtering, each palette color is pre-weighted and put in a
// table for fast blending operations. These macros decide how many weights
// to create for each color. The lower the number, the lower the blend
// accuracy, which can produce very bad artifacts in texture filtering.
#define VID_NUMCOLORWEIGHTS 64
#define VID_COLORWEIGHTMASK (VID_NUMCOLORWEIGHTS-1)
#define VID_COLORWEIGHTBITS 6

// Palettes for converting from 8 bit color to 16 and 32 bit. Also
// contains the weighted versions of each palette color for filtering
// operations
extern unsigned short *V_Palette15;
extern unsigned short *V_Palette16;
extern unsigned int *V_Palette32;

#define VID_PAL15(color, weight) V_Palette15[ (color)*VID_NUMCOLORWEIGHTS + (weight) ]
#define VID_PAL16(color, weight) V_Palette16[ (color)*VID_NUMCOLORWEIGHTS + (weight) ]
#define VID_PAL32(color, weight) V_Palette32[ (color)*VID_NUMCOLORWEIGHTS + (weight) ]

// The available bit-depth modes
typedef enum {
  VID_MODE8,
  VID_MODE15,
  VID_MODE16,
  VID_MODE32,
  VID_MODEGL,
  VID_MODEMAX
} video_mode_t;

// Allocates buffer screens, call before R_Init.
void V_Init (void);

// Draw a block from the specified source screen to the screen.

void V_CopyRect(int srcx, int srcy, pixel_t *source,
                int width, int height,
                int destx, int desty);

void V_DrawPatch(int x, int y, patch_t *patch);
void V_DrawPatchFlipped(int x, int y, patch_t *patch);
void V_DrawTLPatch(int x, int y, patch_t *patch);
void V_DrawAltTLPatch(int x, int y, patch_t * patch);
void V_DrawShadowedPatch(int x, int y, patch_t *patch);
void V_DrawXlaPatch(int x, int y, patch_t * patch);     // villsa [STRIFE]
void V_DrawPatchDirect(int x, int y, patch_t *patch);

// Draw a linear block of pixels into the view buffer.

void V_DrawBlock(int x, int y, int width, int height, pixel_t *src);

void V_MarkRect(int x, int y, int width, int height);

void V_DrawFilledBox(int x, int y, int w, int h, int c);
void V_DrawHorizLine(int x, int y, int w, int c);
void V_DrawVertLine(int x, int y, int h, int c);
void V_DrawBox(int x, int y, int w, int h, int c);

// Draw a raw screen lump

void V_DrawRawScreen(pixel_t *raw);

// Temporarily switch to using a different buffer to draw graphics, etc.

void V_UseBuffer(pixel_t *buffer);

// Return to using the normal screen buffer to draw graphics.

void V_RestoreBuffer(void);

// Save a screenshot of the current screen to a file, named in the 
// format described in the string passed to the function, eg.
// "DOOM%02i.pcx"

void V_ScreenShot(const char *format);

// Load the lookup table for translucency calculations from the TINTTAB
// lump.

void V_LoadTintTable(void);

// villsa [STRIFE]
// Load the lookup table for translucency calculations from the XLATAB
// lump.

void V_LoadXlaTable(void);

void V_DrawMouseSpeedBox(int speed);

#endif

