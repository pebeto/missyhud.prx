/*
 * ge.c
 *
 * HUD rendering through the graphics engine instead of the CPU.
 *
 * The CPU blitter in blit.c writes every pixel of every glyph into the target
 * buffer on every frame, which is both bus traffic the GE is competing for and a
 * race: a display list the GE has not finished executing yet will overwrite the
 * text that was just written. Handing the same work to the GE removes both,
 * because commands in a queued list run after the ones already queued.
 *
 * sceGu is not used. It keeps its state per module, and calling sceGuInit() next
 * to a game that owns the GE would reset state the game is relying on. The list
 * here is assembled by hand from the raw command words, which is also what lets
 * it be submitted without a stall: sceGeSaveContext() and sceGeRestoreContext()
 * write registers immediately, so they cannot bracket a list that runs later.
 * The list restores what it touched itself, as its last commands.
 *
 * Command opcodes and argument encodings follow pspsdk's own libpspgu.
 */
#include <string.h>

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <psputils.h>

#include "ge.h"

/* Raw GE commands, from pspsdk's guInternal.h. */
#define CMD_NOP 0x00
#define CMD_VADDR 0x01
#define CMD_PRIM 0x04
#define CMD_END 0x0c
#define CMD_FINISH 0x0f
#define CMD_BASE 0x10
#define CMD_VERTEX_TYPE 0x12
#define CMD_REGION1 0x15
#define CMD_REGION2 0x16
#define CMD_LIGHTING_ENABLE 0x17
#define CMD_DEPTH_CLIP_ENABLE 0x1c
#define CMD_CULL_FACE_ENABLE 0x1d
#define CMD_TEXTURE_ENABLE 0x1e
#define CMD_FOG_ENABLE 0x1f
#define CMD_DITHER_ENABLE 0x20
#define CMD_ALPHA_BLEND_ENABLE 0x21
#define CMD_ALPHA_TEST_ENABLE 0x22
#define CMD_Z_TEST_ENABLE 0x23
#define CMD_STENCIL_TEST_ENABLE 0x24
#define CMD_TEX_SCALE_U 0x48
#define CMD_TEX_SCALE_V 0x49
#define CMD_TEX_OFFSET_U 0x4a
#define CMD_TEX_OFFSET_V 0x4b
#define CMD_OFFSET_X 0x4c
#define CMD_OFFSET_Y 0x4d
#define CMD_FRAME_BUF_PTR 0x9c
#define CMD_FRAME_BUF_WIDTH 0x9d
#define CMD_TEX_ADDR0 0xa0
#define CMD_TEX_BUF_WIDTH0 0xa8
#define CMD_TEX_SIZE0 0xb8
#define CMD_TEX_MODE 0xc2
#define CMD_TEX_FORMAT 0xc3
#define CMD_TEX_FILTER 0xc6
#define CMD_TEX_FUNC 0xc9
#define CMD_TEX_FLUSH 0xcb
#define CMD_FRAMEBUF_PIX_FORMAT 0xd2
#define CMD_SCISSOR1 0xd4
#define CMD_SCISSOR2 0xd5

/* 8x8 glyphs laid out 16 across, so a 128x64 texture covers 7-bit ASCII.
 *
 * The colours are baked into a 5551 texture rather than kept in a CLUT. A CLUT
 * would be a quarter of the size, but loading one overwrites the palette the
 * game has sitting in GE memory, and a game that does not reload its own CLUT
 * every frame would draw with ours. 16KB of kernel memory is the cheaper price.
 * It also means the texture format no longer has to match the framebuffer: the
 * GE converts, where the CPU blitter needed a path per pixel format. */
#define GLYPH 8
#define TEX_COLS 16
#define TEX_WIDTH (TEX_COLS * GLYPH)
#define TEX_HEIGHT 64
#define TEX_BYTES (TEX_WIDTH * TEX_HEIGHT * 2)

#define CELL_WIDTH BLIT_CELL_WIDTH
#define CELL_HEIGHT BLIT_CELL_HEIGHT

#define MAX_GLYPHS (GE_MAX_COLS * GE_MAX_ROWS)
#define MAX_VERTICES (MAX_GLYPHS * 2)

#define LIST_WORDS 192

/* Two lists, used alternately. Nothing waits for a submission, so the buffer
 * being patched must not be one the GE could still be reading. */
#define LIST_COUNT 2

/* Consecutive failed submissions before the GE path gives up and the caller
 * goes back to blitting. Better a HUD that flickers than one that vanishes. */
#define MAX_FAILURES 4

/* 8x8 debug font, from libpspdebug. Same source the CPU blitter uses. */
extern u8 msx[];

