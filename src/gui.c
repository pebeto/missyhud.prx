#include <stdio.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspthreadman.h>

#include "include/blit.h"

#include "globals.h"

#define BG_COLOR 0x000000
#define FG_COLOR 0xFFFFFF

void printMemoryUsage() {
    char msg[16];
    sprintf(msg, "RAM: %u/%u MB", globals.usedMemory, globals.totalMemory);
    blit_string(0, 0, msg, FG_COLOR, BG_COLOR);
}

void printPowerInfo() {
    if (!(globals.isBatteryExist)) {
        blit_string(0, 1, "Power: No battery", FG_COLOR, BG_COLOR);
        return;
    }
    else {
        if (globals.isBatteryCharging) {
            char percentMsgCharging[26];
            sprintf(percentMsgCharging, "Power: %u%% (charging...)",
                globals.batteryLifePercent);
            blit_string(0, 1, percentMsgCharging, FG_COLOR, BG_COLOR);
        }
        else {
            char percentMsgMinutes[25];
            sprintf(percentMsgMinutes, "Power: %u%% (%u mins)",
                globals.batteryLifePercent, globals.batteryLifeTime);
            blit_string(0, 1, percentMsgMinutes, FG_COLOR, BG_COLOR);
        }
    }
}

void printCpuBusFrequencies() {
    char msg[35];
    sprintf(msg, "CPU/BUS: %lu/%lu MHz", globals.cpuClockFrequency,
        globals.busClockFrequency);
    blit_string(0, 2, msg, FG_COLOR, BG_COLOR);
}

void printFps() {
    char msg[9];
    sprintf(msg, "FPS: %u", globals.fps);
    blit_string(0, 3, msg, FG_COLOR, BG_COLOR);
}

int guiThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND);

    globals.show = 1;

    while (globals.active) {
        sceKernelDelayThreadCB(200);

        if (globals.show) {
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
