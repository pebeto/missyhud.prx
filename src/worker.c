#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>
#include <pspsysmem_kernel.h>

#include "globals.h"

#define PSP_1G_RAM 32
#define NOT_PSP_1G_RAM 64

static u8 lastCpuUsage = 0;
static u32 cpuLastTime = 0;
static u32 cpuLastIdleTime = 0;

u8 getCpuUsage(u32 current_time) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    SceKernelSystemStatus status;
    status.size = sizeof(SceKernelSystemStatus);
    sceKernelReferSystemStatus(&status);

    if((current_time - cpuLastTime) >= ONE_SECOND) {
        if(cpuLastTime > 0 && cpuLastIdleTime > 0 && cpuLastTime < current_time && cpuLastIdleTime < status.idleClocks.low) {
            u32 elapsed_time = current_time - cpuLastTime;
            u32 elapsed_idle_time = status.idleClocks.low - cpuLastIdleTime;

            if(elapsed_time > 0) {
                lastCpuUsage = (u8)(100 - ((float)elapsed_idle_time / (float)elapsed_time) * 100);
            }
        }
        cpuLastTime = current_time;
        cpuLastIdleTime = status.idleClocks.low;
    }
    return lastCpuUsage > 100 ? 100 : lastCpuUsage;
}

static u8 lastFps = 0;
static u32 fpsLastTime = 0;
static u32 fpsLastCounter = 0;

u8 getFPS(u32 current_time) {
    // Method based on the one written by darko79 for PSP-HUD (Thank you).
    if ((current_time - fpsLastTime) >= ONE_SECOND) {
        if (fpsLastTime > 0 && fpsLastTime < current_time) {
            u32 elapsed_time = current_time - fpsLastTime;

            if (elapsed_time > 0) {
                lastFps = (u8)((globals.fpsCounter - fpsLastCounter) * ONE_SECOND / elapsed_time);
            }
        }
        fpsLastTime = current_time;
        fpsLastCounter = globals.fpsCounter;
    }
    return lastFps;
}

int workerThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND/2);

    globals.fps = 0;
    globals.fpsCounter = 0;
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

            u32 currentTime = sceKernelGetSystemTimeLow();
            globals.fps = getFPS(currentTime);
            globals.cpuUsage = getCpuUsage(currentTime);
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeWorkerThread(SceSize args, void *argp) {
    int thid = sceKernelCreateThread("missyhud_worker_thread",
        workerThread, 0x18, 0x10000, 0, NULL);

    if (thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
}
