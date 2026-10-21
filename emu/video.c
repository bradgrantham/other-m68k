/* Video timing (CPLD1 counters), framebuffer rendering and the SDL window.
 *
 * Each scanline slice starts at the HSYNC leading edge, where CPLD1's V
 * counter advances.  Within the slice (CPU clocks from its start):
 *   [0, h_sync)                          HSYNC high
 *   [h_sync, h_sync + h_back)            back porch
 *   [.., + h_active)                     active video (DE high on lines < v_active)
 *   [.., + h_front) = line_clocks        front porch
 * Lines 0..v_active-1 are active, then v_bottom blank lines, v_sync lines of
 * VSYNC (the VBLANK flag sets at the first), then v_top blank lines.
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "hw.h"
#include "emu.h"
#include "bus.h"
#include "io.h"
#include "video.h"

static const struct video_timing ntsc = {
    "NTSC", VIDEO_NTSC_LINE_CLOCKS,
    VIDEO_NTSC_H_ACTIVE, VIDEO_NTSC_H_FRONT, VIDEO_NTSC_H_SYNC, VIDEO_NTSC_H_BACK,
    VIDEO_NTSC_LINES,
    VIDEO_NTSC_V_ACTIVE, VIDEO_NTSC_V_BOTTOM, VIDEO_NTSC_V_SYNC, VIDEO_NTSC_V_TOP,
    (double)CPU_CLOCK_HZ / (double)VIDEO_NTSC_FRAME_CLOCKS,
};

static const struct video_timing pal = {
    "PAL", VIDEO_PAL_LINE_CLOCKS,
    VIDEO_PAL_H_ACTIVE, VIDEO_PAL_H_FRONT, VIDEO_PAL_H_SYNC, VIDEO_PAL_H_BACK,
    VIDEO_PAL_LINES,
    VIDEO_PAL_V_ACTIVE, VIDEO_PAL_V_BOTTOM, VIDEO_PAL_V_SYNC, VIDEO_PAL_V_TOP,
    (double)CPU_CLOCK_HZ / (double)VIDEO_PAL_FRAME_CLOCKS,
};

static const struct video_timing *cur = &ntsc;
static int cur_page;
static int cur_line;
static uint64_t line_start_cycle;

static uint32_t pixels[VIDEO_WIDTH * VIDEO_HEIGHT];
static uint32_t palette[256];

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int have_window;
static uint16_t title_leds = 0xFFFF;
static const struct video_timing *title_std;

static void build_palette(void)
{
    for (int i = 0; i < 256; i++) {
        unsigned r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        r = (r * 255 + 3) / 7;
        g = (g * 255 + 3) / 7;
        b = (b * 255 + 1) / 3;
        palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

int video_init(int scale, int headless)
{
    build_palette();
    if (headless)
        return 0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "m68k-emu: SDL video init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow("m68k-emu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              VIDEO_WIDTH * scale, VIDEO_HEIGHT * scale, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "m68k-emu: SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "m68k-emu: SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(renderer, VIDEO_WIDTH, VIDEO_HEIGHT);
    SDL_RenderSetIntegerScale(renderer, SDL_FALSE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                VIDEO_WIDTH, VIDEO_HEIGHT);
    if (!texture) {
        fprintf(stderr, "m68k-emu: SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    have_window = 1;
    return 0;
}

void video_shutdown(void)
{
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    have_window = 0;
}

void video_reset(void)
{
    cur = &ntsc;
    cur_page = 0;
    cur_line = 0;
}

const struct video_timing *video_timing(void)
{
    return cur;
}

int video_page(void)
{
    return cur_page;
}

void video_line_start(int v, uint64_t cycle)
{
    if (v == 0) {
        /* VA reload and timing table swap happen at the start of the frame. */
        cur_page = io_pending_page();
        cur = io_pending_pal() ? &pal : &ntsc;
    }
    cur_line = v;
    line_start_cycle = cycle;
    io_pdm_line_start(cycle);
    uint16_t f = INTSTAT_HSYNC;
    if (v == cur->v_active + cur->v_bottom)
        f |= INTSTAT_VBLANK;        /* leading edge of VSYNC */
    io_set_flag(f);
}

uint16_t video_live_pins(void)
{
    uint64_t now = emu_cycles();
    int pos = (int)(now - line_start_cycle);
    if (pos < 0) pos = 0;
    if (pos >= cur->line_clocks) pos = cur->line_clocks - 1;
    uint16_t v = 0;
    if (pos < cur->h_sync)
        v |= INTSTAT_HSYNCPIN;
    int act0 = cur->h_sync + cur->h_back;
    if (cur_line < cur->v_active && pos >= act0 && pos < act0 + cur->h_active)
        v |= INTSTAT_DEPIN;
    int vs0 = cur->v_active + cur->v_bottom;
    if (cur_line >= vs0 && cur_line < vs0 + cur->v_sync)
        v |= INTSTAT_VSYNCPIN;
    return v;
}

void video_render(void)
{
    uint32_t base = cur_page ? FB_PAGE1_ADDR : FB_PAGE0_ADDR;
    const uint8_t *src = &bus_ram[base - RAM_BASE];
    uint32_t *dst = pixels;
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++)
        dst[i] = palette[src[i]];
}

const uint32_t *video_pixels(void)
{
    return pixels;
}

static void update_title(void)
{
    uint16_t g = io_gpio() & (GPIO_LED0 | GPIO_LED1);
    if (g == title_leds && cur == title_std)
        return;
    title_leds = g;
    title_std = cur;
    char title[128];
    snprintf(title, sizeof title, "m68k-emu  %s  LED0:%s LED1:%s%s", cur->name,
             (g & GPIO_LED0) ? "ON " : "off", (g & GPIO_LED1) ? "ON " : "off",
             opts.fast ? "  [fast]" : "");
    SDL_SetWindowTitle(window, title);
}

int video_present(void)
{
    if (!have_window)
        return -1;
    SDL_UpdateTexture(texture, NULL, pixels, VIDEO_WIDTH * sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    update_title();
    return 0;
}
