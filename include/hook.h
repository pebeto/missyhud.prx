#ifndef HOOK_H
#define HOOK_H

#include <psptypes.h>

// Why a hook did not install. Reported rather than swallowed, because a hook
// that silently fails to install looks exactly like a function nobody calls.
enum HookStatus {
    HOOK_OK,
    HOOK_NOT_FOUND, // The firmware does not export the function.
    HOOK_NO_SLOT    // MAX_HOOKS already in use.
};

/**
 * Take over a function that user mode reaches through a syscall, which is how
 * both a game and the XMB call it.
 *
 * @param module Module exporting the function, e.g. "sceDisplay_Service".
 * @param library Export library the syscall resolves through, e.g. "sceDisplay".
 * @param nid NID of the function within that library.
 * @param hook Function to take over. Its signature has to be the target's.
 * @param original Receives the real function, for the hook to call.
 *
 * @return One of ::HookStatus.
 */
int hook_syscall(const char *module, const char *library, u32 nid, void *hook,
                 void **original);

/**
 * Point every syscall this module redirected back at the real function.
 */
void unhook_all(void);

#endif
