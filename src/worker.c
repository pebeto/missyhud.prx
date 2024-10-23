#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>
#include <pspsysmem_kernel.h>

#include "utils.h"
#include "worker.h"
#include "globals.h"

static u8 lastCpuUsage = 0;
static u32 cpuLastIdleTime = 0;

u8 getCpuUsage(u32 currentTime, u32 lastTime) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    SceKernelSystemStatus status;
    status.size = sizeof(SceKernelSystemStatus);
    sceKernelReferSystemStatus(&status);

    if (lastTime > 0 && cpuLastIdleTime > 0 && lastTime < currentTime
        && cpuLastIdleTime < status.idleClocks.low) {
        u32 elapsed_time = currentTime - lastTime;
        u32 elapsed_idle_time = status.idleClocks.low - cpuLastIdleTime;

        if (elapsed_time > 0) {
            lastCpuUsage = (u8)(100 - ((float)elapsed_idle_time / (float)elapsed_time) * 100);
        }
    }
    cpuLastIdleTime = status.idleClocks.low;
    return lastCpuUsage > 100 ? 100 : lastCpuUsage;
}

static u8 lastFps = 0;
static u32 fpsLastCounter = 0;

u8 getFPS(u32 currentTime, u32 lastTime) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    if (lastTime > 0 && lastTime < currentTime) {
        u32 elapsed_time = currentTime - lastTime;

        if (elapsed_time > 0) {
            lastFps = (u8)((globals.fpsCounter - fpsLastCounter) * ONE_SECOND / elapsed_time);
        }
    }
    fpsLastCounter = globals.fpsCounter;
    return lastFps;
}

int workerThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND/2);

    static u32 lastTime = 0;

    globals.fps = 0;
    globals.fpsCounter = 0;
    globals.totalMemory = sceKernelGetModel() == 0 ? FAT_RAM : SLIM_RAM;

    while (globals.active) {
        sceKernelDelayThreadCB(200);

        if (globals.show) {
            globals.usedMemory = globals.totalMemory - (u8)(sceKernelTotalFreeMemSize() >> 20);
            globals.isBatteryExist = scePowerIsBatteryExist();
            globals.isBatteryCharging = scePowerIsBatteryCharging() || scePowerIsPowerOnline();

            if (isPSPGo()){
                globals.batteryLifePercent = scePowerGetBatteryLifePercent();
            }
            else {
                globals.batteryLifePercent = scePowerGetBatteryRemainCapacity() * 100 / scePowerGetBatteryFullCapacity();
            }

            globals.batteryLifeTime = scePowerGetBatteryLifeTime();
            globals.cpuClockFrequency = scePowerGetCpuClockFrequency();
            globals.busClockFrequency = scePowerGetBusClockFrequency();

            u32 currentTime = sceKernelGetSystemTimeLow();

            if ((currentTime - lastTime) >= ONE_SECOND) {
                globals.fps = getFPS(currentTime, lastTime);
                globals.cpuUsage = getCpuUsage(currentTime, lastTime);
                lastTime = currentTime;
            }
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeWorkerThread(SceSize args, void *argp) {
    int thid = sceKernelCreateThread("missyhud_worker_thread",
        workerThread, 0x10, 0x200, 0, NULL);

    if (thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
}
