#ifndef GLOBALS_H
#define GLOBALS_H

#define ONE_SECOND 1000000

#include <psptypes.h>

// Display bounds. Each reading is clamped both where it is stored (worker.c) and
// where it is formatted (gui.c); the second clamp is what lets the compiler
// prove every HUD line fits in HUD_COLS and in its buffer.
#define MAX_PERCENT 100
#define MAX_CLOCK_MHZ 999
#define MAX_BATTERY_MINUTES 999
#define MAX_MEMORY_MB 99
#define MAX_FPS 255

// Laid out as a row-major 3x3 grid: setPositions() derives the on-screen column
// from (value % 3) and the row from (value / 3), so the order matters.
enum GuiPosition {
    TOP_LEFT,
    MID_TOP,
    TOP_RIGHT,
    MID_LEFT,
    CENTER,
    MID_RIGHT,
    BOTTOM_LEFT,
    MID_BOTTOM,
    BOTTOM_RIGHT,
    GUI_POSITION_COUNT
};

struct Globals {
    // Written by the control thread, read by the worker and GUI threads.
    volatile u8 show;
    volatile u8 active;
    volatile u8 guiPosition;
    // Incremented from hooked functions, which run in whatever thread called
    // the function being hooked. frameCounter counts real buffer flips, so it is
    // an exact frame count rather than an estimate.
    volatile u32 frameCounter;
    volatile u32 geCounter;
    // Bumped by the worker thread whenever it publishes new readings, so the
    // drawing side knows when to rebuild its strings.
    volatile u8 dataVersion;
    // Written by the worker thread, read by the GUI thread.
    u8 fps;
    u8 cpuUsage;
    u8 usedMemory;
    u8 totalMemory;
    u8 useGeFps;
    u8 isBatteryExist;
    u16 batteryLifeTime;
    u8 isBatteryCharging;
    u8 batteryLifePercent;
    u32 busClockFrequency;
    u32 cpuClockFrequency;
};
extern struct Globals globals;

#endif
