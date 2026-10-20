/* PS/2 keyboard emulation.
 *
 * SDL key events become scan code set 2 make/break sequences (E0 prefix for
 * the extended keys, F0 for release), queued and delivered one byte per
 * ~1 ms of emulated time (a real keyboard needs ~1.1 ms per 11-bit frame).
 * CPLD2's PS2DATA is a single holding register, so delivery waits while
 * PS2RDY is still set.  --type queues the sequences for a whole string.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "hw.h"
#include "emu.h"
#include "io.h"
#include "ps2.h"

#define BYTE_INTERVAL (CPU_CLOCK_HZ / 1000)     /* 1 ms */

struct key { uint8_t code; uint8_t ext; };

/* SDL_Scancode -> set 2 make code. */
static struct key keymap[SDL_NUM_SCANCODES];
static int keymap_ready;

/* ASCII -> (scancode, shift) for --type, US layout. */
static struct { uint8_t sc; uint8_t shift; } ascii_map[128];

static void map(int sc, int code, int ext)
{
    keymap[sc].code = (uint8_t)code;
    keymap[sc].ext = (uint8_t)ext;
}

static void ascii(int c, int sc, int shift)
{
    ascii_map[c].sc = (uint8_t)sc;
    ascii_map[c].shift = (uint8_t)shift;
}

