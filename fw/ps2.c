/* PS/2 scan code set 2 -> ASCII / escape sequences, plus raw key events.
 *
 * Raw events (input_key_event_push): bits 7..0 scan code, bit 8 (KEY_RELEASE) break,
 * bit 9 (KEY_EXTENDED) E0 prefix. ASCII (input_kbd_push): printable keys with shift/caps,
 * Ctrl+letter -> 0x01..0x1A, Enter -> '\r', Backspace -> 0x08, Tab, Esc; cursor and
 * function keys as VT100/xterm sequences:
 *   Up/Down/Right/Left  ESC [ A / B / C / D      Home ESC [ H    End ESC [ F
 *   Ins/Del/PgUp/PgDn   ESC [ 2~ / 3~ / 5~ / 6~
 *   F1-F4  ESC O P/Q/R/S   F5-F12  ESC [ 15~ 17~ 18~ 19~ 20~ 21~ 23~ 24~
 */
#include "bios.h"

static struct {
    uint8_t release;    /* F0 seen */
    uint8_t ext;        /* E0 seen */
    uint8_t skip;       /* bytes to drop (E1 pause sequence) */
    uint8_t shift, ctrl, alt, caps;
} st;

/* index = scan code (set 2, non-E0), value = ASCII unshifted; 0 = not a character */
static const char map_plain[128] = {
    /* 00 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 08 */ 0,    0,    0,    0,    0,    '\t', '`',  0,
    /* 10 */ 0,    0,    0,    0,    0,    'q',  '1',  0,
    /* 18 */ 0,    0,    'z',  's',  'a',  'w',  '2',  0,
    /* 20 */ 0,    'c',  'x',  'd',  'e',  '4',  '3',  0,
    /* 28 */ 0,    ' ',  'v',  'f',  't',  'r',  '5',  0,
    /* 30 */ 0,    'n',  'b',  'h',  'g',  'y',  '6',  0,
    /* 38 */ 0,    0,    'm',  'j',  'u',  '7',  '8',  0,
    /* 40 */ 0,    ',',  'k',  'i',  'o',  '0',  '9',  0,
    /* 48 */ 0,    '.',  '/',  'l',  ';',  'p',  '-',  0,
    /* 50 */ 0,    0,    '\'', 0,    '[',  '=',  0,    0,
    /* 58 */ 0,    0,    '\r', ']',  0,    '\\', 0,    0,
    /* 60 */ 0,    0,    0,    0,    0,    0,    '\b', 0,
    /* 68 */ 0,    '1',  0,    '4',  '7',  0,    0,    0,
    /* 70 */ '0',  '.',  '2',  '5',  '6',  '8',  0x1B, 0,
    /* 78 */ 0,    '+',  '3',  '-',  '*',  '9',  0,    0,
};
static const char map_shift[128] = {
    /* 00 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 08 */ 0,    0,    0,    0,    0,    '\t', '~',  0,
    /* 10 */ 0,    0,    0,    0,    0,    'Q',  '!',  0,
    /* 18 */ 0,    0,    'Z',  'S',  'A',  'W',  '@',  0,
    /* 20 */ 0,    'C',  'X',  'D',  'E',  '$',  '#',  0,
    /* 28 */ 0,    ' ',  'V',  'F',  'T',  'R',  '%',  0,
    /* 30 */ 0,    'N',  'B',  'H',  'G',  'Y',  '^',  0,
    /* 38 */ 0,    0,    'M',  'J',  'U',  '&',  '*',  0,
    /* 40 */ 0,    '<',  'K',  'I',  'O',  ')',  '(',  0,
    /* 48 */ 0,    '>',  '?',  'L',  ':',  'P',  '_',  0,
    /* 50 */ 0,    0,    '"',  0,    '{',  '+',  0,    0,
    /* 58 */ 0,    0,    '\r', '}',  0,    '|',  0,    0,
    /* 60 */ 0,    0,    0,    0,    0,    0,    '\b', 0,
    /* 68 */ 0,    '1',  0,    '4',  '7',  0,    0,    0,
    /* 70 */ '0',  '.',  '2',  '5',  '6',  '8',  0x1B, 0,
    /* 78 */ 0,    '+',  '3',  '-',  '*',  '9',  0,    0,
};

