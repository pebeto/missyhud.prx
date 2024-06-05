#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdisplay_kernel.h>
#include <pspsysmem_kernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <stdio.h>

#include "include/blit.h"
#include "include/hook.h"

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, 0, 3);

int (*_sceDisplaySetFrameBufferInternal)(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync);

static u8 show = 1;
static u8 active = 1;


static u8 fps = 0;
static u8 last_fps = 0;
static u32 fps_counter = 0;
static u32 fps_last_clock = 0;
static u32 fps_last_counter = 0;

int getFPS() {
    // Method extracted from PSP-HUD. Thanks to darko79.
    u8 current_fps = last_fps;
    u32 clock_low = sceKernelGetSystemTimeLow();

    if((clock_low - fps_last_clock) >= 1000000) {
        if(fps_last_clock > 0 && fps_last_clock < clock_low) {
            u32 el_clock = clock_low - fps_last_clock;

            if(el_clock > 0) {
                current_fps = (u8)((fps_counter - fps_last_counter) * 1000000 / el_clock);
            }
        }
        fps_last_clock = clock_low;
        fps_last_counter = fps_counter;
    }
    last_fps = current_fps;
    return current_fps;
}

void printFps() {
    char msg[9];
    fps = getFPS();
    sprintf(msg, "FPS: %u", fps);
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
    else {
        u16 remainingCapacity = scePowerGetBatteryRemainCapacity();
        u16 fullCapacity = scePowerGetBatteryFullCapacity();
        u8 percentage = remainingCapacity * 100 / fullCapacity;

        if (scePowerIsBatteryCharging()) {
            char percentMsgCharging[28];
            sprintf(percentMsgCharging, "Power: %u%% (charging...)",
                percentage);
            blit_string(0, 1, percentMsgCharging, 0xFFFFFF, 0x000000);
        }
        else {
            char percentMsgMinutes[27];
            sprintf(percentMsgMinutes, "Power: %u%% (%u mins)", percentage,
                (u16) scePowerGetBatteryLifeTime());
            blit_string(0, 1, percentMsgMinutes, 0xFFFFFF, 0x000000);
        }
    }
}

void printCpuBusFrequencies() {
    char msg[17];
    sprintf(msg, "CPU/BUS: %u/%u MHz",
        scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
    blit_string(0, 2, msg, 0xFFFFFF, 0x000000);
}

int sceDisplaySetFrameBufferInternalHook(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync) {
    fps_counter++;
    return _sceDisplaySetFrameBufferInternal(pri, topaddr, bufferwidth,
        pixelformat, sync);
}

int ctrl_thread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000000);

    SceCtrlData pad;
    sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(active) {
        sceKernelDelayThread(1000000);
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons != 0) {
            if (pad.Buttons & PSP_CTRL_LTRIGGER
                && pad.Buttons & PSP_CTRL_RTRIGGER
                && pad.Buttons & PSP_CTRL_START) {
                show = !show;
            }
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

int main_thread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000000);

    hook_function((unsigned int*) sceDisplaySetFrameBufferInternal,
        sceDisplaySetFrameBufferInternalHook,
        &_sceDisplaySetFrameBufferInternal);

    while(active) {
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

int module_start(SceSize args, void *argp) {
    int ctrl_thid = sceKernelCreateThread("missyhud_ctrl_thread", ctrl_thread,
        0x18, 0x10000, 0, NULL);
    int thid = sceKernelCreateThread("missyhud_main_thread", main_thread, 0x18,
        0x10000, 0, NULL);

    if (ctrl_thid >= 0) {
        sceKernelStartThread(ctrl_thid, args, argp);
    }
    if(thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
    return 0;
}

int module_stop(SceSize args, void *argp) {
    active = 0;
    return 0;
}