static void build_maps(void)
{
    static const int letters[26] = {
        0x1C, 0x32, 0x21, 0x23, 0x24, 0x2B, 0x34, 0x33, 0x43, 0x3B, 0x42, 0x4B, 0x3A,
        0x31, 0x44, 0x4D, 0x15, 0x2D, 0x1B, 0x2C, 0x3C, 0x2A, 0x1D, 0x22, 0x35, 0x1A,
    };
    static const int digits[10] = { 0x45, 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46 };
    static const int fkeys[12] = { 0x05, 0x06, 0x04, 0x0C, 0x03, 0x0B, 0x83, 0x0A, 0x01, 0x09, 0x78, 0x07 };
    static const char shifted_digits[] = ")!@#$%^&*(";

    for (int i = 0; i < 26; i++) {
        map(SDL_SCANCODE_A + i, letters[i], 0);
        ascii('a' + i, SDL_SCANCODE_A + i, 0);
        ascii('A' + i, SDL_SCANCODE_A + i, 1);
    }
    for (int i = 0; i < 10; i++) {
        int sc = (i == 0) ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + i - 1;
        map(sc, digits[i], 0);
        ascii('0' + i, sc, 0);
        ascii(shifted_digits[i], sc, 1);
    }
    for (int i = 0; i < 12; i++)
        map(SDL_SCANCODE_F1 + i, fkeys[i], 0);

    struct { int sc, code, ext; int plain, shift; } punct[] = {
        { SDL_SCANCODE_GRAVE,        0x0E, 0, '`', '~' },
        { SDL_SCANCODE_MINUS,        0x4E, 0, '-', '_' },
        { SDL_SCANCODE_EQUALS,       0x55, 0, '=', '+' },
        { SDL_SCANCODE_BACKSLASH,    0x5D, 0, '\\', '|' },
        { SDL_SCANCODE_BACKSPACE,    0x66, 0, '\b', 0 },
        { SDL_SCANCODE_SPACE,        0x29, 0, ' ', 0 },
        { SDL_SCANCODE_TAB,          0x0D, 0, '\t', 0 },
        { SDL_SCANCODE_CAPSLOCK,     0x58, 0, 0, 0 },
        { SDL_SCANCODE_LSHIFT,       0x12, 0, 0, 0 },
        { SDL_SCANCODE_LCTRL,        0x14, 0, 0, 0 },
        { SDL_SCANCODE_LGUI,         0x1F, 1, 0, 0 },
        { SDL_SCANCODE_LALT,         0x11, 0, 0, 0 },
        { SDL_SCANCODE_RSHIFT,       0x59, 0, 0, 0 },
        { SDL_SCANCODE_RCTRL,        0x14, 1, 0, 0 },
        { SDL_SCANCODE_RGUI,         0x27, 1, 0, 0 },
        { SDL_SCANCODE_RALT,         0x11, 1, 0, 0 },
        { SDL_SCANCODE_APPLICATION,  0x2F, 1, 0, 0 },
        { SDL_SCANCODE_RETURN,       0x5A, 0, '\n', 0 },
        { SDL_SCANCODE_ESCAPE,       0x76, 0, 27, 0 },
        { SDL_SCANCODE_SCROLLLOCK,   0x7E, 0, 0, 0 },
        { SDL_SCANCODE_LEFTBRACKET,  0x54, 0, '[', '{' },
        { SDL_SCANCODE_RIGHTBRACKET, 0x5B, 0, ']', '}' },
        { SDL_SCANCODE_SEMICOLON,    0x4C, 0, ';', ':' },
        { SDL_SCANCODE_APOSTROPHE,   0x52, 0, '\'', '"' },
        { SDL_SCANCODE_COMMA,        0x41, 0, ',', '<' },
        { SDL_SCANCODE_PERIOD,       0x49, 0, '.', '>' },
        { SDL_SCANCODE_SLASH,        0x4A, 0, '/', '?' },
        { SDL_SCANCODE_INSERT,       0x70, 1, 0, 0 },
        { SDL_SCANCODE_HOME,         0x6C, 1, 0, 0 },
        { SDL_SCANCODE_PAGEUP,       0x7D, 1, 0, 0 },
        { SDL_SCANCODE_DELETE,       0x71, 1, 127, 0 },
        { SDL_SCANCODE_END,          0x69, 1, 0, 0 },
        { SDL_SCANCODE_PAGEDOWN,     0x7A, 1, 0, 0 },
        { SDL_SCANCODE_UP,           0x75, 1, 0, 0 },
        { SDL_SCANCODE_LEFT,         0x6B, 1, 0, 0 },
        { SDL_SCANCODE_DOWN,         0x72, 1, 0, 0 },
        { SDL_SCANCODE_RIGHT,        0x74, 1, 0, 0 },
        { SDL_SCANCODE_NUMLOCKCLEAR, 0x77, 0, 0, 0 },
        { SDL_SCANCODE_KP_DIVIDE,    0x4A, 1, 0, 0 },
        { SDL_SCANCODE_KP_MULTIPLY,  0x7C, 0, 0, 0 },
        { SDL_SCANCODE_KP_MINUS,     0x7B, 0, 0, 0 },
        { SDL_SCANCODE_KP_PLUS,      0x79, 0, 0, 0 },
        { SDL_SCANCODE_KP_ENTER,     0x5A, 1, 0, 0 },
        { SDL_SCANCODE_KP_PERIOD,    0x71, 0, 0, 0 },
        { SDL_SCANCODE_KP_0,         0x70, 0, 0, 0 },
        { SDL_SCANCODE_KP_1,         0x69, 0, 0, 0 },
        { SDL_SCANCODE_KP_2,         0x72, 0, 0, 0 },
        { SDL_SCANCODE_KP_3,         0x7A, 0, 0, 0 },
        { SDL_SCANCODE_KP_4,         0x6B, 0, 0, 0 },
        { SDL_SCANCODE_KP_5,         0x73, 0, 0, 0 },
        { SDL_SCANCODE_KP_6,         0x74, 0, 0, 0 },
        { SDL_SCANCODE_KP_7,         0x6C, 0, 0, 0 },
        { SDL_SCANCODE_KP_8,         0x75, 0, 0, 0 },
        { SDL_SCANCODE_KP_9,         0x7D, 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof punct / sizeof punct[0]; i++) {
        map(punct[i].sc, punct[i].code, punct[i].ext);
        if (punct[i].plain) ascii(punct[i].plain, punct[i].sc, 0);
        if (punct[i].shift) ascii(punct[i].shift, punct[i].sc, 1);
    }
    ascii('\r', SDL_SCANCODE_RETURN, 0);
    keymap_ready = 1;
}

/* ---- byte queue ------------------------------------------------------------- */

static uint8_t *queue;
static size_t q_len, q_cap, q_head;
static uint64_t next_deliver;

static void push(uint8_t b)
{
    if (q_len == q_cap) {
        if (q_head) {
            memmove(queue, queue + q_head, q_len - q_head);
            q_len -= q_head;
            q_head = 0;
        }
        if (q_len == q_cap) {
            q_cap = q_cap ? q_cap * 2 : 256;
            queue = realloc(queue, q_cap);
            if (!queue) {
                perror("realloc");
                exit(1);
            }
        }
    }
    queue[q_len++] = b;
}

static void push_key(struct key k, int down)
{
    if (k.ext)
        push(0xE0);
    if (!down)
        push(0xF0);
    push(k.code);
}

void ps2_reset(void)
{
    if (!keymap_ready)
        build_maps();
    q_len = q_head = 0;
    next_deliver = 0;
}

void ps2_key(int sdl_scancode, int down)
{
    if (!keymap_ready)
        build_maps();
    if (sdl_scancode < 0 || sdl_scancode >= SDL_NUM_SCANCODES)
        return;
    if (sdl_scancode == SDL_SCANCODE_PAUSE) {
        /* The Pause key has its own 8-byte make and no break code; it is the
         * NMI button here anyway, so send nothing. */
        return;
    }
    struct key k = keymap[sdl_scancode];
    if (k.code == 0)
        return;
    push_key(k, down);
}

void ps2_tick(uint64_t now)
{
    if (q_head == q_len) {
        q_head = q_len = 0;
        return;
    }
    if (now < next_deliver || io_ps2_ready())
        return;
    io_ps2_deliver(queue[q_head++], 0);
    next_deliver = now + BYTE_INTERVAL;
}

void ps2_type_text(const char *text)
{
    if (!keymap_ready)
        build_maps();
    struct key shift = keymap[SDL_SCANCODE_LSHIFT];
    for (const char *p = text; *p; p++) {
        int c = (unsigned char)*p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'e': c = 27; break;
            case 'b': c = '\b'; break;
            case '\\': c = '\\'; break;
            default: c = (unsigned char)*p; break;
            }
        }
        if (c >= 128 || ascii_map[c].sc == 0)
            continue;
        struct key k = keymap[ascii_map[c].sc];
        if (ascii_map[c].shift)
            push_key(shift, 1);
        push_key(k, 1);
        push_key(k, 0);
        if (ascii_map[c].shift)
            push_key(shift, 0);
    }
}

int ps2_queue_empty(void)
{
    return q_head == q_len;
}
