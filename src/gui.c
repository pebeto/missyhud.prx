#include <stdio.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspdisplay_kernel.h>
#include <pspge.h>
#include <pspthreadman.h>

#include "include/blit.h"
#include "include/hook.h"

#include "globals.h"
#include "gui.h"
#include "utils.h"

enum GuiLine { CPU_LINE, POWER_LINE, MEMORY_LINE, FPS_LINE, GUI_LINE_COUNT };

// Exactly "100% (999 mins)", the widest battery reading, plus its terminator.
#define BATTERY_BUFFER 16

#define GUI_STACK_SIZE 0x1000
#define GUI_PRIORITY 0x10

// Time allowed for a call already inside a hook to return before the threads,
// and this module's memory, go away.
#define HOOK_DRAIN_DELAY (ONE_SECOND / 10)

// Poll interval for the GUI thread while the display hook is doing the drawing.
#define HOOK_IDLE_POLL (ONE_SECOND / 10)

static SceUID guiThid = -1;

// Cell each line is drawn at, and the text itself. Rebuilt only when the worker
// publishes new readings, so the display hook does no formatting per frame.
static u8 lineColumn[GUI_LINE_COUNT];
static u8 lineRow[GUI_LINE_COUNT];
static char lineText[GUI_LINE_COUNT][LINE_BUFFER];
static u8 textVersion = 0;
static u8 textValid = 0;

// sceDisplaySetFrameBufferInternal rather than sceDisplaySetFrameBuf: the
// latter is sceDisplay_driver's kernel entry point, and a game runs in user
// mode, so its flips reach the internal function without ever passing through
// it. Both the user syscall and the kernel export funnel into this one.
int (*_sceDisplaySetFrameBufferInternal)(int pri, void *topaddr, int bufferwidth,
                                         int pixelformat, int sync);
int (*_sceGeListEnQueue)(const void *list, void *stall, int cbid, void *arg);

// Whether each hijack took. Reported on the FPS line, so a plugin that cannot
// count frames says so instead of showing a plausible zero.
static u8 displayHooked = 0;
static u8 geHooked = 0;

// Applied to every value that reaches a format string, so the width of each
// line is bounded here rather than depending on what the worker thread stored.
static unsigned int clampTo(unsigned int value, unsigned int max) {
    return value > max ? max : value;
}

static void formatCpuIndicators(char *msg) {
    snprintf(msg, LINE_BUFFER, "CPU: %u%% (%u/%u MHz)",
             clampTo(globals.cpuUsage, MAX_PERCENT),
             clampTo((unsigned int)globals.cpuClockFrequency, MAX_CLOCK_MHZ),
             clampTo((unsigned int)globals.busClockFrequency, MAX_CLOCK_MHZ));
}

static void formatPowerIndicators(char *msg) {
    char remaining[BATTERY_BUFFER];

    if (!globals.isBatteryExist) {
        snprintf(msg, LINE_BUFFER, "Power: No battery");
        return;
    }

    if (isPSPGo()) {
        // The Go reports a coarse level rather than a usable percentage.
        const char *level = globals.batteryLifePercent >= 60   ? "High"
                            : globals.batteryLifePercent >= 30 ? "Medium"
                                                              : "Low";
        snprintf(remaining, sizeof(remaining), "%s", level);
    } else {
        snprintf(remaining, sizeof(remaining), "%u%% (%u mins)",
                 clampTo(globals.batteryLifePercent, MAX_PERCENT),
                 clampTo(globals.batteryLifeTime, MAX_BATTERY_MINUTES));
    }

    snprintf(msg, LINE_BUFFER, "Power: %s%s", remaining,
             globals.isBatteryCharging ? " (charging...)" : "");
}

static void formatMemoryUsage(char *msg) {
    snprintf(msg, LINE_BUFFER, "RAM: %u/%u MB",
             clampTo(globals.usedMemory, MAX_MEMORY_MB),
             clampTo(globals.totalMemory, MAX_MEMORY_MB));
}

