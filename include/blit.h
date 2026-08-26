#ifndef BLIT_H
#define BLIT_H

#include <psptypes.h>

/* Visible LCD area. Text is clipped to this; it is never wrapped onto the
 * following row. */
#define BLIT_SCREEN_WIDTH 480
#define BLIT_SCREEN_HEIGHT 272

/* Glyphs are 8x8 but advance 7 pixels, matching the debug font's spacing. */
#define BLIT_GLYPH_SIZE 8
#define BLIT_CELL_WIDTH 7
#define BLIT_CELL_HEIGHT 8

/* Character cells that fit on screen. The last column still needs a full glyph
 * width, so this is not simply WIDTH / CELL_WIDTH. */
#define BLIT_COLS (((BLIT_SCREEN_WIDTH - BLIT_GLYPH_SIZE) / BLIT_CELL_WIDTH) + 1)
#define BLIT_ROWS (BLIT_SCREEN_HEIGHT / BLIT_CELL_HEIGHT)

/* A display buffer has to be at least as wide as the screen, and bufferwidth is
 * a power of two, so anything outside this range is a bad argument rather than a
 * stride. Bounding it means a clipped glyph cannot address past the buffer. */
#define BLIT_MIN_STRIDE BLIT_SCREEN_WIDTH
#define BLIT_MAX_STRIDE 4096

/* A framebuffer to draw into, described exactly as sceDisplaySetFrameBuf()
 * receives it. Passing the target in keeps the blitter free of both display
 * syscalls and shared cursor state, so it is re-entrant. */
typedef struct {
    void *base;
    int stride;
    int format;
} BlitTarget;

int blit_target_valid(const BlitTarget *target);

/**
 * Draw a string at a character cell, clipping at the screen edge.
 *
 * @param target Framebuffer to draw into.
 * @param col Character column of the first glyph.
 * @param row Character row.
 * @param msg Text to draw.
 * @param fg Foreground colour, as 0xBBGGRR.
 * @param bg Background colour, as 0xBBGGRR.
 */
void blit_text(const BlitTarget *target, int col, int row, const char *msg, u32 fg, u32 bg);

#endif
