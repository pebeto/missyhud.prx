#include <psppower.h>
#include <pspsysmem_kernel.h>
#include <pspthreadman.h>

#include "globals.h"
#include "utils.h"
#include "worker.h"

#define WORKER_STACK_SIZE 0x1000
#define WORKER_PRIORITY 0x10
#define WORKER_POLL_INTERVAL (ONE_SECOND / 10)

static SceUID workerThid = -1;

static u8 lastCpuUsage = 0;
static u32 cpuLastIdleTime = 0;

static u32 clampU32(int value, u32 max) {
    if (value < 0) {
        return 0;
    }
    return (u32)value > max ? max : (u32)value;
}

static u8 getCpuUsage(u32 currentTime, u32 lastTime) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    SceKernelSystemStatus status;
    status.size = sizeof(SceKernelSystemStatus);
    sceKernelReferSystemStatus(&status);

    if (lastTime > 0 && cpuLastIdleTime > 0 && lastTime < currentTime &&
        cpuLastIdleTime < status.idleClocks.low) {
        u32 elapsedTime = currentTime - lastTime;
        u32 elapsedIdleTime = status.idleClocks.low - cpuLastIdleTime;
        // Scaling down before dividing keeps the numerator from overflowing and
        // keeps the divisor non-zero, so the divide-by-zero trap MIPS emits is
        // unreachable.
        u32 onePercent = elapsedTime / 100;

        if (onePercent > 0) {
            if (elapsedIdleTime > elapsedTime) {
                elapsedIdleTime = elapsedTime;
            }
            lastCpuUsage = (u8)(MAX_PERCENT - elapsedIdleTime / onePercent);
        }
    }
    cpuLastIdleTime = status.idleClocks.low;
    return lastCpuUsage;
}

static u8 lastFps = 0;
static u32 frameLastCounter = 0;
static u32 geLastCounter = 0;

// Number of consecutive FPS calculation cycles where the primary counter
// did not increment before falling back to the GE-based method.
#define FALLBACK_THRESHOLD 3
static u8 zeroFpsCount = 0;

// Counter delta over elapsedTime, as a per-second rate. Going through
// milliseconds keeps the multiply from overflowing at any plausible rate.
static u8 perSecondRate(u32 delta, u32 elapsedTime) {
    u32 elapsedMillis = elapsedTime / 1000;
    u32 rate;

    if (elapsedMillis == 0) {
        return 0;
    }

    rate = (delta * 1000) / elapsedMillis;
    return rate > MAX_FPS ? MAX_FPS : (u8)rate;
}

static u8 getFPS(u32 currentTime, u32 lastTime) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    u32 frameDelta = globals.frameCounter - frameLastCounter;

    if (lastTime > 0 && lastTime < currentTime) {
        u32 elapsedTime = currentTime - lastTime;

        if (elapsedTime > 0) {
            if (frameDelta == 0) {
                if (zeroFpsCount < FALLBACK_THRESHOLD) {
                    zeroFpsCount++;
                }
            } else {
                zeroFpsCount = 0;
                globals.useGeFps = 0;
            }

            // If the display hook hasn't fired for several cycles, fall back
            // to counting GE display list submissions instead.
            if (zeroFpsCount >= FALLBACK_THRESHOLD) {
                globals.useGeFps = 1;
            }

            if (globals.useGeFps) {
                lastFps = perSecondRate(globals.geCounter - geLastCounter, elapsedTime);
            } else {
                // One hooked sceDisplaySetFrameBuf call is one buffer flip, so
                // this is an exact frame count rather than an estimate.
                lastFps = perSecondRate(frameDelta, elapsedTime);
            }
        }
    }

    // The hook draws the HUD itself whenever it is being reached; the GUI thread
    // only falls back to drawing into the displayed buffer when it is not.
    globals.hookDrawing = frameDelta > 0;

    frameLastCounter = globals.frameCounter;
    geLastCounter = globals.geCounter;
    return lastFps;
}

