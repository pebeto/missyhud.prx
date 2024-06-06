#ifndef GLOBALS_H
#define GLOBALS_H

#define ONE_SECOND 1000000

#include <psptypes.h>

struct Globals {
    u8 fps;
    u8 show;
    u8 active;
    u8 cpuUsage;
    u8 usedMemory;
    u8 totalMemory;
    u32 fpsCounter;
    u8 isBatteryExist;
    u16 batteryLifeTime;
    u8 isBatteryCharging;
    u8 batteryLifePercent;
    u32 busClockFrequency;
    u32 cpuClockFrequency;
};

extern struct Globals globals;

#endif
