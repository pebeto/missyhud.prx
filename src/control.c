#include <pspctrl.h>
#include <pspthreadman.h>

#include "control.h"
#include "globals.h"
#include "utils.h"

#define CONTROL_STACK_SIZE 0x1000
#define CONTROL_PRIORITY 0x10

// Also the granularity at which this thread notices module_stop().
#define CONTROL_POLL_INTERVAL (ONE_SECOND / 10)

// Polls the toggle combination must be held to fire, i.e. one second.
#define TOGGLE_HOLD_POLLS 10

#define ANALOG_RIGHT 200
#define ANALOG_LEFT 50

static SceUID controlThid = -1;

static int isTogglePressed(const SceCtrlData *pad) {
    return (pad->Buttons & PSP_CTRL_LTRIGGER) && (pad->Buttons & PSP_CTRL_RTRIGGER) &&
           (pad->Buttons & PSP_CTRL_START);
}

// 1 to cycle forwards, -1 to cycle backwards, 0 for no request.
static int positionRequest(const SceCtrlData *pad) {
    if (!(pad->Buttons & PSP_CTRL_START)) {
        return 0;
    }
    if (pad->Lx > ANALOG_RIGHT) {
        return 1;
    }
    if (pad->Lx < ANALOG_LEFT) {
        return -1;
    }
    return 0;
}

static void cyclePosition(int direction) {
    u8 position = globals.guiPosition;

    if (position >= GUI_POSITION_COUNT) {
        position = TOP_LEFT;
    }

    globals.guiPosition = direction > 0
                              ? (u8)((position + 1) % GUI_POSITION_COUNT)
                              : (u8)((position + GUI_POSITION_COUNT - 1) %
                                     GUI_POSITION_COUNT);
}

static int controlThread(unsigned int args, void *argp) {
    u8 togglePolls = 0;
    int lastDirection = 0;
    // A combination that already reads as held when the thread starts must not
    // fire, so the toggle arms only once it has been seen released.
    int toggleArmed = 0;

    (void)args;
    (void)argp;

    sceKernelDelayThread(ONE_SECOND);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (globals.active) {
        SceCtrlData pad;
        int direction;
        int toggleHeld;

        sceKernelDelayThread(CONTROL_POLL_INTERVAL);

        // Peek rather than Read, so the plugin never blocks on a sample or
        // takes one from the game that is also reading the pad.
        if (sceCtrlPeekBufferPositive(&pad, 1) < 1) {
            continue;
        }

        toggleHeld = isTogglePressed(&pad);

        // The count saturates, so this fires exactly once on the poll where the
        // hold completes and not again until the combination is released.
        if (toggleHeld && toggleArmed) {
            if (togglePolls < TOGGLE_HOLD_POLLS) {
                togglePolls++;
                if (togglePolls == TOGGLE_HOLD_POLLS) {
                    globals.show = !globals.show;
                }
            }
        } else if (!toggleHeld) {
            toggleArmed = 1;
            togglePolls = 0;
        }

        // Edge triggered, so one flick of the stick moves exactly one step
        // instead of racing through the positions at the poll rate. The toggle
        // combination also holds START, so it must not cycle as well.
        direction = toggleHeld ? 0 : positionRequest(&pad);
        if (direction != 0 && direction != lastDirection) {
            cyclePosition(direction);
        }
        lastDirection = direction;
    }

    sceKernelExitThread(0);
    return 0;
}

void executeControlThread(SceSize args, void *argp) {
    controlThid = sceKernelCreateThread("missyhud_control_thread", controlThread,
                                        CONTROL_PRIORITY, CONTROL_STACK_SIZE, 0, NULL);

    if (controlThid >= 0) {
        sceKernelStartThread(controlThid, args, argp);
    }
}

void stopControlThread(void) {
    waitForThread(controlThid);
    controlThid = -1;
}