static void readMemoryUsage(void) {
    u32 freeMemory = (u32)(sceKernelTotalFreeMemSize() >> 20);

    globals.usedMemory =
        freeMemory >= globals.totalMemory ? 0 : (u8)(globals.totalMemory - freeMemory);
}

static void readPowerStatus(void) {
    int percent;
    int lifeTime;

    globals.isBatteryExist = scePowerIsBatteryExist() != 0;
    globals.isBatteryCharging =
        scePowerIsBatteryCharging() > 0 || scePowerIsPowerOnline() > 0;

    if (!globals.isBatteryExist) {
        globals.batteryLifePercent = 0;
        globals.batteryLifeTime = 0;
        return;
    }

    if (isPSPGo()) {
        percent = scePowerGetBatteryLifePercent();
    } else {
        int full = scePowerGetBatteryFullCapacity();
        int remaining = scePowerGetBatteryRemainCapacity();

        // A zero or failed capacity read would otherwise reach the MIPS
        // divide-by-zero trap.
        percent = (full > 0 && remaining >= 0) ? remaining * 100 / full : -1;
    }

    globals.batteryLifePercent = (u8)clampU32(percent, MAX_PERCENT);

    lifeTime = scePowerGetBatteryLifeTime();
    globals.batteryLifeTime = (u16)clampU32(lifeTime, MAX_BATTERY_MINUTES);
}

// Clears the state carried in statics, so a module that is stopped and started
// again measures from scratch instead of against readings from its last run.
static void resetMetrics(void) {
    lastCpuUsage = 0;
    cpuLastIdleTime = 0;
    lastFps = 0;
    frameLastCounter = 0;
    geLastCounter = 0;
    zeroFpsCount = 0;

    globals.fps = 0;
    globals.frameCounter = 0;
    globals.geCounter = 0;
    globals.useGeFps = 0;
    globals.hookDrawing = 0;
    globals.dataVersion = 0;
}

static int workerThread(unsigned int args, void *argp) {
    u32 lastTime = 0;

    (void)args;
    (void)argp;

    sceKernelDelayThread(ONE_SECOND / 2);

    resetMetrics();
    globals.totalMemory = sceKernelGetModel() == 0 ? FAT_RAM : SLIM_RAM;

    while (globals.active) {
        u32 currentTime;

        sceKernelDelayThreadCB(WORKER_POLL_INTERVAL);

        if (!globals.show) {
            // Re-prime the counters so the HUD refreshes as soon as it is shown
            // again, rather than reporting a rate measured across the gap.
            lastTime = 0;
            continue;
        }

        currentTime = sceKernelGetSystemTimeLow();

        // Unsigned arithmetic, so this still holds across the ~72 minute wrap of
        // sceKernelGetSystemTimeLow().
        if ((u32)(currentTime - lastTime) < ONE_SECOND) {
            continue;
        }

        readMemoryUsage();
        readPowerStatus();
        globals.cpuClockFrequency =
            clampU32(scePowerGetCpuClockFrequency(), MAX_CLOCK_MHZ);
        globals.busClockFrequency =
            clampU32(scePowerGetBusClockFrequency(), MAX_CLOCK_MHZ);
        globals.fps = getFPS(currentTime, lastTime);
        globals.cpuUsage = getCpuUsage(currentTime, lastTime);
        lastTime = currentTime;

        // Published last, so the drawing side only rebuilds its strings once
        // every reading above it is in place.
        globals.dataVersion++;
    }

    sceKernelExitThread(0);
    return 0;
}

void executeWorkerThread(SceSize args, void *argp) {
    workerThid = sceKernelCreateThread("missyhud_worker_thread", workerThread,
                                       WORKER_PRIORITY, WORKER_STACK_SIZE, 0, NULL);

    if (workerThid >= 0) {
        sceKernelStartThread(workerThid, args, argp);
    }
}

void stopWorkerThread(void) {
    waitForThread(workerThid);
    workerThid = -1;
}
