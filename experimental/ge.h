#ifndef GE_H
#define GE_H

#include <psptypes.h>

#include "blit.h"

/* Widest and tallest HUD the vertex buffer is sized for. Kept in step with
 * HUD_COLS and HUD_ROWS in gui.h, and enforced in geAddText(). */
#define GE_MAX_COLS 36
#define GE_MAX_ROWS 4

/**
 * Build the font texture and the static part of the display list.
 *
 * @param fg Foreground colour, as 0xBBGGRR.
 * @param bg Background colour, as 0xBBGGRR.
 *
 * @return 1 if the GE path is usable, 0 if it is not and the caller should keep
 *         blitting from the CPU.
 */
int geInit(u32 fg, u32 bg);

/**
 * Whether geInit() succeeded.
 */
int geReady(void);

/**
 * Release the GE callback. Has to run before this module's memory is freed,
 * because the driver holds pointers into it.
 */
void geShutdown(void);

/**
 * Start a new set of text. Discards whatever was queued before.
 */
void geBeginText(void);

/**
 * Append one line as a run of 8x8 sprites, at a character cell.
 *
 * @param col Character column of the first glyph.
 * @param row Character row.
 * @param msg Text to draw. Clipped at the screen edge and at GE_MAX_COLS.
 */
void geAddText(int col, int row, const char *msg);

/**
 * Finish a set of text, making the sprites visible to the GE.
 */
void geEndText(void);

/**
 * Point the list at a framebuffer and hand it to the GE.
 *
 * The list runs after whatever the game has already queued, so it draws on top
 * of a finished frame instead of racing the GE for the same pixels. It is
 * submitted without waiting, so the caller does not stall.
 *
 * @param target Framebuffer to draw into.
 */
void geSubmit(const BlitTarget *target);

/**
 * Whether any submission has been refused. The GE path turns itself off after
 * MAX_FAILURES in a row, so the caller falls back instead of drawing nothing.
 */
int geFailed(void);

/**
 * Result of the last refused submission, for reporting.
 */
u32 geLastError(void);

#endif
