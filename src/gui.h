#ifndef GUI_H
#define GUI_H

#include <pspkerneltypes.h>

#define BG_COLOR 0x000000
#define FG_COLOR 0xFFFFFF

#define W_MAX 45
#define H_MAX 33

typedef struct {
    u8 x;
    u8 y;
} GuiComponent;

void executeGuiThread(SceSize args, void *argp);

#endif
