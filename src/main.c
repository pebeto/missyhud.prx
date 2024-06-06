#include <pspmoduleinfo.h>
#include <pspdisplay_kernel.h>

#include "include/hook.h"

#include "gui.h"
#include "worker.h"
#include "control.h"
#include "globals.h"

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, 0, 4);

u8 fps = 0;
u8 show = 1;
u8 active = 1;
u8 usedMemory;
u8 totalMemory;
u8 isBatteryExist;
u16 batteryLifeTime;
u32 fps_counter = 0;
u8 isBatteryCharging;
u8 batteryLifePercent;
u32 busClockFrequency;
u32 cpuClockFrequency;


int (*_sceDisplaySetFrameBufferInternal)(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync);

int sceDisplaySetFrameBufferInternalHook(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync) {
    fps_counter++;
    return _sceDisplaySetFrameBufferInternal(pri, topaddr, bufferwidth,
        pixelformat, sync);
}

int module_start(SceSize args, void *argp) {
    hook_function((unsigned int*) sceDisplaySetFrameBufferInternal,
        sceDisplaySetFrameBufferInternalHook,
        &_sceDisplaySetFrameBufferInternal);

    executeControlThread(args, argp);
    executeWorkerThread(args, argp);
    executeGuiThread(args, argp);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    active = 0;
    return 0;
}