static void formatFps(char *msg) {
    if (!displayHooked && !geHooked) {
        snprintf(msg, LINE_BUFFER, "FPS: n/a");
        return;
    }

    // The GE count is display lists submitted, not buffer flips, so it is
    // marked as the estimate it is.
    snprintf(msg, LINE_BUFFER, "FPS: %u%s", clampTo(globals.fps, MAX_FPS),
             globals.useGeFps ? " (ge)" : "");
}

static void setPositions(void) {
    u8 position = globals.guiPosition;
    u8 column;
    u8 row;
    u8 x;
    u8 y;
    int i;

    if (position >= GUI_POSITION_COUNT) {
        position = TOP_LEFT;
    }

    column = position % 3;
    row = position / 3;

    // Placed as a block. Text is clipped rather than wrapped now, so the right
    // column can sit flush against the last cell that fits a full line.
    x = column == 0 ? 0
                    : (column == 1 ? (BLIT_COLS - HUD_COLS) / 2 : BLIT_COLS - HUD_COLS);
    y = row == 0 ? 0 : (row == 1 ? (BLIT_ROWS - HUD_ROWS) / 2 : BLIT_ROWS - HUD_ROWS);

    for (i = 0; i < GUI_LINE_COUNT; i++) {
        lineColumn[i] = x;
        lineRow[i] = (u8)(y + i);
    }
}

// Rebuilds the strings only when the worker has published something new, so the
// per-frame cost in the display hook is the blit alone.
static void refreshText(void) {
    if (textValid && textVersion == globals.dataVersion) {
        return;
    }

    textVersion = globals.dataVersion;
    formatCpuIndicators(lineText[CPU_LINE]);
    formatPowerIndicators(lineText[POWER_LINE]);
    formatMemoryUsage(lineText[MEMORY_LINE]);
    formatFps(lineText[FPS_LINE]);
    textValid = 1;
}

static void drawHud(const BlitTarget *target) {
    int i;

    setPositions();
    refreshText();

    for (i = 0; i < GUI_LINE_COUNT; i++) {
        blit_text(target, lineColumn[i], lineRow[i], lineText[i], FG_COLOR, BG_COLOR);
    }
}

// Runs in whichever thread the game flips buffers from, so it stays short and
// touches nothing that could block.
static int sceDisplaySetFrameBufferInternalHook(int pri, void *topaddr, int bufferwidth,
                                                int pixelformat, int sync) {
    globals.frameCounter++;

    // The buffer is not on screen yet, so drawing here neither tears against the
    // scanout nor gets lost when the flip happens, which is what made the HUD
    // flicker in double buffered games.
    if (globals.show && topaddr != NULL) {
        BlitTarget target;

        target.base = topaddr;
        target.stride = bufferwidth;
        target.format = pixelformat;
        drawHud(&target);
    }

    return _sceDisplaySetFrameBufferInternal(pri, topaddr, bufferwidth, pixelformat,
                                            sync);
}

static int sceGeListEnQueueHook(const void *list, void *stall, int cbid, void *arg) {
    globals.geCounter++;
    return _sceGeListEnQueue(list, stall, cbid, arg);
}

// Footprint of the fallback path's last draw. Only the GUI thread touches these,
// and only while the display hook is not being reached.
static u8 fallbackColumn[GUI_LINE_COUNT];
static u8 fallbackRow[GUI_LINE_COUNT];
static u8 fallbackLen[GUI_LINE_COUNT];
static int fallbackVisible = 0;

static void blankCells(const BlitTarget *target, int col, int row, int len) {
    char blanks[LINE_BUFFER];

    if (len <= 0) {
        return;
    }
    if (len > HUD_COLS) {
        len = HUD_COLS;
    }

    memset(blanks, ' ', (size_t)len);
    blanks[len] = '\0';
    blit_text(target, col, row, blanks, FG_COLOR, BG_COLOR);
}

