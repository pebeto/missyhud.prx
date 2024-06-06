#include <stdio.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>

#include "include/blit.h"

#include "globals.h"

void printMemoryUsage() {
    char msg[16];
    sprintf(msg, "RAM: %u/%u MB", usedMemory, totalMemory);
    blit_string(0, 0, msg, 0xFFFFFF, 0x000000);
}

void printPowerInfo() {
    if (!isBatteryExist) {
        blit_string(0, 1, "Power: No battery", 0xFFFFFF, 0x000000);
        return;
    }
    else {
        if (isBatteryCharging) {
            char percentMsgCharging[26];
            sprintf(percentMsgCharging, "Power: %u%% (charging...)",
                batteryLifePercent);
            blit_string(0, 1, percentMsgCharging, 0xFFFFFF, 0x000000);
        }
        else {
            char percentMsgMinutes[25];
            sprintf(percentMsgMinutes, "Power: %u%% (%u mins)",
                batteryLifePercent, batteryLifeTime);
            blit_string(0, 1, percentMsgMinutes, 0xFFFFFF, 0x000000);
        }
    }
}

void printCpuBusFrequencies() {
    char msg[35];
    sprintf(msg, "CPU/BUS: %lu/%lu MHz", cpuClockFrequency, busClockFrequency);
    blit_string(0, 2, msg, 0xFFFFFF, 0x000000);
}

void printFps() {
    char msg[9];
    sprintf(msg, "FPS: %u", fps);
    blit_string(0, 3, msg, 0xFFFFFF, 0x000000);
}

int guiThread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000000);

    while (active) {
        sceKernelDelayThreadCB(200);

        if (show) {
            printMemoryUsage();
            printPowerInfo();
            printCpuBusFrequencies();
            printFps();
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeGuiThread(SceSize args, void *argp) {
    int thid = sceKernelCreateThread("missyhud_gui_thread", guiThread, 0x18,
        0x10000, 0, NULL);

    if(thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
}