/* Two vertices per sprite: top left and bottom right. */
typedef struct {
    s16 u;
    s16 v;
    s16 x;
    s16 y;
    s16 z;
} GeVertex;

static u16 fontTexture[TEX_WIDTH * TEX_HEIGHT] __attribute__((aligned(64)));
static GeVertex vertices[MAX_VERTICES] __attribute__((aligned(16)));
static u32 lists[LIST_COUNT][LIST_WORDS] __attribute__((aligned(16)));
static int currentList = 0;

static int vertexCount = 0;
static int ready = 0;
static int failures = 0;
static u32 lastError = 0;

/* sceGeListEnQueue() takes a callback id from sceGeSetCallback() and has no
 * documented value for "none". Passing -1 got past the privilege check once k1
 * was cleared and then crashed the machine, so a real one is registered even
 * though nothing here needs to be told when a list finishes. */
static int callbackId = -1;

/* Run from the GE's interrupt, so they do nothing at all. */
static void geSignalHandler(int id, void *arg) {
    (void)id;
    (void)arg;
}

static void geFinishHandler(int id, void *arg) {
    (void)id;
    (void)arg;
}

/* Slots patched every frame, filled in while the list is assembled. */
static int slotPixelFormat;
static int slotFrameBufPtr;
static int slotFrameBufWidth;
static int slotPrim;
static int slotEpilogue;
static int listWords;

/* Registers the list writes, so the epilogue can put them back. The frame
 * buffer and vertex registers are left out: a game sets those itself before it
 * draws anything, and restoring a stale buffer address is worse than leaving
 * ours behind. */
static const u8 restoreCmds[] = {
    CMD_TEXTURE_ENABLE,    CMD_Z_TEST_ENABLE,      CMD_ALPHA_BLEND_ENABLE,
    CMD_ALPHA_TEST_ENABLE, CMD_STENCIL_TEST_ENABLE, CMD_CULL_FACE_ENABLE,
    CMD_LIGHTING_ENABLE,   CMD_FOG_ENABLE,         CMD_DITHER_ENABLE,
    CMD_DEPTH_CLIP_ENABLE, CMD_TEX_MODE,           CMD_TEX_FORMAT,
    CMD_TEX_FILTER,        CMD_TEX_FUNC,           CMD_TEX_ADDR0,
    CMD_TEX_BUF_WIDTH0,    CMD_TEX_SIZE0,
    CMD_TEX_SCALE_U,       CMD_TEX_SCALE_V,        CMD_TEX_OFFSET_U,
    CMD_TEX_OFFSET_V,      CMD_SCISSOR1,           CMD_SCISSOR2,
    CMD_REGION1,           CMD_REGION2,            CMD_OFFSET_X,
    CMD_OFFSET_Y};

#define RESTORE_COUNT ((int)(sizeof(restoreCmds) / sizeof(restoreCmds[0])))

static u32 command(u8 cmd, u32 argument) {
    return ((u32)cmd << 24) | (argument & 0xffffff);
}

/* A float argument keeps its top 24 bits, the way sendCommandf() packs it. */
static u32 commandf(u8 cmd, float argument) {
    union {
        float f;
        u32 i;
    } bits;

    bits.f = argument;

    return command(cmd, bits.i >> 8);
}

/* The GE addresses memory physically, so the kernel window has to come off any
 * pointer that goes inside the list. */
static u32 gePhys(const void *pointer) {
    return (u32)pointer & 0x1fffffff;
}

static int log2i(int value) {
    int exponent = 0;

    while ((1 << exponent) < value) {
        exponent++;
    }

    return exponent;
}

/* Same packing blit.c uses for a 5551 target, with the alpha bit forced on so
 * the glyph does not depend on any material or vertex colour state. */
static u16 to5551(u32 colour) {
    return (u16)(0x8000 | ((colour >> 3) & 0x1f) | (((colour >> 11) & 0x1f) << 5) |
                 (((colour >> 19) & 0x1f) << 10));
}

/* Paints every texel of every glyph cell, foreground where the font bit is set
 * and background elsewhere, which is what the CPU blitter did per frame. */
static void buildFontTexture(u32 fg, u32 bg) {
    u16 ink = to5551(fg);
    u16 paper = to5551(bg);
    int ch;

    for (ch = 0; ch < TEX_COLS * (TEX_HEIGHT / GLYPH); ch++) {
        int originX = (ch % TEX_COLS) * GLYPH;
        int originY = (ch / TEX_COLS) * GLYPH;
        int line;

        for (line = 0; line < GLYPH; line++) {
            u8 bits = msx[ch * GLYPH + line];
            int y = originY + line;
            int bit;

            for (bit = 0; bit < GLYPH; bit++) {
                int x = originX + bit;

                fontTexture[y * TEX_WIDTH + x] = (bits & (0x80 >> bit)) ? ink : paper;
            }
        }
    }
}