// The hook path needs none of this: it draws into a buffer the game has just
// rendered, so last frame's text is never there to begin with. Drawing into the
// buffer already on screen does leave it behind, so a line that has got shorter,
// or moved, has to be blanked first.
static void drawHudTracked(const BlitTarget *target) {
    int i;

    setPositions();
    refreshText();

    // Erase every stale footprint before drawing any line, so a HUD that has
    // just moved cannot blank a line already redrawn at its new position.
    for (i = 0; i < GUI_LINE_COUNT; i++) {
        u8 len = (u8)strlen(lineText[i]);

        if (fallbackLen[i] == 0) {
            continue;
        }
        if (fallbackColumn[i] != lineColumn[i] || fallbackRow[i] != lineRow[i]) {
            blankCells(target, fallbackColumn[i], fallbackRow[i], fallbackLen[i]);
            fallbackLen[i] = 0;
        } else if (len < fallbackLen[i]) {
            blankCells(target, lineColumn[i] + len, lineRow[i], fallbackLen[i] - len);
            fallbackLen[i] = len;
        }
    }

    for (i = 0; i < GUI_LINE_COUNT; i++) {
        blit_text(target, lineColumn[i], lineRow[i], lineText[i], FG_COLOR, BG_COLOR);
        fallbackColumn[i] = lineColumn[i];
        fallbackRow[i] = lineRow[i];
        fallbackLen[i] = (u8)strlen(lineText[i]);
    }
}

static void clearHudTracked(const BlitTarget *target) {
    int i;

    for (i = 0; i < GUI_LINE_COUNT; i++) {
        blankCells(target, fallbackColumn[i], fallbackRow[i], fallbackLen[i]);
        fallbackLen[i] = 0;
    }
}

// Fallback for anything that never calls sceDisplaySetFrameBuf: draw into the
// buffer currently on screen, as this plugin did everywhere before.
static int currentDisplayTarget(BlitTarget *target) {
    // sceDisplayGetMode() used to be called only to produce the fourth argument
    // here, which is a sync mode, not a display mode. It happened to work
    // because both constants are 0.
    if (sceDisplayGetFrameBuf(&target->base, &target->stride, &target->format,
                              PSP_DISPLAY_SETBUF_IMMEDIATE) < 0) {
        return 0;
    }

    return blit_target_valid(target);
}

static int guiThread(unsigned int args, void *argp) {
    (void)args;
    (void)argp;

    sceKernelDelayThread(ONE_SECOND);

    while (globals.active) {
        BlitTarget target;

        // While the hook is drawing there is nothing to do here, so poll slowly
        // rather than waking for every vblank.
        if (globals.hookDrawing) {
            sceKernelDelayThreadCB(HOOK_IDLE_POLL);
            continue;
        }

        sceKernelDelayThreadCB(200);

        if (globals.show) {
            if (currentDisplayTarget(&target)) {
                drawHudTracked(&target);
                fallbackVisible = 1;
            }
        } else if (fallbackVisible) {
            if (currentDisplayTarget(&target)) {
                clearHudTracked(&target);
            }
            fallbackVisible = 0;
        }
        sceDisplayWaitVblankStart();
    }

    if (fallbackVisible) {
        BlitTarget target;

        if (currentDisplayTarget(&target)) {
            clearHudTracked(&target);
        }
        fallbackVisible = 0;
    }

    sceKernelExitThread(0);
    return 0;
}

void executeGuiThread(SceSize args, void *argp) {
    textValid = 0;

    displayHooked = hook_function(sceDisplaySetFrameBufferInternal,
                                  sceDisplaySetFrameBufferInternalHook,
                                  (void **)&_sceDisplaySetFrameBufferInternal) == 0;

    geHooked = hook_function(sceGeListEnQueue, sceGeListEnQueueHook,
                             (void **)&_sceGeListEnQueue) == 0;

    guiThid = sceKernelCreateThread("missyhud_gui_thread", guiThread, GUI_PRIORITY,
                                    GUI_STACK_SIZE, 0, NULL);

    if (guiThid >= 0) {
        sceKernelStartThread(guiThid, args, argp);
    }
}

void stopGuiThread(void) {
    // Restore the patched entries before anything else is torn down, so no
    // further calls enter this module.
    unhook_all();
    sceKernelDelayThread(HOOK_DRAIN_DELAY);

    waitForThread(guiThid);
    guiThid = -1;
}
