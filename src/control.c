#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspthreadman.h>

#include "globals.h"

int controlThread(unsigned int args, void *argp) {
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

void executeControlThread(SceSize args, void *argp) {
    int thid = sceKernelCreateThread("missyhud_control_thread",
        controlThread, 0x18, 0x10000, 0, NULL);

    if (thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
}
