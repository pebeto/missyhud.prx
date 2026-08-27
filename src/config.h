#ifndef CONFIG_H
#define CONFIG_H

#include <psptypes.h>

// Where the HUD is drawn, when the config pins it. Character cells rather than
// pixels, because the glyphs are placed on a cell grid and a pixel position
// would only be rounded back onto it.
struct Config {
    // Set when the config supplied a position, which takes the place of the
    // nine cycled ones.
    u8 pinned;
    u8 posX;
    u8 posY;

    // Held the way blit_text() wants them, 0xBBGGRR, already converted from the
    // 0xRRGGBB the file is written in.
    u32 fgColor;
    u32 bgColor;

    u8 showCpu;
    u8 showPower;
    u8 showRam;
    u8 showFps;
};

extern struct Config config;

/**
 * Put the built-in defaults in place. Safe to call from module_start(), and it
 * has to be, so the display hook has something sane to draw with before
 * loadConfig() has had a chance to run.
 */
void configDefaults(void);

/**
 * Apply missyhud.config on top of the defaults, if there is one.
 *
 * Reads ms0:/seplugins/ and ef0:/seplugins/, so it works on a PSP Go without
 * being told which it is. A file that is missing, unreadable or malformed
 * leaves the defaults in place, and a key that is not recognised is skipped
 * rather than failing the rest of the file.
 *
 * Has to be called from a thread rather than from module_start(), because the
 * memory stick is not necessarily mounted that early.
 */
void loadConfig(void);

#endif
