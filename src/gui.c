#include <stdio.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspthreadman.h>

#include "include/blit.h"
#include "include/hook.h"

#include "globals.h"
#include "gui.h"
#include "utils.h"

enum GuiLine { CPU_LINE, POWER_LINE, MEMORY_LINE, FPS_LINE, GUI_LINE_COUNT };

// Exactly "100% (999 mins)", the widest battery reading, plus its terminator.
#define BATTERY_BUFFER 16

// Each syscall taken over, as sctrlHENFindFunction() needs it named. NIDs are
// the ones pspsdk's user mode stubs carry for these functions.
#define DISPLAY_MODULE "sceDisplay_Service"
#define DISPLAY_LIBRARY "sceDisplay"
#define SET_FRAME_BUF_NID 0x289D82FE

#define GE_MODULE "sceGE_Manager"
#define GE_LIBRARY "sceGe_user"
#define LIST_ENQUEUE_NID 0xAB49E76A

#define GUI_STACK_SIZE 0x1000
#define GUI_PRIORITY 0x10

// Time allowed for a call already inside a hook to return before the threads,
// and this module's memory, go away.
#define HOOK_DRAIN_DELAY (ONE_SECOND / 10)

// Poll interval for the GUI thread while the display hook is doing the drawing.
#define HOOK_IDLE_POLL (ONE_SECOND / 10)

// Polls without a new frame before the display hook counts as gone. The thread
// wakes about once per vblank, so this tolerates a game flipping as slowly as
// 8fps while still noticing a real stop inside about an eighth of a second.
#define HOOK_IDLE_POLLS 8

static SceUID guiThid = -1;

// Cell each line is drawn at, and the text itself. Rebuilt only when the worker
// publishes new readings, so the display hook does no formatting per frame.
static u8 lineColumn[GUI_LINE_COUNT];
static u8 lineRow[GUI_LINE_COUNT];
static char lineText[GUI_LINE_COUNT][LINE_BUFFER];
static u8 textVersion = 0;
static u8 textValid = 0;

// sceDisplaySetFrameBuf as user mode reaches it, which is the syscall a game or
// the XMB makes to put a buffer on screen.
int (*_sceDisplaySetFrameBuf)(void *topaddr, int bufferwidth, int pixelformat, int sync);
int (*_sceGeListEnQueue)(const void *list, void *stall, int cbid, void *arg);

// How each hijack went. Reported on the FPS line, so a plugin that cannot count
// frames says why instead of showing a plausible zero.
static u8 displayHookStatus = HOOK_NOT_FOUND;
static u8 geHookStatus = HOOK_NOT_FOUND;

// The glyphs are written by the CPU straight into the framebuffer, so they have
// to reach memory before the buffer is scanned out instead of sitting in the
// data cache waiting to be evicted. Writing them cached is why the HUD faded in
// and out under load.
//
// Which bit reaches the uncached mirror depends on the segment, and an address
// outside the two that are known is returned untouched rather than turned into
// something that is not mapped. A null address stays null: it has to keep
// failing blit_target_valid() instead of becoming a plausible pointer.
static void *uncached(void *address) {
    u32 addr = (u32)address;

    if (addr == 0) {
        return NULL;
    }

    switch (addr >> 28) {
    case 0x0:
    case 0x1:
        // User and physical window, cached. Its mirror is at +0x40000000.
        return (void *)(addr | 0x40000000);
    case 0x8:
    case 0x9:
        // Kernel window, cached. Its mirror is at +0x20000000.
        return (void *)(addr | 0x20000000);
    default:
        return address;
    }
}

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
    // Indexed by HookStatus. The display hook is where the frame count comes
    // from, so when it is not in place the line says which step failed.
    static const char *const hookFailure[] = {"", "find", "slots"};

    if (displayHookStatus != HOOK_OK && geHookStatus != HOOK_OK) {
        snprintf(msg, LINE_BUFFER, "FPS: n/a (%s)", hookFailure[displayHookStatus]);
        return;
    }
    if (displayHookStatus != HOOK_OK) {
        snprintf(msg, LINE_BUFFER, "FPS: %u (ge, %s)", clampTo(globals.fps, MAX_FPS),
                 hookFailure[displayHookStatus]);
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
static int sceDisplaySetFrameBufHook(void *topaddr, int bufferwidth, int pixelformat,
                                     int sync) {
    globals.frameCounter++;

    // The buffer is not on screen yet, so drawing here neither tears against the
    // scanout nor gets lost when the flip happens, which is what made the HUD
    // flicker in double buffered games.
    if (globals.show && topaddr != NULL) {
        BlitTarget target;

        target.base = uncached(topaddr);
        target.stride = bufferwidth;
        target.format = pixelformat;
        drawHud(&target);
    }

    return _sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
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

    if (!blit_target_valid(target)) {
        return 0;
    }

    target->base = uncached(target->base);

    return 1;
}

static int guiThread(unsigned int args, void *argp) {
    // Sampled here rather than taken from the worker thread. The worker only
    // looks once a second, which left this thread drawing into the visible
    // buffer for up to a second after a game started flipping again, and that
    // is what made the HUD drop in and out across a loading screen.
    u32 seenFrames = globals.frameCounter;
    u8 idlePolls = 0;

    (void)args;
    (void)argp;

    sceKernelDelayThread(ONE_SECOND);

    while (globals.active) {
        BlitTarget target;
        u32 frames = globals.frameCounter;

        if (frames != seenFrames) {
            seenFrames = frames;
            idlePolls = 0;
        } else if (idlePolls < HOOK_IDLE_POLLS) {
            idlePolls++;
        }

        // While the hook is drawing there is nothing to do here, so poll slowly
        // rather than waking for every vblank.
        if (idlePolls < HOOK_IDLE_POLLS) {
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

    displayHookStatus =
        (u8)hook_syscall(DISPLAY_MODULE, DISPLAY_LIBRARY, SET_FRAME_BUF_NID,
                         sceDisplaySetFrameBufHook, (void **)&_sceDisplaySetFrameBuf);

    geHookStatus = (u8)hook_syscall(GE_MODULE, GE_LIBRARY, LIST_ENQUEUE_NID,
                                    sceGeListEnQueueHook, (void **)&_sceGeListEnQueue);

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
