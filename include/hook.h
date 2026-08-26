#ifndef HOOK_H
#define HOOK_H

/**
 * Hijack a kernel function, given the import stub the loader resolved for it.
 * The hook takes over the function itself, so its signature is the function's
 * own and it runs for every call.
 *
 * @param stub Import stub for the target, e.g. sceDisplaySetFrameBuf.
 * @param hook Function to take over.
 * @param original Receives a pointer used to call the original (or the next
 *                 hook, if another module has already hijacked it).
 *
 * @return 0 on success, 1 if the stub could not be resolved or no slot is free.
 */
int hook_function(void *stub, void *hook, void **original);

/**
 * Put back every entry this module hijacked. A function another module has since
 * hijacked on top of ours is left alone rather than torn out from under it.
 */
void unhook_all(void);

#endif