static void buildList(u32 *list) {
    int i = 0;
    int j;

    /* Patched per frame from the target the hook was handed. */
    slotPixelFormat = i;
    list[i++] = command(CMD_FRAMEBUF_PIX_FORMAT, 0);
    slotFrameBufPtr = i;
    list[i++] = command(CMD_FRAME_BUF_PTR, 0);
    slotFrameBufWidth = i;
    list[i++] = command(CMD_FRAME_BUF_WIDTH, 0);

    /* Whole screen, no view transform: sprite coordinates are pixels. */
    list[i++] = command(CMD_SCISSOR1, 0);
    list[i++] = command(CMD_SCISSOR2, ((BLIT_SCREEN_HEIGHT - 1) << 10) |
                                          (BLIT_SCREEN_WIDTH - 1));
    list[i++] = command(CMD_REGION1, 0);
    list[i++] = command(CMD_REGION2, ((BLIT_SCREEN_HEIGHT - 1) << 10) |
                                         (BLIT_SCREEN_WIDTH - 1));
    list[i++] = command(CMD_OFFSET_X, 0);
    list[i++] = command(CMD_OFFSET_Y, 0);

    /* Nothing that could reject or tint a fragment. */
    list[i++] = command(CMD_Z_TEST_ENABLE, 0);
    list[i++] = command(CMD_ALPHA_BLEND_ENABLE, 0);
    list[i++] = command(CMD_ALPHA_TEST_ENABLE, 0);
    list[i++] = command(CMD_STENCIL_TEST_ENABLE, 0);
    list[i++] = command(CMD_CULL_FACE_ENABLE, 0);
    list[i++] = command(CMD_LIGHTING_ENABLE, 0);
    list[i++] = command(CMD_FOG_ENABLE, 0);
    list[i++] = command(CMD_DITHER_ENABLE, 0);
    list[i++] = command(CMD_DEPTH_CLIP_ENABLE, 0);

    list[i++] = command(CMD_TEXTURE_ENABLE, 1);
    /* No mipmaps, single CLUT, unswizzled. */
    list[i++] = command(CMD_TEX_MODE, 0);
    list[i++] = command(CMD_TEX_FORMAT, GU_PSM_5551);

    list[i++] = command(CMD_TEX_ADDR0, gePhys(fontTexture));
    list[i++] = command(CMD_TEX_BUF_WIDTH0,
                        ((gePhys(fontTexture) >> 8) & 0x0f0000) | TEX_WIDTH);
    list[i++] = command(CMD_TEX_SIZE0,
                        (log2i(TEX_HEIGHT) << 8) | log2i(TEX_WIDTH));
    list[i++] = command(CMD_TEX_FILTER, GU_NEAREST | (GU_NEAREST << 8));
    /* The texture is the colour, alpha included: nothing outside the list. */
    list[i++] = command(CMD_TEX_FUNC, GU_TFX_REPLACE | (GU_TCC_RGBA << 8));
    /* 2D coordinates are texels already, so scale by one and do not shift. */
    list[i++] = commandf(CMD_TEX_SCALE_U, 1.0f);
    list[i++] = commandf(CMD_TEX_SCALE_V, 1.0f);
    list[i++] = commandf(CMD_TEX_OFFSET_U, 0.0f);
    list[i++] = commandf(CMD_TEX_OFFSET_V, 0.0f);
    list[i++] = command(CMD_TEX_FLUSH, 0);

    list[i++] = command(CMD_VERTEX_TYPE,
                        GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D);
    list[i++] = command(CMD_BASE, (gePhys(vertices) >> 8) & 0xf0000);
    list[i++] = command(CMD_VADDR, gePhys(vertices));

    /* Vertex count is patched per frame; zero draws nothing. */
    slotPrim = i;
    list[i++] = command(CMD_PRIM, (GU_SPRITES << 16) | 0);

    slotEpilogue = i;
    for (j = 0; j < RESTORE_COUNT; j++) {
        list[i++] = command(CMD_NOP, 0);
    }

    list[i++] = command(CMD_FINISH, 0);
    list[i++] = command(CMD_END, 0);

    listWords = i;
}

