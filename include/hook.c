/*
 * hook.c
 *
 * Function hijacking, delegated to the CFW SystemCtrl API. This replaces the
 * hand-rolled patcher extracted from Joysens by Alexander "Raphael" Berl, which
 * redirected the first jal *inside* the target: that made the hook stand in for
 * an inner callee rather than the function itself, so its signature was not the
 * function's and it ran once per inner call.
 *
 * sctrlHENHijackFunction() takes over the entry instead, and is stackable, so it
 * composes with other plugins that hook the same function.
 */
#include <psptypes.h>
#include <psputils.h>
#include <systemctrl.h>

#include "hook.h"

#define J_OPCODE 0x08000000
#define JUMP_MASK 0xFC000000
#define JUMP_TARGET_MASK 0x03FFFFFF
#define JUMP_REGION_MASK 0xF0000000

#define MAX_HOOKS 4

/* Words snapshotted at the function entry so unhook_all() can restore them.
 * SystemCtrl offers no un-hijack call, and FunctionPatchData's layout is not
 * documented, so the entry is saved and compared here instead of unpicking it. */
#define ENTRY_WORDS 8

/* pspsdk renamed FunctionPatchData to SctrlFunctionPatchData, so the layout is
 * mirrored here and passed as void * to build against either name. */
typedef struct {
    unsigned int instructions[5];
    unsigned int extra[3];
} PatchData;

static struct {
    u32 *entry;
    u32 original[ENTRY_WORDS];
    u32 patched[ENTRY_WORDS];
    PatchData patch;
} hooks[MAX_HOOKS];
static int hook_count = 0;

/* The loader rewrites a kernel module's import stubs as `j realfunc; nop`, so
 * the real address can be read back without guessing at a NID or module name.
 * The jump region comes from the stub's own address rather than a hardcoded
 * 0x80000000, and a stub that is not a jump is rejected instead of followed. */
static u32 *resolve_stub(const u32 *stub) {
    u32 instruction;

    if (stub == NULL) {
        return NULL;
    }

    instruction = *stub;
    if ((instruction & JUMP_MASK) != J_OPCODE) {
        return NULL;
    }

    return (u32 *)(((u32)stub & JUMP_REGION_MASK) |
                   ((instruction & JUMP_TARGET_MASK) << 2));
}

static void snapshot(u32 *dst, const u32 *entry) {
    int i;

    for (i = 0; i < ENTRY_WORDS; i++) {
        dst[i] = _lw((u32)(entry + i));
    }
}

static int entry_matches(const u32 *entry, const u32 *expected) {
    int i;

    for (i = 0; i < ENTRY_WORDS; i++) {
        if (_lw((u32)(entry + i)) != expected[i]) {
            return 0;
        }
    }

    return 1;
}

int hook_function(void *stub, void *hook, void **original) {
    u32 *entry = resolve_stub((const u32 *)stub);

    if (entry == NULL || hook_count >= MAX_HOOKS) {
        return 1;
    }

    snapshot(hooks[hook_count].original, entry);
    sctrlHENHijackFunction((void *)&hooks[hook_count].patch, entry, hook, original);
    snapshot(hooks[hook_count].patched, entry);

    hooks[hook_count].entry = entry;
    hook_count++;

    return 0;
}

void unhook_all(void) {
    int restored = 0;

    while (hook_count > 0) {
        u32 *entry;
        int i;

        hook_count--;
        entry = hooks[hook_count].entry;

        /* Only restore an entry that is still exactly as the hijack left it.
         * If another module has stacked its own hook on top, putting our copy
         * back would tear theirs out. */
        if (!entry_matches(entry, hooks[hook_count].patched)) {
            continue;
        }

        for (i = 0; i < ENTRY_WORDS; i++) {
            _sw(hooks[hook_count].original[i], (u32)(entry + i));
        }
        restored = 1;
    }

    if (restored) {
        /* The instructions were written as data, so the writes have to reach
         * memory and the stale copies have to leave the instruction cache. */
        sceKernelDcacheWritebackAll();
        sceKernelIcacheInvalidateAll();
    }
}
