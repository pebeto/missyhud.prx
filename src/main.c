#include <pspmoduleinfo.h>
#include <pspdisplay_kernel.h>

#include "include/hook.h"

#include "gui.h"
#include "worker.h"
#include "control.h"
#include "globals.h"

#ifndef MAJOR_VERSION
#define MAJOR_VERSION 0
#endif

#ifndef MINOR_VERSION
#define MINOR_VERSION 0
#endif

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, MAJOR_VERSION, MINOR_VERSION);

struct Globals globals;

int (*_sceDisplaySetFrameBufferInternal)(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync);

int sceDisplaySetFrameBufferInternalHook(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync) {
    globals.fpsCounter++;
    return _sceDisplaySetFrameBufferInternal(pri, topaddr, bufferwidth,
        pixelformat, sync);
}

int module_start(SceSize args, void *argp) {
    hook_function((unsigned int*) sceDisplaySetFrameBufferInternal,
        sceDisplaySetFrameBufferInternalHook,
        &_sceDisplaySetFrameBufferInternal);

    globals.active = 1;

    executeControlThread(args, argp);
    executeWorkerThread(args, argp);
    executeGuiThread(args, argp);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    globals.active = 0;
    return 0;
}
