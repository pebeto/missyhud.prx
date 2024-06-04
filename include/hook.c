/* 
* hook.c
*
* Code extracted from the source code of Joysens, one of the best plugins ever
* written for the PSP.
* Credits to Alexander "Raphael" Berl.
*
*/
#include <pspsdk.h>

#define GET_JUMP_TARGET(x) (0x80000000 | (((x) & 0x03FFFFFF) << 2))

int hook_function(unsigned int* jump, void* hook, unsigned int* result) {
    unsigned int target;
    unsigned int func;
    int inst;

    target = GET_JUMP_TARGET(*jump);
    while (((inst = _lw(target+4)) & ~0x03FFFFFF) != 0x0C000000)
        target += 4;

    if((inst & ~0x03FFFFFF) != 0x0C000000) {
        return 1;
    }

    *result = GET_JUMP_TARGET(inst);
    func = (unsigned int) hook;
    func = (func & 0x0FFFFFFF) >> 2;
    _sw(0x0C000000 | func, target+4);

    return 0;
}