int geInit(u32 fg, u32 bg) {
    PspGeCallbackData callback;
    int i;

    ready = 0;
    failures = 0;
    lastError = 0;
    currentList = 0;

    buildFontTexture(fg, bg);

    for (i = 0; i < LIST_COUNT; i++) {
        buildList(lists[i]);
    }

    if (listWords > LIST_WORDS) {
        return 0;
    }

    sceKernelDcacheWritebackRange(fontTexture, sizeof(fontTexture));

    callback.signal_func = geSignalHandler;
    callback.signal_arg = NULL;
    callback.finish_func = geFinishHandler;
    callback.finish_arg = NULL;

    callbackId = sceGeSetCallback(&callback);
    if (callbackId < 0) {
        lastError = (u32)callbackId;
        return 0;
    }

    ready = 1;

    return 1;
}

void geShutdown(void) {
    ready = 0;

    /* The handlers live in this module, so the driver must stop pointing at
     * them before the memory goes away. */
    if (callbackId >= 0) {
        sceGeUnsetCallback(callbackId);
        callbackId = -1;
    }
}

int geReady(void) {
    return ready;
}

void geBeginText(void) {
    vertexCount = 0;
}

void geAddText(int col, int row, const char *msg) {
    int x = col * CELL_WIDTH;
    int y = row * CELL_HEIGHT;

    if (msg == NULL || col < 0 || row < 0) {
        return;
    }

    for (; *msg != '\0'; msg++, x += CELL_WIDTH) {
        u8 ch = (u8)*msg;
        int u;
        int v;

        if (vertexCount + 2 > MAX_VERTICES) {
            return;
        }
        /* Clip rather than wrap, the way blit_text() does. */
        if (x + GLYPH > BLIT_SCREEN_WIDTH || y + GLYPH > BLIT_SCREEN_HEIGHT) {
            return;
        }

        /* The font texture only holds 7-bit ASCII. */
        ch &= 0x7f;
        u = (ch % TEX_COLS) * GLYPH;
        v = (ch / TEX_COLS) * GLYPH;

        vertices[vertexCount].u = (s16)u;
        vertices[vertexCount].v = (s16)v;
        vertices[vertexCount].x = (s16)x;
        vertices[vertexCount].y = (s16)y;
        vertices[vertexCount].z = 0;
        vertexCount++;

        vertices[vertexCount].u = (s16)(u + GLYPH);
        vertices[vertexCount].v = (s16)(v + GLYPH);
        vertices[vertexCount].x = (s16)(x + GLYPH);
        vertices[vertexCount].y = (s16)(y + GLYPH);
        vertices[vertexCount].z = 0;
        vertexCount++;
    }
}

void geEndText(void) {
    if (vertexCount > 0) {
        sceKernelDcacheWritebackRange(vertices,
                                      (u32)(vertexCount * (int)sizeof(GeVertex)));
    }
}

void geSubmit(const BlitTarget *target) {
    u32 *list;
    u32 base;
    int result;
    int j;

    if (!ready || vertexCount == 0 || !blit_target_valid(target)) {
        return;
    }

    list = lists[currentList];
    currentList = (currentList + 1) % LIST_COUNT;

    base = gePhys(target->base);

    list[slotPixelFormat] = command(CMD_FRAMEBUF_PIX_FORMAT, (u32)target->format);
    list[slotFrameBufPtr] = command(CMD_FRAME_BUF_PTR, base);
    list[slotFrameBufWidth] =
        command(CMD_FRAME_BUF_WIDTH, ((base & 0xff000000) >> 8) | (u32)target->stride);
    list[slotPrim] = command(CMD_PRIM, (GU_SPRITES << 16) | (u32)vertexCount);

    /* Read after the game's frame is finished, so these are the values it left
     * behind and expects to still be there. A register argument is 24 bits wide,
     * so a wider result is not a value: leaving our own state behind is bad, but
     * writing back a number that is not state would be worse. */
    for (j = 0; j < RESTORE_COUNT; j++) {
        u32 value = sceGeGetCmd((int)restoreCmds[j]);

        list[slotEpilogue + j] = (value > 0xffffff)
                                     ? command(CMD_NOP, 0)
                                     : command(restoreCmds[j], value);
    }

    sceKernelDcacheWritebackRange(list, (u32)(listWords * (int)sizeof(u32)));

    /* No stall address and no callback: the list runs to its END on its own, and
     * nothing here waits for it. The result is checked, because a queue that
     * stops accepting lists looks exactly like a HUD that switched itself off. */
    result = sceGeListEnQueue(list, NULL, callbackId, NULL);

    if (result < 0) {
        lastError = (u32)result;
        if (++failures >= MAX_FAILURES) {
            ready = 0;
        }
        return;
    }

    failures = 0;
}

int geFailed(void) {
    return lastError != 0;
}

u32 geLastError(void) {
    return lastError;
}
