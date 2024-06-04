#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspsysmem_kernel.h>
#include <psppower.h>
#include <stdio.h>

#include "include/blit.h"
#include "include/hook.h"

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, 0, 2);

int (*g_setframebuf)(int unk, void* topaddr, int bufferwidth, int pixelformat,
    int sync);

static char active = 1;

SceUInt32 fps_last_clock = 0;
int fps_counter = 0;
int fps_last_counter = 0;
int last_fps = -1;

int getFPS() {
    // Method extracted from PSP-HUD. Thanks to darko79.
    int current_fps = last_fps;
    SceKernelSysClock clock;
    sceKernelGetSystemTime(&clock);
    if((clock.low - fps_last_clock) >= 1000000) {
        if(fps_last_clock > 0 && fps_last_clock < clock.low) {
            int el_clock = clock.low - fps_last_clock;
            if(el_clock > 0) current_fps = (int)((fps_counter - fps_last_counter) * 1000000 / el_clock);
        }
        fps_last_clock = clock.low;
        fps_last_counter = fps_counter;
    }
    last_fps = current_fps;
    return current_fps;
}

int fps = 0;

void printFps() {
    char msg[16];
    fps = getFPS();
    sprintf(msg, "FPS: %u", (fps > 0 ? fps : (u8) sceDisplayGetFramePerSec()));
    blit_string(0, 3, msg, 0xFFFFFF, 0x000000);
}

void printMemoryUsage() {
    char msg[13];
    u8 maxMem = sceKernelGetModel() == 0 ? 32 : 64;
    sprintf(msg, "RAM: %u/%u MB",
        maxMem - (u8)(sceKernelTotalFreeMemSize() >> 20), maxMem);
    blit_string(0, 0, msg, 0xFFFFFF, 0x000000);
}

void printPowerInfo() {
    if (!scePowerIsBatteryExist()) {
        blit_string(0, 1, "Power: No battery", 0xFFFFFF, 0x000000);
        return;
    }

    u16 remainingCapacity = scePowerGetBatteryRemainCapacity();
    u16 fullCapacity = scePowerGetBatteryFullCapacity();
    u16 percentage = remainingCapacity * 100 / fullCapacity;

    if (scePowerIsBatteryCharging()) {
        char percentMsgCharging[28];
        sprintf(percentMsgCharging, "Power: %u%% (charging...)", percentage);
        blit_string(0, 1, percentMsgCharging, 0xFFFFFF, 0x000000);
    }
    else {
        char percentMsgMinutes[27];
        sprintf(percentMsgMinutes, "Power: %u%% (%u mins)", percentage,
            (u16) scePowerGetBatteryLifeTime());
        blit_string(0, 1, percentMsgMinutes, 0xFFFFFF, 0x000000);
    }
}

void printCpuBusFrequencies() {
    char msg[17];
    sprintf(msg, "CPU/BUS: %u/%u MHz",
        scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
    blit_string(0, 2, msg, 0xFFFFFF, 0x000000);
}

int setframebuf_hook(int unk, void* topaddr, int bufferwidth, int pixelformat,
    int sync) {
    fps_counter++;
    return g_setframebuf(unk, topaddr, bufferwidth, pixelformat, sync);
}

int main_thread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000);

    hook_function((unsigned int*) sceDisplaySetFrameBuf, setframebuf_hook,
        &g_setframebuf);

    while(active) {
        sceKernelDelayThreadCB(1000/5);

        printMemoryUsage();
        printPowerInfo();
        printCpuBusFrequencies();
        printFps();

        sceDisplayWaitVblankStart();
    }

    sceKernelExitDeleteThread(0);
    return 0;
}

int module_start(SceSize args, void *argp) {
    int thid = sceKernelCreateThread("missyhud_thread", main_thread, 0x18,
        0x10000, 0, NULL);

    if(thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
    return 0;
}

int module_stop(SceSize args, void *argp) {
    active = 0;
    return 0;
}
