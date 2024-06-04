/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * scr_printf.c - Debug screen functions.
 *
 * Copyright (c) 2005 Marcus R. Brown <mrbrown@ocgnet.org>
 * Copyright (c) 2005 James Forshaw <tyranid@gmail.com>
 * Copyright (c) 2005 John Kelley <ps2dev@kelley.ca>
 *
 * $Id: scr_printf.c 2017 2006-10-07 16:51:57Z tyranid $
 *
 * This file is modified from the scr_printf.c in the sdk so init can be set 
 * directly (because pspDebugScreenInitEx automatically clears the screen)
 * other than that, all of the functions in here except blit_string come
 * from that file unchanged
 */


#include <pspdisplay.h>
#include <pspkernel.h>
#include <string.h>

#define PSP_SCREEN_WIDTH 480
#define PSP_SCREEN_HEIGHT 272
#define PSP_LINE_SIZE 512

static int X = 0, Y = 0;
static int MX=68, MY=34;
static u32 bg_col = 0, fg_col = 0xFFFFFFFF;
static void* g_vram_base = (u32 *) 0x04000000;
static int g_vram_offset = 0;
static int g_vram_mode = PSP_DISPLAY_PIXEL_FORMAT_8888;
static int init = 0;

extern u8 msx[];

static u16 convert_8888_to_565(u32 color);
static u16 convert_8888_to_5551(u32 color);
static u16 convert_8888_to_4444(u32 color);
void pspDebugScreenSetBackColor(u32 colour);
void pspDebugScreenSetTextColor(u32 colour);
void pspDebugScreenSetColorMode(int mode);
int pspDebugScreenGetX();
int pspDebugScreenGetY();
void pspDebugScreenSetXY(int x, int y);
void pspDebugScreenSetOffset(int offset);
void pspDebugScreenSetBase(u32* base);
static void debug_put_char_32(int x, int y, u32 color, u32 bgc, u8 ch);
static void debug_put_char_16(int x, int y, u16 color, u16 bgc, u8 ch);
void pspDebugScreenPutChar(int x, int y, u32 color, u8 ch);
void  _pspDebugScreenClearLine(int Y);
int pspDebugScreenPrintData(const char *buff, int size);

/////////////////////////////////////////////////////////////////////////////
// blit text
/////////////////////////////////////////////////////////////////////////////
int blit_string(int sx,int sy,const char *msg,int fg_col,int bg_col)
{
    int pwidth, pheight, bufferwidth, pixelformat, unk;
    unsigned int* vram32;
    int size;
    sceDisplayGetMode(&unk, &pwidth, &pheight);
    sceDisplayGetFrameBuf((void*)&vram32, &bufferwidth, &pixelformat, unk);

    if((bufferwidth == 0) || (vram32 == 0))
        return 0;

    init = 1;
    pspDebugScreenSetColorMode(pixelformat);
    pspDebugScreenSetOffset(0);
    pspDebugScreenSetBase((void*)vram32);

    pspDebugScreenSetBackColor(bg_col);
    pspDebugScreenSetTextColor(fg_col);
    pspDebugScreenSetXY(sx, sy);
    size = strlen(msg);
    return pspDebugScreenPrintData(msg, (size >= MX) ? (MX-1) : size); //restrict to the maximum x value
}
int blit_string_ctr(int sy,const char *msg,int fg_col,int bg_col)
{
    int sx = 480/2-strlen(msg);
    return blit_string(sx,sy,msg,fg_col,bg_col);
}

static u16 convert_8888_to_565(u32 color)
{
    int r, g, b;

    b = (color >> 19) & 0x1F;
    g = (color >> 10) & 0x3F;
    r = (color >> 3) & 0x1F;

    return r | (g << 5) | (b << 11);
}

static u16 convert_8888_to_5551(u32 color)
{
    int r, g, b, a;

    a = (color >> 24) ? 0x8000 : 0;
    b = (color >> 19) & 0x1F;
    g = (color >> 11) & 0x1F;
    r = (color >> 3) & 0x1F;

    return a | r | (g << 5) | (b << 10);
}

static u16 convert_8888_to_4444(u32 color)
{
    int r, g, b, a;

    a = (color >> 28) & 0xF; 
    b = (color >> 20) & 0xF;
    g = (color >> 12) & 0xF;
    r = (color >> 4) & 0xF;

    return (a << 12) | r | (g << 4) | (b << 8);
}

void pspDebugScreenSetBackColor(u32 colour)
{
   bg_col = colour;
}

void pspDebugScreenSetTextColor(u32 colour)
{
   fg_col = colour;
}

