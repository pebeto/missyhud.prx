#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>
#include <pspsysmem_kernel.h>

#include "globals.h"

static u8 last_fps = 0;
static u32 fps_last_clock = 0;
static u32 fps_last_counter = 0;

int getFPS() {
    // Method extracted from PSP-HUD. Thanks to darko79.
    u8 current_fps = last_fps;
    u32 clock_low = sceKernelGetSystemTimeLow();

    if ((clock_low - fps_last_clock) >= 1000000) {
        if (fps_last_clock > 0 && fps_last_clock < clock_low) {
            u32 el_clock = clock_low - fps_last_clock;

            if (el_clock > 0) {
                current_fps = (u8)((fps_counter - fps_last_counter) * 1000000 / el_clock);
            }
        }
        fps_last_clock = clock_low;
        fps_last_counter = fps_counter;
    }
    last_fps = current_fps;
    return current_fps;
}

int workerThread(unsigned int args, void *argp) {
    sceKernelDelayThread(500000);

    while (active) {
        sceKernelDelayThreadCB(200);

        if (show) {
            totalMemory = sceKernelGetModel() == 0 ? 32 : 64;
            usedMemory = totalMemory - (u8)(sceKernelTotalFreeMemSize() >> 20);
            isBatteryExist = scePowerIsBatteryExist();
            isBatteryCharging = scePowerIsBatteryCharging() || scePowerIsPowerOnline();
            batteryLifePercent = scePowerGetBatteryRemainCapacity() * 100 / scePowerGetBatteryFullCapacity();
            batteryLifeTime = scePowerGetBatteryLifeTime();
            cpuClockFrequency = scePowerGetCpuClockFrequency();
            busClockFrequency = scePowerGetBusClockFrequency();
            fps = getFPS();
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeWorkerThread(SceSize args, void *argp) {
    int worker_thid = sceKernelCreateThread("missyhud_worker_thread",
        workerThread, 0x18, 0x10000, 0, NULL);

    if (worker_thid >= 0) {
        sceKernelStartThread(worker_thid, args, argp);
    }
}
