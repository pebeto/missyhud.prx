#ifndef GUI_H
#define GUI_H

#include <pspkerneltypes.h>

#include "include/blit.h"

#define BG_COLOR 0x000000
#define FG_COLOR 0xFFFFFF

// Widest line the HUD can produce, with every field clamped:
// "Power: 100% (999 mins) (charging...)".
#define HUD_COLS 36
#define HUD_ROWS 4

// One HUD line plus its terminator.
#define LINE_BUFFER (HUD_COLS + 1)

void executeGuiThread(SceSize args, void *argp);
void stopGuiThread(void);

#endif
