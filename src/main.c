#include <pspmoduleinfo.h>

#include "control.h"
#include "globals.h"
#include "gui.h"
#include "worker.h"

#ifndef MAJOR_VERSION
#define MAJOR_VERSION 0
#endif

#ifndef MINOR_VERSION
#define MINOR_VERSION 0
#endif

PSP_MODULE_INFO("missyhud", PSP_MODULE_KERNEL, MAJOR_VERSION, MINOR_VERSION);

struct Globals globals;

int module_start(SceSize args, void *argp) {
    // Set before any thread starts, so the control thread cannot race the GUI
    // thread to initialise them.
    globals.active = 1;
    globals.show = 1;
    globals.guiPosition = TOP_LEFT;

    executeControlThread(args, argp);
    executeWorkerThread(args, argp);
    executeGuiThread(args, argp);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    globals.active = 0;

    // Removes the hooks first, then waits for every thread to leave its loop.
    // Returning while either is outstanding would leave the kernel calling into,
    // or running, memory that is about to be freed.
    stopGuiThread();
    stopWorkerThread();
    stopControlThread();

    return 0;
}
