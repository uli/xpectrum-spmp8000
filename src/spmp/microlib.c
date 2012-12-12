/*
 * Copyright (C) 2012, Ulrich Hecht <ulrich.hecht@gmail.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "microlib.h"

#include <stdio.h>
#include <libgame.h>

char *video_screen8 = NULL;

static int microlib_inited = 0;

uint16_t _palette[256];
void set_palette(palette_t palette){
    int i;
    for (i = 0; i < 256; i++) {
        _palette[i] = MAKE_RGB565(palette[i].r, palette[i].g, palette[i].b);
    }
}

unsigned long getTicks(){
    return libgame_utime() / 1000;
}

void microlib_usleep(int time)
{
    cyg_thread_delay(time / 10000);
}

emu_sound_params_t sp;

void sound_volume(int left, int rigth)
{
    /* XXX: unimplemented */
}

int sound_open(int rate, int bits, int stereo){
    if (bits != 16) {
#ifdef SPMP_ADBG
        adbg_printf("unsupported sound format\n");
#endif
        return -1;
    }
#ifndef SPMP_ADBG
    sp.rate = rate;
    sp.channels = stereo + 1;
    emuIfSoundInit(&sp);
#endif
    return 0;
}

int sound_close(){
#ifndef SPMP_ADBG
    emuIfSoundCleanup();
#endif
    return 0;
}

int sound_send(void *samples,int nsamples)
{
#ifndef SPMP_ADBG
    sp.buf = samples;
    sp.buf_size = nsamples * 2;
    emuIfSoundPlay(&sp);
#endif
    return 0;
}

emu_keymap_t keymap;

void microlib_end(void)
{
    if ( microlib_inited )
    {
        sound_close();
        emuIfGraphCleanup();
        emuIfKeyCleanup(&keymap);
        fcloseall();
        free(video_screen8);
        microlib_inited = 0;
    }
}

void __call_exitprocs(int, void *);
int my_exit(void)
{
    /* cannot call exit() here because we are running in an OS context */
    __call_exitprocs(0, NULL);
    return NativeGE_gameExit();
}

void microlib_init()
{
    if ( !microlib_inited )
    {
        video_screen8 = malloc( 320 * 240 );
        emu_graph_params_t gp;
        gp.pixels = (uint16_t *)video_screen8;
        gp.has_palette = 1;
        gp.palette = _palette;
        gp.width = 320;
        gp.height = 240;
        gp.src_clip_x = 0;
        gp.src_clip_y = 0;
        gp.src_clip_w = 320;
        gp.src_clip_h = 240;
        emuIfGraphInit(&gp);

        keymap.controller = 0;
        emuIfKeyInit(&keymap);
        if (libgame_load_buttons("xpectrum/xpectrum.map", &keymap) < 0) {
            map_buttons();
        }

        microlib_inited = 1;
        atexit(microlib_end);
        g_stEmuAPIs->exit = my_exit;
    }
}

void dump_video_nosync(void)
{
    void *sb = gDisplayDev->getShadowBuffer();
    gDisplayDev->setShadowBuffer(gDisplayDev->getFrameBuffer());
    emuIfGraphShow();
    gDisplayDev->setShadowBuffer(sb);
}

void dump_video()
{
    emuIfGraphShow();
}

long joystick_read()
{
    long button = 0;
    uint32_t keys = emuIfKeyGetInput(&keymap);
    if (keys & keymap.scancode[EMU_KEY_LEFT])	button |= JOY_BUTTON_LEFT;
    if (keys & keymap.scancode[EMU_KEY_RIGHT])	button |= JOY_BUTTON_RIGHT;
    if (keys & keymap.scancode[EMU_KEY_UP])	button |= JOY_BUTTON_UP;
    if (keys & keymap.scancode[EMU_KEY_DOWN])	button |= JOY_BUTTON_DOWN;

    if (keys & keymap.scancode[EMU_KEY_START])	button |= JOY_BUTTON_MENU;
    if (keys & keymap.scancode[EMU_KEY_SELECT])	button |= JOY_BUTTON_SELECT;

    if (keys & keymap.scancode[EMU_KEY_SQUARE])	button |= JOY_BUTTON_A;
    if (keys & keymap.scancode[EMU_KEY_X])	button |= JOY_BUTTON_X;
    if (keys & keymap.scancode[EMU_KEY_O])	button |= JOY_BUTTON_B;
    if (keys & keymap.scancode[EMU_KEY_TRIANGLE])	button |= JOY_BUTTON_Y;
    if (keys & keymap.scancode[EMU_KEY_L])	button |= JOY_BUTTON_L;
    if (keys & keymap.scancode[EMU_KEY_R])	button |= JOY_BUTTON_R;

    return button;
}

void map_buttons(void)
{
    libgame_buttonmap_t bm[] = {
        {"Menu", EMU_KEY_START},
        {"Keyboard", EMU_KEY_SELECT},	/* aka SELECT */
        {"OK", EMU_KEY_O},		/* aka B */
        {"Cancel", EMU_KEY_X},		/* aka X */
        {"Edit", EMU_KEY_SQUARE},	/* aka A */
        {"Delete", EMU_KEY_TRIANGLE},	/* aka Y */
        {"L", EMU_KEY_L},
        {"R", EMU_KEY_R},
        {0, 0}
    };
    libgame_map_buttons("xpectrum/xpectrum.map", &keymap, bm);
}
