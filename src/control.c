#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspthreadman.h>

#include "globals.h"

int cyclePositionsThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND);

    SceCtrlData pad;
    sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (globals.active) {
        sceKernelDelayThread(ONE_SECOND/10);
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons != 0) {
            if (pad.Buttons & PSP_CTRL_START
                && pad.Lx > 200) { // Analog stick to the right
                globals.guiPosition = globals.guiPosition > BOTTOM_RIGHT ?
                    1 : globals.guiPosition + 1;
            }
            if (pad.Buttons & PSP_CTRL_START
                && pad.Lx < 50) { // Analog stick to the left
                globals.guiPosition = globals.guiPosition < 1 ?
                    BOTTOM_RIGHT : globals.guiPosition - 1;
            }
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

int hideGuiThread(unsigned int args, void *argp) {
    sceKernelDelayThread(ONE_SECOND);

    SceCtrlData pad;
    sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (globals.active) {
        sceKernelDelayThread(ONE_SECOND);
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons != 0) {
            if (pad.Buttons & PSP_CTRL_LTRIGGER
                && pad.Buttons & PSP_CTRL_RTRIGGER
                && pad.Buttons & PSP_CTRL_START) {
                globals.show = !(globals.show);
            }
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeControlThreads(SceSize args, void *argp) {
    int hideGui_thid = sceKernelCreateThread("missyhud_hidegui_thread",
        hideGuiThread, 0x10, 0x200, 0, NULL);

    if (hideGui_thid >= 0) {
        sceKernelStartThread(hideGui_thid, args, argp);
    }

    int cyclePositions_thid = sceKernelCreateThread(
        "missyhud_cyclepositions_thread", cyclePositionsThread, 0x10, 0x200, 0,
        NULL);

    if (cyclePositions_thid >= 0) {
        sceKernelStartThread(cyclePositions_thid, args, argp);
    }
}
