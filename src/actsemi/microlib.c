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
//#define DEBUG_VERBOSE

#ifdef AS_DEBUG
#define DBG(x...) fprintf(stderr, x)
#else
#define DBG(x...)
#endif

#define VERSION "R1.02"

#include "microlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <actsemi.h>

int exit_xp_thread = 0;
int pause_xp_thread = 0;

void main_shutdown(void);

/* Called periodically to check if the emulation thread should pause
   or quit. */
void check_thread(void)
{
  if (exit_xp_thread) {
    main_shutdown();
    pthread_exit(NULL);
  }
  while (pause_xp_thread) {
    usleep(50000);
    if (exit_xp_thread) {
      main_shutdown();
      pthread_exit(NULL);
    }
  }
}

FILE* stderr;

/* This is the screen xpectrum paints to. */
unsigned char *video_screen8 = NULL;
/* The system and emulation threads are not synchronized, so we copy the
   fully rendered screen to a back buffer until the system thread paints it
   to the screen.  */
unsigned char *video_screen8_back = NULL;

/* System screen frame buffer. */
uint16_t *screen_fb;

static int microlib_inited = 0;

uint16_t _palette[256];
#define MAKE_RGB565(r, g, b) (((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3))
void set_palette(palette_t palette){
    int i;
    for (i = 0; i < 256; i++) {
        _palette[i] = MAKE_RGB565(palette[i].r, palette[i].g, palette[i].b);
    }
}

unsigned long getTicks(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec / 1000 + tv.tv_sec * 1000;
}

void microlib_usleep(int time)
{
    check_thread();
    usleep(time);
}

void sound_volume(int left, int rigth)
{
    /* XXX: unimplemented */
}

int sound_initialized = 0;
int system_sound_enabled = 1;
void *libsound_handle = 0;
ao_handle_t ao_handle = 0;

int sound_open(int rate, int bits, int stereo){
    if (!sound_initialized && libsound_handle) {
        ao_handle = mmm_ao_open();
        if (!ao_handle) {
            DBG("failed to get AO handle\n");
            return 0;
        }
        mmm_ao_params_t ap;
        ap.channels = 2;
        ap.rate = 44100;
        if (mmm_ao_cmd(ao_handle, MMM_AO_SET_PARAMS, &ap) < 0) {
            DBG("MMM_AO_SET_PARAMS failed\n");
        }
        if (mmm_ao_cmd(ao_handle, MMM_AO_SET_BUFFER_SIZE, (void *)0x800) < 0) {
            DBG("MMM_AO_SET_BUFFER_SIZE failed\n");
        }
        /* Not entirely sure what this does, but using lower values results
           in clicks once in a while. */
        if (mmm_ao_cmd(ao_handle, MMM_AO_SET_FRAMES, (void *)4) < 0) {
            DBG("MMM_AO_SET_FRAMES failed\n");
        }
        if (mmm_ao_cmd(ao_handle, MMM_AO_PLAY, 0) < 0) {
            DBG("MMM_AO_PLAY failed\n");
        }
        sound_initialized = 1;
    }
    return 0;
}

int sound_close(){
    if (sound_initialized) {
        if (ao_handle)
          mmm_ao_close(ao_handle);
        sound_initialized = 0;
    }
    return 0;
}

int sound_send(void *samples,int nsamples)
{
    if (sound_initialized) {
        int32_t *left = malloc(nsamples / 2 * 4);
        int32_t *right = malloc(nsamples / 2 * 4);
        int16_t *in = (int16_t *)samples;
        int i;
        for (i = 0; i < nsamples / 2; i++) {
            left[i] = in[i * 2];
            right[i] = in[i * 2 + 1];
        }
        mmm_ao_data_t ad;
        ad.buf_left = left;
        ad.buf_right = right;
        ad.channels = 2;
        ad.buf_size = nsamples / 2;
        mmm_ao_data(ao_handle, &ad);
        free(left);
        free(right);
    }
    return 0;
}

void microlib_end(void)
{
    if ( microlib_inited )
    {
        sound_close();
        free(video_screen8);
        free(video_screen8_back);
        if (libsound_handle)
            dlclose(libsound_handle);
        microlib_inited = 0;
    }
}

