#ifndef GLOBALS_H
#define GLOBALS_H

#include <psptypes.h>

struct Globals {
    u8 fps;
    u8 show;
    u8 active;
    u8 usedMemory;
    u8 totalMemory;
    u32 fps_counter;
    u8 isBatteryExist;
    u16 batteryLifeTime;
    u8 isBatteryCharging;
    u8 batteryLifePercent;
    u32 busClockFrequency;
    u32 cpuClockFrequency;
};

extern struct Globals globals;

#endif
