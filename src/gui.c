#include <stdio.h>

#include <pspdisplay.h>
#include <pspthreadman.h>
#include <pspdisplay_kernel.h>
#include <string.h>

#include "include/blit.h"
#include "include/hook.h"

#include "gui.h"
#include "utils.h"
#include "globals.h"

int (*_sceDisplaySetFrameBufferInternal)(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync);

int sceDisplaySetFrameBufferInternalHook(int pri, void* topaddr,
    int bufferwidth, int pixelformat, int sync) {
    globals.fpsCounter++;
    return _sceDisplaySetFrameBufferInternal(pri, topaddr, bufferwidth,
        pixelformat, sync);
}

void printCpuIndicators(GuiComponent cpu) {
    char msg[38];
    sprintf(msg, "CPU: %u%% (%lu/%lu MHz)", globals.cpuUsage,
        globals.cpuClockFrequency, globals.busClockFrequency);
    blit_string(cpu.x, cpu.y, msg, FG_COLOR, BG_COLOR);
}

void printPowerIndicators(GuiComponent power) {
    if (!(globals.isBatteryExist)) {
        blit_string(power.x, power.y, "Power: No battery", FG_COLOR, BG_COLOR);
        return;
    }
    else {
        char batteryLifePercentBuffer[18];

        if (isPSPGo()) {
            if (globals.batteryLifePercent >= 60) {
                strcpy(batteryLifePercentBuffer, "High");
            }
            else if (globals.batteryLifePercent >= 30) {
                strcpy(batteryLifePercentBuffer, "Medium");
            }
            else {
                strcpy(batteryLifePercentBuffer, "Low");
            }
        }
        else {
            sprintf(batteryLifePercentBuffer, "%u%% (%u mins)",
                globals.batteryLifePercent, globals.batteryLifeTime);
        }

        if (globals.isBatteryCharging) {
            char percentMsgCharging[22];
            sprintf(percentMsgCharging, "Power: %s (charging...)",
                batteryLifePercentBuffer);
            blit_string(power.x, power.y, percentMsgCharging, FG_COLOR,
                BG_COLOR);
        }
        else {
            char percentMsgMinutes[8];
            sprintf(percentMsgMinutes, "Power: %s", batteryLifePercentBuffer);
            blit_string(power.x, power.y, percentMsgMinutes, FG_COLOR,
                BG_COLOR);
        }
    }
}

void printMemoryUsage(GuiComponent memory) {
    char msg[16];
    sprintf(msg, "RAM: %u/%u MB", globals.usedMemory, globals.totalMemory);
    blit_string(memory.x, memory.y, msg, FG_COLOR, BG_COLOR);
}

void printFps(GuiComponent fps) {
    char msg[9];
    sprintf(msg, "FPS: %u", globals.fps);
    blit_string(fps.x, fps.y, msg, FG_COLOR, BG_COLOR);
}

void setPositions(GuiComponent* cpu, GuiComponent* power, GuiComponent* memory,
    GuiComponent* fps) {
    switch (globals.guiPosition) {
        case TOP_RIGHT:
            cpu->x = power->x = memory->x = fps->x = W_MAX;
            cpu->y = 0;
            power->y = 1;
            memory->y = 2;
            fps->y = 3;
            break;
        case BOTTOM_LEFT:
            cpu->x = power->x = memory->x = fps->x = 0;
            cpu->y = H_MAX - 3;
            power->y = H_MAX - 2;
            memory->y = H_MAX - 1;
            fps->y = H_MAX;
            break;
        case BOTTOM_RIGHT:
            cpu->x = power->x = memory->x = fps->x = W_MAX;
            cpu->y = H_MAX - 3;
            power->y = H_MAX - 2;
            memory->y = H_MAX - 1;
            fps->y = H_MAX;
            break;
        case MID_LEFT:
            cpu->x = power->x = memory->x = fps->x = 0;
            cpu->y = H_MAX/2 - 3;
            power->y = H_MAX/2 - 2;
            memory->y = H_MAX/2 - 1;
            fps->y = H_MAX/2;
            break;
        case MID_RIGHT:
            cpu->x = power->x = memory->x = fps->x = W_MAX;
            cpu->y = H_MAX/2 - 3;
            power->y = H_MAX/2 - 2;
            memory->y = H_MAX/2 - 1;
            fps->y = H_MAX/2;
            break;
        case MID_TOP:
            cpu->x = power->x = memory->x = fps->x = W_MAX/2;
            cpu->y = 0;
            power->y = 1;
            memory->y = 2;
            fps->y = 3;
            break;
        case MID_BOTTOM:
            cpu->x = power->x = memory->x = fps->x = W_MAX/2;
            cpu->y = H_MAX - 3;
            power->y = H_MAX - 2;
            memory->y = H_MAX - 1;
            fps->y = H_MAX;
            break;
        case CENTER:
            cpu->x = power->x = memory->x = fps->x = W_MAX/2;
            cpu->y = H_MAX/2 - 3;
            power->y = H_MAX/2 - 2;
            memory->y = H_MAX/2 - 1;
            fps->y = H_MAX/2;
            break;
        case TOP_LEFT: default:
            cpu->x = power->x = memory->x = fps->x = 0;
            cpu->y = 0;
            power->y = 1;
            memory->y = 2;
            fps->y = 3;
            break;
    }
}

int guiThread(unsigned int args, void* argp) {
    sceKernelDelayThread(ONE_SECOND);

    globals.show = 1;
    globals.guiPosition = 0;

    GuiComponent cpu, power, memory, fps;

    while (globals.active) {
        sceKernelDelayThreadCB(200);

        setPositions(&cpu, &power, &memory, &fps);

        if (globals.show) {
            printCpuIndicators(cpu);
            printPowerIndicators(power);
            printMemoryUsage(memory);
            printFps(fps);
        }
        sceDisplayWaitVblankStart();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void executeGuiThread(SceSize args, void* argp) {
    hook_function((unsigned int*)sceDisplaySetFrameBufferInternal,
        sceDisplaySetFrameBufferInternalHook,
        (unsigned int*)&_sceDisplaySetFrameBufferInternal);

    int thid = sceKernelCreateThread("missyhud_gui_thread", guiThread, 0x10,
        0x200, 0, NULL);

    if (thid >= 0) {
        sceKernelStartThread(thid, args, argp);
    }
}
