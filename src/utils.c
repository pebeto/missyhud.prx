#include <pspsysreg.h>
#include <pspthreadman.h>

#include "globals.h"
#include "utils.h"

#define TACHYON_GO_04G 0x00720000
#define TACHYON_GO_09G 0x00810000

// How long module_stop() waits for a thread to notice globals.active.
#define THREAD_EXIT_TIMEOUT (2 * ONE_SECOND)

int isPSPGo(void) {
    // Probed once: the GUI thread asks every frame and this is a syscall.
    static int isGo = -1;

    if (isGo < 0) {
        u32 tachyonVersion = sceSysregGetTachyonVersion();
        isGo = (tachyonVersion == TACHYON_GO_04G || tachyonVersion == TACHYON_GO_09G);
    }

    return isGo;
}

void waitForThread(SceUID thid) {
    SceUInt timeout = THREAD_EXIT_TIMEOUT;

    if (thid < 0) {
        return;
    }

    if (sceKernelWaitThreadEnd(thid, &timeout) < 0) {
        // Wedged or timed out. A thread left running in memory that is about to
        // be freed is worse than one torn down from under itself, and
        // sceKernelDeleteThread() refuses anything not already dormant.
        sceKernelTerminateDeleteThread(thid);
        return;
    }

    sceKernelDeleteThread(thid);
}
