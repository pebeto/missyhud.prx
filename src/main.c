#include <pspmoduleinfo.h>

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

int module_start(SceSize args, void *argp) {
    globals.active = 1;

    executeControlThreads(args, argp);
    executeWorkerThread(args, argp);
    executeGuiThread(args, argp);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    globals.active = 0;
    return 0;
}
