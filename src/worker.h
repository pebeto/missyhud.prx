#ifndef WORKER_H
#define WORKER_H

#include <pspkerneltypes.h>

#define FAT_RAM 32
#define SLIM_RAM 64

void executeWorkerThread(SceSize args, void *argp);

#endif
