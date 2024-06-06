#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspthreadman.h>

#include "globals.h"

int controlThread(unsigned int args, void *argp) {
    sceKernelDelayThread(1000000);

    SceCtrlData pad;
    sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (globals.active) {
        sceKernelDelayThread(1000000);
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

void executeControlThread(SceSize args, void *argp) {
    int control_thid = sceKernelCreateThread("missyhud_control_thread",
        controlThread, 0x18, 0x10000, 0, NULL);

    if (control_thid >= 0) {
        sceKernelStartThread(control_thid, args, argp);
    }
}