void pspDebugScreenSetColorMode(int mode)
{
    switch(mode)
    {
        case PSP_DISPLAY_PIXEL_FORMAT_565:
        case PSP_DISPLAY_PIXEL_FORMAT_5551:
        case PSP_DISPLAY_PIXEL_FORMAT_4444:
        case PSP_DISPLAY_PIXEL_FORMAT_8888:
            break;
        default: mode = PSP_DISPLAY_PIXEL_FORMAT_8888;
    };

    g_vram_mode = mode;
}


int pspDebugScreenGetX()
{
    return X;
}

int pspDebugScreenGetY()
{
    return Y;
}

void pspDebugScreenSetXY(int x, int y)
{
    if( x<MX && x>=0 ) X=x;
    if( y<MY && y>=0 ) Y=y;
}

void pspDebugScreenSetOffset(int offset)
{
    g_vram_offset = offset;
}

void pspDebugScreenSetBase(u32* base)
{
    g_vram_base = base;
}

static void debug_put_char_32(int x, int y, u32 color, u32 bgc, u8 ch)
{
   int  i,j, l;
   u8   *font;
   u32  pixel;
   u32 *vram_ptr;
   u32 *vram;

   if(!init)
   {
       return;
   }

   vram = g_vram_base;
   vram += (g_vram_offset >> 2) + x;
   vram += (y * PSP_LINE_SIZE);
   
   font = &msx[ (int)ch * 8];
   for (i=l=0; i < 8; i++, l+= 8, font++)
   {
      vram_ptr  = vram;
      for (j=0; j < 8; j++)
    {
          if ((*font & (128 >> j)))
              pixel = color;
          else
              pixel = bgc;

          *vram_ptr++ = pixel; 
    }
      vram += PSP_LINE_SIZE;
   }
}

static void debug_put_char_16(int x, int y, u16 color, u16 bgc, u8 ch)
{
   int  i,j, l;
   u8   *font;
   u16  pixel;
   u16 *vram_ptr;
   u16 *vram;

   if(!init)
   {
       return;
   }

   vram = g_vram_base;
   vram += (g_vram_offset >> 1) + x;
   vram += (y * PSP_LINE_SIZE);
   
   font = &msx[ (int)ch * 8];
   for (i=l=0; i < 8; i++, l+= 8, font++)
   {
      vram_ptr  = vram;
      for (j=0; j < 8; j++)
    {
          if ((*font & (128 >> j)))
              pixel = color;
          else
              pixel = bgc;

          *vram_ptr++ = pixel; 
    }
      vram += PSP_LINE_SIZE;
   }
}

void pspDebugScreenPutChar( int x, int y, u32 color, u8 ch)
{
    if(g_vram_mode == PSP_DISPLAY_PIXEL_FORMAT_8888)
    {
        debug_put_char_32(x, y, color, bg_col, ch);
    }
    else
    {
        u16 c = 0;
        u16 b = 0;
        switch(g_vram_mode)
        {
            case PSP_DISPLAY_PIXEL_FORMAT_565: c = convert_8888_to_565(color);
                                               b = convert_8888_to_565(bg_col);
                                               break;
            case PSP_DISPLAY_PIXEL_FORMAT_5551: c = convert_8888_to_5551(color);
                                               b = convert_8888_to_5551(bg_col);
                                               break;
            case PSP_DISPLAY_PIXEL_FORMAT_4444: c = convert_8888_to_4444(color);
                                               b = convert_8888_to_4444(bg_col);
                                               break;
        };
        debug_put_char_16(x, y, c, b, ch);
    }
}


void  _pspDebugScreenClearLine( int Y)
{
   int i;
   for (i=0; i < MX; i++)
    pspDebugScreenPutChar( i*7 , Y * 8, bg_col, 219);
}

/* Print non-nul terminated strings */
int pspDebugScreenPrintData(const char *buff, int size)
{
    int i;
    int j;
    char c;

    if(!init)
    {
        return 0;
    }

    for (i = 0; i < size; i++)
    {
        c = buff[i];
        switch (c)
        {
            case '\n':
                        X = 0;
                        Y ++;
                        if (Y == MY)
                            Y = 0;
                        _pspDebugScreenClearLine(Y);
                        break;
            case '\t':
                        for (j = 0; j < 5; j++) {
                            pspDebugScreenPutChar( X*7 , Y * 8, fg_col, ' ');
                            X++;
                        }
                        break;
            default:
                        pspDebugScreenPutChar( X*7 , Y * 8, fg_col, c);
                        X++;
                        if (X == MX)
                        {
                            X = 0;
                            Y++;
                            if (Y == MY)
                                Y = 0;
                            _pspDebugScreenClearLine(Y);
                        }
        }
    }

    return i;
}
