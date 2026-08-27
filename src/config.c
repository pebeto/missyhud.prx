/*
 * config.c
 *
 * Optional missyhud.config, parsed by hand.
 *
 * The file is a few KEY=VALUE lines, so there is no need for strtoul() or
 * sscanf(): the kernel libc this plugin links against does not carry all of
 * them, and a parser this small is easier to bound than to trust.
 */
#include <pspiofilemgr.h>

#include "config.h"
#include "gui.h"

// Only ever holds a handful of short lines, and anything past this is ignored
// rather than read into the stack.
#define CONFIG_BYTES 512

// A key plus its value, comfortably.
#define MAX_KEY 24

struct Config config;

static const char *const paths[] = {"ms0:/seplugins/missyhud.config",
                                    "ef0:/seplugins/missyhud.config"};

#define PATH_COUNT ((int)(sizeof(paths) / sizeof(paths[0])))

void configDefaults(void) {
    config.pinned = 0;
    config.posX = 0;
    config.posY = 0;
    config.fgColor = FG_COLOR;
    config.bgColor = BG_COLOR;
    config.showCpu = 1;
    config.showPower = 1;
    config.showRam = 1;
    config.showFps = 1;
}

static int isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/* Accepts 0x-prefixed hex and plain decimal. Returns 0 on anything else, so a
 * malformed value reads as zero rather than as whatever was in memory. */
static int parseNumber(const char *text, u32 *out) {
    u32 value = 0;
    int digits = 0;

    while (isSpace(*text)) {
        text++;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;

        for (; *text != '\0' && !isSpace(*text); text++) {
            char c = *text;
            u32 digit;

            if (c >= '0' && c <= '9') {
                digit = (u32)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = (u32)(c - 'a') + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = (u32)(c - 'A') + 10;
            } else {
                return 0;
            }

            /* Eight digits is a full u32; a ninth would wrap silently. */
            if (digits >= 8) {
                return 0;
            }

            value = (value << 4) | digit;
            digits++;
        }
    } else {
        for (; *text != '\0' && !isSpace(*text); text++) {
            if (*text < '0' || *text > '9') {
                return 0;
            }
            if (value > 0xffffffffu / 10) {
                return 0;
            }

            value = value * 10 + (u32)(*text - '0');
            digits++;
        }
    }

    if (digits == 0) {
        return 0;
    }

    *out = value;

    return 1;
}

static int keyMatches(const char *key, int length, const char *name) {
    int i;

    for (i = 0; i < length; i++) {
        if (name[i] == '\0' || key[i] != name[i]) {
            return 0;
        }
    }

    return name[length] == '\0';
}

/* The file is written the way a colour is normally written, 0xRRGGBB, while the
 * blitter and the framebuffer both want the red byte lowest. */
static u32 toBlitColour(u32 rrggbb) {
    return ((rrggbb & 0xff) << 16) | (rrggbb & 0xff00) | ((rrggbb >> 16) & 0xff);
}

static void applyPair(const char *key, int keyLength, const char *value) {
    u32 number;

    if (!parseNumber(value, &number)) {
        return;
    }

    /* An out of range position is rejected rather than clamped to the origin,
     * which would pin the HUD on the strength of a typo. */
    if (keyMatches(key, keyLength, "POS_X")) {
        if (number >= BLIT_COLS) {
            return;
        }
        config.posX = (u8)number;
        config.pinned = 1;
    } else if (keyMatches(key, keyLength, "POS_Y")) {
        if (number >= BLIT_ROWS) {
            return;
        }
        config.posY = (u8)number;
        config.pinned = 1;
    } else if (keyMatches(key, keyLength, "FG_COLOR")) {
        config.fgColor = toBlitColour(number & 0xffffff);
    } else if (keyMatches(key, keyLength, "BG_COLOR")) {
        config.bgColor = toBlitColour(number & 0xffffff);
    } else if (keyMatches(key, keyLength, "CPU_INDICATORS")) {
        config.showCpu = number != 0;
    } else if (keyMatches(key, keyLength, "POWER_INDICATORS")) {
        config.showPower = number != 0;
    } else if (keyMatches(key, keyLength, "RAM_INDICATORS")) {
        config.showRam = number != 0;
    } else if (keyMatches(key, keyLength, "FPS_INDICATOR")) {
        config.showFps = number != 0;
    }
}

/* One line, already terminated. Blank lines and # comments are skipped. */
static void applyLine(char *line) {
    char *cursor = line;
    char *equals;
    int keyLength;

    while (isSpace(*cursor)) {
        cursor++;
    }
    if (*cursor == '\0' || *cursor == '#') {
        return;
    }

    for (equals = cursor; *equals != '\0' && *equals != '='; equals++) {
    }
    if (*equals != '=') {
        return;
    }

    keyLength = (int)(equals - cursor);
    while (keyLength > 0 && isSpace(cursor[keyLength - 1])) {
        keyLength--;
    }
    if (keyLength == 0 || keyLength > MAX_KEY) {
        return;
    }

    applyPair(cursor, keyLength, equals + 1);
}

static int readFile(char *buffer, int capacity) {
    int i;

    for (i = 0; i < PATH_COUNT; i++) {
        SceUID fd = sceIoOpen(paths[i], PSP_O_RDONLY, 0777);
        int read;

        if (fd < 0) {
            continue;
        }

        read = sceIoRead(fd, buffer, (SceSize)(capacity - 1));
        sceIoClose(fd);

        if (read <= 0) {
            continue;
        }

        buffer[read] = '\0';

        return read;
    }

    return 0;
}

void loadConfig(void) {
    char buffer[CONFIG_BYTES];
    char *line;
    int i;
    int read;

    configDefaults();

    read = readFile(buffer, CONFIG_BYTES);
    if (read == 0) {
        return;
    }

    /* Split on newlines in place, so each line is its own terminated string. */
    line = buffer;
    for (i = 0; i < read; i++) {
        if (buffer[i] != '\n') {
            continue;
        }

        buffer[i] = '\0';
        applyLine(line);
        line = &buffer[i + 1];
    }

    /* Whatever follows the last newline, if the file does not end with one. */
    applyLine(line);
}
