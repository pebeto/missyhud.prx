#include <pspsysreg.h>

int isPSPGo() {
    u32 tachyonVersion = sceSysregGetTachyonVersion();
    return tachyonVersion == 0x00720000 || tachyonVersion == 0x00810000;
}
