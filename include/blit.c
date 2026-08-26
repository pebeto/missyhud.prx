/*
 * blit.c
 *
 * Glyph rasteriser for the HUD. The 8x8 font (msx) and the 8888 -> 16bpp colour
 * conversions come from scr_printf.c in the PSPSDK:
 *
 * Copyright (c) 2005 Marcus R. Brown <mrbrown@ocgnet.org>
 * Copyright (c) 2005 James Forshaw <tyranid@gmail.com>
 * Copyright (c) 2005 John Kelley <ps2dev@kelley.ca>
 *
 * Licensed under the BSD license, see LICENSE.pspsdk in this directory for
 * details.
 *
 * The cursor state machine, line wrapping, tab and newline handling and
 * per-call framebuffer lookup of the original have all been dropped: the caller
 * supplies the target, and text that would leave the screen is clipped rather
 * than wrapped.
 */
#include <stddef.h>

#include <pspdisplay.h>
#include <psptypes.h>

#include "blit.h"

/* 8x8 debug font, from libpspdebug. */
extern u8 msx[];

static u16 convert_8888_to_565(u32 color) {
    int r, g, b;

    b = (color >> 19) & 0x1F;
    g = (color >> 10) & 0x3F;
    r = (color >> 3) & 0x1F;

    return r | (g << 5) | (b << 11);
}

static u16 convert_8888_to_5551(u32 color) {
    int r, g, b, a;

    a = (color >> 24) ? 0x8000 : 0;
    b = (color >> 19) & 0x1F;
    g = (color >> 11) & 0x1F;
    r = (color >> 3) & 0x1F;

    return a | r | (g << 5) | (b << 10);
}

static u16 convert_8888_to_4444(u32 color) {
    int r, g, b, a;

    a = (color >> 28) & 0xF;
    b = (color >> 20) & 0xF;
    g = (color >> 12) & 0xF;
    r = (color >> 4) & 0xF;

    return (a << 12) | r | (g << 4) | (b << 8);
}

static u16 convert_8888(u32 color, int format) {
    switch (format) {
    case PSP_DISPLAY_PIXEL_FORMAT_565:
        return convert_8888_to_565(color);
    case PSP_DISPLAY_PIXEL_FORMAT_5551:
        return convert_8888_to_5551(color);
    case PSP_DISPLAY_PIXEL_FORMAT_4444:
        return convert_8888_to_4444(color);
    default:
        return convert_8888_to_565(color);
    }
}

int blit_target_valid(const BlitTarget *target) {
    return target != NULL && target->base != NULL &&
           target->stride >= BLIT_MIN_STRIDE && target->stride <= BLIT_MAX_STRIDE &&
           target->format >= PSP_DISPLAY_PIXEL_FORMAT_565 &&
           target->format <= PSP_DISPLAY_PIXEL_FORMAT_8888;
}

static void put_glyph_32(u32 *dst, int stride, u32 fg, u32 bg, u8 ch) {
    const u8 *glyph = &msx[(int)ch * BLIT_GLYPH_SIZE];
    int line;
    int bit;

    for (line = 0; line < BLIT_GLYPH_SIZE; line++, glyph++, dst += stride) {
        u8 bits = *glyph;

        for (bit = 0; bit < BLIT_GLYPH_SIZE; bit++) {
            dst[bit] = (bits & (0x80 >> bit)) ? fg : bg;
        }
    }
}

static void put_glyph_16(u16 *dst, int stride, u16 fg, u16 bg, u8 ch) {
    const u8 *glyph = &msx[(int)ch * BLIT_GLYPH_SIZE];
    int line;
    int bit;

    for (line = 0; line < BLIT_GLYPH_SIZE; line++, glyph++, dst += stride) {
        u8 bits = *glyph;

        for (bit = 0; bit < BLIT_GLYPH_SIZE; bit++) {
            dst[bit] = (bits & (0x80 >> bit)) ? fg : bg;
        }
    }
}

void blit_text(const BlitTarget *target, int col, int row, const char *msg, u32 fg,
               u32 bg) {
    int x;
    int y;

    if (!blit_target_valid(target) || msg == NULL) {
        return;
    }
    if (col < 0 || row < 0 || row >= BLIT_ROWS) {
        return;
    }

    x = col * BLIT_CELL_WIDTH;
    y = row * BLIT_CELL_HEIGHT;

    if (target->format == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        u32 *line = (u32 *)target->base + (y * target->stride);

        for (; *msg != '\0'; msg++, x += BLIT_CELL_WIDTH) {
            /* Clip instead of wrapping: the original advanced to the next row
             * and blanked it, which put a bar across the screen. */
            if (x + BLIT_GLYPH_SIZE > BLIT_SCREEN_WIDTH) {
                return;
            }
            put_glyph_32(line + x, target->stride, fg, bg, (u8)*msg);
        }
    } else {
        u16 *line = (u16 *)target->base + (y * target->stride);
        u16 fg16 = convert_8888(fg, target->format);
        u16 bg16 = convert_8888(bg, target->format);

        for (; *msg != '\0'; msg++, x += BLIT_CELL_WIDTH) {
            if (x + BLIT_GLYPH_SIZE > BLIT_SCREEN_WIDTH) {
                return;
            }
            put_glyph_16(line + x, target->stride, fg16, bg16, (u8)*msg);
        }
    }
}