/* function keys: scan code -> F number */
static int fkey(uint8_t sc)
{
    switch (sc) {
    case 0x05: return 1;  case 0x06: return 2;  case 0x04: return 3;  case 0x0C: return 4;
    case 0x03: return 5;  case 0x0B: return 6;  case 0x83: return 7;  case 0x0A: return 8;
    case 0x01: return 9;  case 0x09: return 10; case 0x78: return 11; case 0x07: return 12;
    default: return 0;
    }
}

static void push_str(const char *s)
{
    while (*s) input_kbd_push((uint8_t)*s++);
}

void ps2_init(void)
{
    memset(&st, 0, sizeof st);
}

static void ext_key(uint8_t sc)
{
    const char *seq = 0;
    switch (sc) {
    case 0x75: seq = "\033[A"; break;   /* up */
    case 0x72: seq = "\033[B"; break;   /* down */
    case 0x74: seq = "\033[C"; break;   /* right */
    case 0x6B: seq = "\033[D"; break;   /* left */
    case 0x6C: seq = "\033[H"; break;   /* home */
    case 0x69: seq = "\033[F"; break;   /* end */
    case 0x70: seq = "\033[2~"; break;  /* insert */
    case 0x71: seq = "\033[3~"; break;  /* delete */
    case 0x7D: seq = "\033[5~"; break;  /* page up */
    case 0x7A: seq = "\033[6~"; break;  /* page down */
    case 0x5A: input_kbd_push('\r'); return;    /* keypad enter */
    case 0x4A: input_kbd_push('/'); return;     /* keypad / */
    case 0x14: st.ctrl = 1; return;             /* right ctrl (make) */
    case 0x11: st.alt = 1; return;              /* right alt */
    default: return;
    }
    push_str(seq);
}

void ps2_feed(uint8_t b)
{
    if (st.skip) { st.skip--; return; }
    if (b == 0xE0) { st.ext = 1; return; }
    if (b == 0xF0) { st.release = 1; return; }
    if (b == 0xE1) { st.skip = 7; return; }     /* Pause: E1 14 77 E1 F0 14 F0 77 */
    if (b == 0xAA || b == 0xFA || b == 0xFE || b == 0xEE) {      /* BAT ok, ACK, resend, echo */
        st.ext = st.release = 0;
        return;
    }

    uint16_t ev = b;
    if (st.release) ev |= KEY_RELEASE;
    if (st.ext) ev |= KEY_EXTENDED;
    int release = st.release, ext = st.ext;
    st.release = st.ext = 0;
    input_key_event_push(ev);

    if (ext) {
        if (b == 0x12) return;                  /* fake shift around PrtSc */
        if (release) {
            if (b == 0x14) st.ctrl = 0;
            if (b == 0x11) st.alt = 0;
            return;
        }
        ext_key(b);
        return;
    }

    switch (b) {
    case 0x12: case 0x59: st.shift = !release; return;
    case 0x14: st.ctrl = !release; return;
    case 0x11: st.alt = !release; return;
    case 0x58: if (!release) st.caps = !st.caps; return;
    default: break;
    }
    if (release) return;

    int f = fkey(b);
    if (f) {
        static const char f5[] = { 15, 17, 18, 19, 20, 21, 23, 24 };
        char seq[8];
        if (f <= 4) { seq[0] = 0x1B; seq[1] = 'O'; seq[2] = (char)('P' + f - 1); seq[3] = 0; }
        else ksnprintf(seq, sizeof seq, "\033[%d~", f5[f - 5]);
        push_str(seq);
        return;
    }
    if (b >= 128) return;
    char c = st.shift ? map_shift[b] : map_plain[b];
    if (!c) return;
    if (c >= 'a' && c <= 'z') {
        if (st.caps) c = (char)(c - 'a' + 'A');
    } else if (c >= 'A' && c <= 'Z') {
        if (st.caps && st.shift) c = (char)(c - 'A' + 'a');
    }
    if (st.ctrl) {
        if ((c >= 'a' && c <= 'z')) c = (char)(c - 'a' + 1);
        else if ((c >= 'A' && c <= 'Z')) c = (char)(c - 'A' + 1);
        else if (c >= '@' && c <= '_') c = (char)(c - '@');
        else if (c == ' ' || c == '2') c = 0;        /* NUL */
        else return;
    }
    input_kbd_push((uint8_t)c);
}
