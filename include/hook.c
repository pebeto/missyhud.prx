/*
 * hook.c
 *
 * Function interception through the CFW's syscall table.
 *
 * The obvious call for this is sctrlHENHijackFunction(), which patches a
 * function's entry and stacks with other plugins. ARK-4 does not export it: its
 * SystemCtrlForKernel table has no Hijack entry at all, so the import resolves
 * to something inert, the entry is never patched, and the hook is simply never
 * reached. pspsdk declaring the prototype says nothing about the running CFW.
 *
 * sctrlHENPatchSyscall() is exported by every CFW, and it is what the earlier
 * hand-rolled patcher, extracted from Joysens by Alexander "Raphael" Berl, was
 * standing in for. It redirects the syscall table entry rather than the
 * function, so the function is left as it is and no trampoline is needed. Only
 * user mode enters through a syscall, which is the side a game and the XMB are
 * both on.
 */
#include <psptypes.h>
#include <systemctrl.h>

#include "hook.h"

#define MAX_HOOKS 4

static struct {
    void *original;
    void *hook;
} hooks[MAX_HOOKS];
static int hook_count = 0;

int hook_syscall(const char *module, const char *library, u32 nid, void *hook,
                 void **original) {
    u32 addr;

    *original = NULL;

    if (hook_count >= MAX_HOOKS) {
        return HOOK_NO_SLOT;
    }

    addr = sctrlHENFindFunction(module, library, nid);
    if (addr == 0) {
        return HOOK_NOT_FOUND;
    }

    sctrlHENPatchSyscall((void *)addr, hook);
    sctrlFlushCache();

    hooks[hook_count].original = (void *)addr;
    hooks[hook_count].hook = hook;
    hook_count++;

    // The function itself is untouched, so its address stays the way to call it.
    *original = (void *)addr;

    return HOOK_OK;
}

void unhook_all(void) {
    while (hook_count > 0) {
        hook_count--;
        // Matches only entries still pointing at our hook, so a plugin that has
        // since redirected the same syscall is left alone.
        sctrlHENPatchSyscall(hooks[hook_count].hook, hooks[hook_count].original);
    }

    sctrlFlushCache();
}
