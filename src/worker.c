#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>
#include <pspsysmem_kernel.h>

#include "globals.h"

#define PSP_1G_RAM 32
#define NOT_PSP_1G_RAM 64

static u8 last_fps = 0;
static u32 fps_last_time = 0;
static u32 fps_last_counter = 0;

u8 getFPS() {
    // Method based on the one written by darko79 for PSP-HUD (Thank  you).
    u32 current_time = sceKernelGetSystemTimeLow();

    if ((current_time - fps_last_time) >= ONE_SECOND) {
        if (fps_last_time > 0 && fps_last_time < current_time) {
            u32 elapsed_time = current_time - fps_last_time;

            if (elapsed_time > 0) {
                last_fps = (u8)((globals.fps_counter - fps_last_counter) * ONE_SECOND / elapsed_time);
            }
        }
        fps_last_time = current_time;
        fps_last_counter = globals.fps_counter;
    }
    return last_fps;
}

int workerThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND/2);

    globals.fps = 0;
    globals.fps_counter = 0;
    globals.totalMemory = sceKernelGetModel() == 0 ? PSP_1G_RAM : NOT_PSP_1G_RAM;

    while (globals.active) {
        sceKernelDelayThreadCB(200);

        if (globals.show) {
           globals.usedMemory = globals.totalMemory - (u8)(sceKernelTotalFreeMemSize() >> 20);
           globals.isBatteryExist = scePowerIsBatteryExist();
           globals.isBatteryCharging = scePowerIsBatteryCharging() || scePowerIsPowerOnline();
           globals.batteryLifePercent = scePowerGetBatteryRemainCapacity() * 100 / scePowerGetBatteryFullCapacity();
           globals.batteryLifeTime = scePowerGetBatteryLifeTime();
           globals.cpuClockFrequency = scePowerGetCpuClockFrequency();
           globals.busClockFrequency = scePowerGetBusClockFrequency();
           globals.fps = getFPS();
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