void microlib_init()
{
    if ( !microlib_inited )
    {
        libsound_handle = dlopen("libsound.so", 1);
        if (!libsound_handle) {
            DBG("failed to dlopen libsound.so\n");
        }
        video_screen8 = malloc( 320 * 240 );
        video_screen8_back = malloc(320 * 240);

        microlib_inited = 1;
    }
}

int redraw = 0;
void dump_video()
{
#ifdef DEBUG_VERBOSE
    DBG("dump_video to 0x%x\n", (unsigned int)screen_fb);
#endif
    /* Back up Spectrum screen; we don't know when the system thread will
       paint this, so we make a copy to prevent flickering. */
    memcpy(video_screen8_back, video_screen8, 320 * 240);
    redraw = 1;	/* Tell system we have something to paint. */
    check_thread();
}

/* Paint the back buffer to the system screen. */
static void sync_screen(void)
{
#ifdef DEBUG_VERBOSE
    DBG("sync_screen at %d\n", getTicks());
#endif
    int x, y;
    for (y = 0; y < 240; y++) {
      for (x = 0; x < 320; x++) {
        screen_fb[y * 320 + x] = _palette[video_screen8_back[y * 320 + x]];
      }
    }
}

emu_keys_t keys;
long joystick_read()
{
    long button = 0;
    if (keys.p1[EMU_KEY_LEFT])	button |= JOY_BUTTON_LEFT;
    if (keys.p1[EMU_KEY_RIGHT])	button |= JOY_BUTTON_RIGHT;
    if (keys.p1[EMU_KEY_UP])	button |= JOY_BUTTON_UP;
    if (keys.p1[EMU_KEY_DOWN])	button |= JOY_BUTTON_DOWN;

    if (keys.p1[EMU_KEY_START])	button |= JOY_BUTTON_MENU;
    if (keys.p1[EMU_KEY_SELECT])	button |= JOY_BUTTON_SELECT;

    if (keys.p1[EMU_KEY_SQUARE])	button |= JOY_BUTTON_A;
    if (keys.p1[EMU_KEY_CROSS])	button |= JOY_BUTTON_X;
    if (keys.p1[EMU_KEY_CIRCLE])	button |= JOY_BUTTON_B;
    if (keys.p1[EMU_KEY_TRIANGLE])	button |= JOY_BUTTON_Y;
    if (keys.p1[EMU_KEY_L])	button |= JOY_BUTTON_L;
    if (keys.p1[EMU_KEY_R])	button |= JOY_BUTTON_R;
    return button;
}

struct rect {
    uint32_t w;
    uint32_t h;
};
struct rect screen_size;

struct cmd_22 {
    uint32_t key_code_mode;
    uint32_t key_code[0x10];
    uint32_t key_file_len;
};

int cmd_22(struct cmd_22 *data)
{
    DBG("cmd_22 mode %u len %u\n", (unsigned int)data->key_code_mode, (unsigned int)data->key_file_len);
    unsigned int i;
    for (i = 0; i < data->key_file_len / 4; i++) {
        DBG("key code %u %08X\n", i, (unsigned int)data->key_code[i]);
    }
    return 0;
}

int max_screen_dimensions(struct rect *r)
{
    DBG("screen size %d/%d\n", (int)r->w, (int)r->h);
    screen_size = *r;
    return 0;
}

struct rect_form {
    uint32_t w;
    uint32_t h;
    uint32_t format;
};

int get_our_dimensions(struct rect_form *rf)
{
    rf->w = 320;
    rf->h = 240;
    rf->format = 1;
    return 0;
}

void enqueue_key(emu_keys_t *input)
{
    keys = *input;
#ifdef DEBUG_VERBOSE
    int i;
    int have_keys = 0;
    for (i = 0; i < 0x16; i++) {
        if (input->p1[i] || input->p2[i]) {
            have_keys = 1;
            break;
        }
    }
    if (!have_keys)
        return;
    DBG("keys");
    for (i = 0; i < 0x16; i++) {
        DBG("|%2d %u,%u", i, input->p1[i], input->p2[i]);
    }
    DBG("\n");
#endif
}

