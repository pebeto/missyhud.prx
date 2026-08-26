#ifndef UTILS_H
#define UTILS_H

#include <pspkerneltypes.h>

/**
 * Check if the device is a PSP Go by looking at its Tachyon revision.
 * The result is probed once and cached.
 *
 * @return 1 if the device is a PSP Go, 0 otherwise.
 */
int isPSPGo(void);

/**
 * Wait for a thread to leave its loop, then delete it. The wait is bounded so
 * a wedged thread cannot hang module_stop().
 *
 * @param thid Thread to wait for. Negative values are ignored.
 */
void waitForThread(SceUID thid);

#endif
