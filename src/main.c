#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspsysmem_kernel.h>
#include <psppower.h>
#include <stdio.h>

#include "include/blit.h"

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, 0, 1);

static char active = 1;

void printMemoryUsage() {
    char msg[13];
    u8 maxMem = sceKernelGetModel() == 0 ? 32 : 64;
    sprintf(msg, "RAM: %u/%u MB",
        maxMem - (u8)(sceKernelTotalFreeMemSize() >> 20), maxMem);
    blit_string(0, 0, msg, 0xFFFFFF, 0x000000);
}

void printPowerInfo() {
    if (scePowerIsBatteryCharging()) {
        char percentMsgCharging[24];
        sprintf(percentMsgCharging, "Power: %u%% (charging...)",
            scePowerGetBatteryLifePercent());
        blit_string(0, 1, percentMsgCharging, 0xFFFFFF, 0x000000);
    }
    else {
        char percentMsgMinutes[19];
        sprintf(percentMsgMinutes, "Power: %u%% (%u mins)",
            scePowerGetBatteryLifePercent(), scePowerGetBatteryLifeTime());
        blit_string(0, 1, percentMsgMinutes, 0xFFFFFF, 0x000000);
    }
}

void printCpuBusFrequencies() {
    char msg[17];
    sprintf(msg, "CPU/BUS: %u/%u MHz",
        scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());
    blit_string(0, 2, msg, 0xFFFFFF, 0x000000);
}

int main_thread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000);

    while(active) {
        sceKernelDelayThreadCB(1000/5);

        printMemoryUsage();
        printPowerInfo();
        printCpuBusFrequencies();

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