pthread_t xp_thread;
extern void *xpectrum_main(void *);
int control(int cmd, void *data)
{
  int ret;
#ifdef DEBUG_VERBOSE
  DBG("cmd %d data 0x%x\n", cmd, (unsigned int)data);
#endif
  
  switch (cmd) {
    case 0:	/* init */
      microlib_init();
      pthread_create(&xp_thread, NULL, xpectrum_main, NULL);
      return 0;
    case 1:	/* reset */
      DBG("reset!\n");
      return 0;
    case 2:	/* game name? */
      DBG("name %s\n", (char *)data);
      return 0;
    case 3:
      DBG("shutdown\n");
      exit_xp_thread = 1;
      pthread_join(xp_thread, NULL);
#ifdef AS_DEBUG
      fclose(stderr);
#endif
      return 0;
    case 4:
      DBG("another name %s\n", (char *)data);
      return 0;
    case 7:	/* sound enable */
      system_sound_enabled = *((int *)data);
      DBG("sound enable %d\n", system_sound_enabled);
      return 0;
    case 10:	/* pause */
      DBG("pause\n");
      pause_xp_thread = 1;
      return 0;
    case 12:	/* resume */
      DBG("resume\n");
      pause_xp_thread = 0;
      return 0;
    case 13:	/* get our dimensions, format */
      return get_our_dimensions((struct rect_form *)data);
    case 14:
#ifdef DEBUG_VERBOSE
      DBG("framebuffer addr 0x%x\n", *((unsigned int *)data));
#endif
      screen_fb = *((uint16_t **)data);
      return 0;
    case 15:
      return max_screen_dimensions((struct rect*)data);
    case 16:	/* screen mode */
      DBG("screen mode %d\n", *((int *)data));
      return 0;
    case 17:	/* get redraw flag */
      sync_screen();
#ifdef DEBUG_VERBOSE
      DBG("redraw?\n");
#endif
      ret = redraw;
      redraw = 0;
      return ret;
    case 18:	/* input mode */
      DBG("input mode %d\n", *((int *)data));
      return 0;
    case 22:
      return cmd_22((struct cmd_22 *)data);
    default:
      DBG("unknown command %d, data 0x%x\n", cmd, (unsigned int)data);
      return 0;
  }
}

int dummy_main_loop(void)
{
#ifdef DEBUG_VERBOSE
  DBG("main loop at %d\n", getTicks());
#endif
  /* Emulator framework calls this permanently, so we have to sleep a bit
     so the xpectrum thread will not starve. */
  usleep(20000);
  return 0;
}

void *p1apitbl[] = {
  dummy_main_loop,
  enqueue_key,
  control
};

void  __attribute__ ((section (".init"))) _init_proc(void)
{
#ifdef AS_DEBUG
  stderr = fopen("/mnt/disk0/xpectrum.txt", "w");
  setbuf(stderr, NULL);
  DBG("xpectrum starting\n");
#endif
  api_install(7, p1apitbl);
}

void __attribute__ ((section (".fini"))) _term_proc(void)
{
}

void abort(void)
{
    pthread_exit((void *)-1);
}

int ferror(FILE *fp)
{
    return 0;
}

int fflush(FILE *fp)
{
    return 0;
}

uint32_t general_plugin_info = 0;
uint32_t *get_plugin_info(void)
{
  return &general_plugin_info;
}

const char *P1_SO_VERSION = VERSION;

asm(
".section .dlsym,\"a\"\n"
".word P1_SO_VERSION\n"
".word _dlstr_P1_SO_VERSION\n"
".word get_plugin_info\n"
".word _dlstr_get_plugin_info\n"
".word p1apitbl\n"
".word _dlstr_api_table\n"
".section .dlstr,\"a\"\n"
"_dlstr_P1_SO_VERSION:\n"
".string \"P1_SO_VERSION\\0\"\n"
"_dlstr_get_plugin_info:\n"
".string \"get_plugin_info\\0\"\n"
"_dlstr_api_table:\n"
".string \"api_table\\0\"\n"
);

