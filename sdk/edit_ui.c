/* edit_ui.c: the editor's screen and keyboard (edit.h; the editing is in edit_core.c).
 *
 * The screen is drawn on framebuffer page 1 with the BIOS's 6x10 font, 53 columns by 24
 * rows: 23 rows of text and a status line.  The BIOS console on page 0 is not touched, so
 * it is as it was when the editor returns.  A shadow of what each cell shows lets a redraw
 * touch only the cells that changed.  The keyboard is read as raw events (sys_key_event) so
 * that Shift and Ctrl are known for every key, the cursor keys included.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syscalls.h"
#include "edit.h"
#include "../fw/font6x10.h"

#define COLS 53
#define ROWS 24
#define TEXT_ROWS (ROWS - 1)
#define CELL_W FONT6X10_W
#define CELL_H FONT6X10_H
                                        /* 53 * 6 = 318 of the 320 pixels: two spare on the right */
#define UNDO_BYTES 32768u
#define TAB_STOP 4

/* RGB332 */
#define C_TEXT   0xFF
#define C_BACK   0x01                   /* the console's dark blue */
enum { A_NORMAL, A_SELECT, A_CURSOR, A_STATUS, A_COUNT };
static const uint8_t attr_fg[A_COUNT] = { C_TEXT, 0x00, 0x00, 0x00 };
static const uint8_t attr_bg[A_COUNT] = { C_BACK, 0x1F, 0xFC, 0xB6 };

struct ui {
    struct edit_session *s;
    struct edit_buf e;
    uint8_t *fb;                        /* page 1 */
    uint32_t stride;
    size_t top;                         /* offset of the first line shown */
    long top_line;                      /* its 1-based number */
    long left;                          /* first column shown */
    uint8_t ch[ROWS][COLS], at[ROWS][COLS];     /* what the screen shows now */
    long drawn_top_line, drawn_left;    /* the view the screen shows now */
    /* one glyph row as three 16-bit words (six pixels), for each attribute and each of
     * the 64 six-bit patterns: a cell is 30 word moves */
    uint16_t pat[A_COUNT][64][3];
    char message[COLS + 1];             /* shown on the status line until the next key */
    char find[40];
    int shift, ctrl, caps;
};

/* ---- drawing ---------------------------------------------------------------------------------- */

static void make_patterns(struct ui *u)
{
    for (int a = 0; a < A_COUNT; a++)
        for (int bits = 0; bits < 64; bits++)
            for (int w = 0; w < 3; w++) {
                uint8_t l = bits & (0x20 >> (2 * w)) ? attr_fg[a] : attr_bg[a];
                uint8_t r = bits & (0x10 >> (2 * w)) ? attr_fg[a] : attr_bg[a];
                u->pat[a][bits][w] = (uint16_t)(l << 8 | r);
            }
}

/* Cell x is 6 * col, always even, so the pixels can be written as words. */
static void draw_cell(struct ui *u, int row, int col, int c, int attr)
{
    u->ch[row][col] = (uint8_t)c;
    u->at[row][col] = (uint8_t)attr;
    const unsigned char *g = font6x10[(c >= 32 && c < 127) ? c : (c == '\t' || c == 0 ? ' ' : '?')];
    const uint16_t (*pat)[3] = u->pat[attr];
    uint16_t *p = (uint16_t *)(u->fb + (uint32_t)row * CELL_H * u->stride + col * CELL_W);
    uint32_t step = u->stride / 2;
    for (int y = 0; y < CELL_H; y++, p += step) {
        const uint16_t *w = pat[g[y] & 63];
        p[0] = w[0];
        p[1] = w[1];
        p[2] = w[2];
    }
}

/* chr and atr are the shadow rows of `row` (no multiply per cell) */
#define CELL(u, chr, atr, row, col, c, attr) \
    do { if ((chr)[col] != (uint8_t)(c) || (atr)[col] != (attr)) draw_cell(u, row, col, c, attr); } while (0)

static void status(struct ui *u)
{
    char line[COLS + 16], right[32];
    long ln = u->top_line + eb_count_lines(&u->e, u->top, u->e.cursor);
    snprintf(right, sizeof right, "Ln %ld Col %ld", ln, eb_col_of(&u->e, u->e.cursor) + 1);
    if (u->message[0])
        snprintf(line, sizeof line, " %s", u->message);
    else
        snprintf(line, sizeof line, " %.16s%s  ^S Save ^Q Quit", u->s->title ? u->s->title : "",
                 u->e.modified ? " *" : "");
    int n = (int)strlen(line), r = (int)strlen(right);
    uint8_t *chr = u->ch[ROWS - 1], *atr = u->at[ROWS - 1];
    for (int col = 0; col < COLS; col++) {
        int c = ' ';
        if (col < n) c = (unsigned char)line[col];
        if (col >= COLS - 1 - r && col < COLS - 1 && (col >= n + 1 || !u->message[0])) c = (unsigned char)right[col - (COLS - 1 - r)];
        CELL(u, chr, atr, ROWS - 1, col, c, A_STATUS);
    }
}

/* Keep the cursor on the screen, then draw the cells that changed. */
static void render(struct ui *u)
{
    struct edit_buf *e = &u->e;
    size_t cur = e->cursor;

    if (cur < u->top) {                                 /* above the screen */
        size_t ls = eb_line_start(e, cur);
        u->top_line -= eb_count_lines(e, ls, u->top);
        u->top = ls;
    } else {
        long row = eb_count_lines(e, u->top, cur);
        while (row >= TEXT_ROWS) {                      /* below it: move the top down */
            u->top = eb_line_end(e, u->top) + 1;
            u->top_line++;
            row--;
        }
    }
    long col = eb_col_of(e, cur);
    if (col < u->left) u->left = col;
    if (col >= u->left + COLS) u->left = col - COLS + 1;

    /* A scroll of a few lines moves the picture and its shadow; only the new rows are
     * then drawn. */
    long d = u->top_line - u->drawn_top_line;
    if (u->left == u->drawn_left && d != 0 && d < TEXT_ROWS && d > -TEXT_ROWS) {
        size_t rowbytes = CELL_H * u->stride;
        if (d > 0) {
            memcpy(u->fb, u->fb + (size_t)d * rowbytes, (size_t)(TEXT_ROWS - d) * rowbytes);
            memmove(u->ch[0], u->ch[d], (size_t)(TEXT_ROWS - d) * COLS);
            memmove(u->at[0], u->at[d], (size_t)(TEXT_ROWS - d) * COLS);
        } else {
            /* downwards a row at a time from the bottom: rows never overlap, so memcpy
             * (long moves) serves; newlib's memmove goes backwards a byte at a time */
            for (long r = TEXT_ROWS - 1; r >= -d; r--)
                memcpy(u->fb + (size_t)r * rowbytes, u->fb + (size_t)(r + d) * rowbytes, rowbytes);
            memmove(u->ch[-d], u->ch[0], (size_t)(TEXT_ROWS + d) * COLS);
            memmove(u->at[-d], u->at[0], (size_t)(TEXT_ROWS + d) * COLS);
        }
    }
    u->drawn_top_line = u->top_line;
    u->drawn_left = u->left;

    /* Walk the text from the top of the screen with a pointer into the gap buffer: q runs
     * to qend, which is the gap (then the walk hops over it) or the end of the block. */
    size_t from = 0, to = 0, pos = u->top;
    int sel = eb_selection(e, &from, &to);
    const char *bend = e->b + e->cap;
    const char *q = pos < e->gap ? e->b + pos : e->b + pos + (e->gap_end - e->gap);
    const char *qend = pos < e->gap ? e->b + e->gap : bend;
    int ended = 0;                                      /* the row is past the last line */
    long right = u->left + COLS;
    for (int row = 0; row < TEXT_ROWS; row++) {
        uint8_t *chr = u->ch[row], *atr = u->at[row];
        long c = 0;                                     /* column in the line */
        for (;;) {
            if (q == qend && qend != bend) { q = e->b + e->gap_end; qend = bend; }
            int at_end = q == qend;
            int ch = at_end ? '\n' : (unsigned char)*q;
            if (c >= u->left && c < right) {
                int attr = ended ? A_NORMAL : pos == cur ? A_CURSOR
                         : (sel && pos >= from && pos < to) ? A_SELECT : A_NORMAL;
                int col = (int)(c - u->left);
                int shown = ch == '\n' ? ' ' : ch;
                CELL(u, chr, atr, row, col, shown, attr);
            }
            if (ch == '\n') {
                if (at_end) ended = 1; else { q++; pos++; }
                break;
            }
            q++;
            pos++;
            c++;
        }
        for (c = (c < u->left ? u->left : c + 1); c < right; c++) {     /* the rest of the row is blank */
            int col = (int)(c - u->left);
            CELL(u, chr, atr, row, col, ' ', A_NORMAL);
        }
    }
    status(u);
}

/* ---- keyboard ----------------------------------------------------------------------------------- */

/* PS/2 set 2, US layout: the BIOS's tables (fw/ps2.c) */
static const char map_plain[128] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,'\t','`',0, 0,0,0,0,0,'q','1',0, 0,0,'z','s','a','w','2',0,
    0,'c','x','d','e','4','3',0, 0,' ','v','f','t','r','5',0, 0,'n','b','h','g','y','6',0,
    0,0,'m','j','u','7','8',0, 0,',','k','i','o','0','9',0, 0,'.','/','l',';','p','-',0,
    0,0,'\'',0,'[','=',0,0, 0,0,'\r',']',0,'\\',0,0, 0,0,0,0,0,0,'\b',0,
    0,'1',0,'4','7',0,0,0, '0','.','2','5','6','8',0x1B,0, 0,'+','3','-','*','9',0,0,
};
static const char map_shift[128] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,'\t','~',0, 0,0,0,0,0,'Q','!',0, 0,0,'Z','S','A','W','@',0,
    0,'C','X','D','E','$','#',0, 0,' ','V','F','T','R','%',0, 0,'N','B','H','G','Y','^',0,
    0,0,'M','J','U','&','*',0, 0,'<','K','I','O',')','(',0, 0,'>','?','L',':','P','_',0,
    0,0,'"',0,'{','+',0,0, 0,0,'\r','}',0,'|',0,0, 0,0,0,0,0,0,'\b',0,
    0,'1',0,'4','7',0,0,0, '0','.','2','5','6','8',0x1B,0, 0,'+','3','-','*','9',0,0,
};

/* what a key press means to the editor */
enum { K_NONE = 0, K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL, K_F3 };
#define K_CTRL  0x1000                  /* with a letter: K_CTRL | 's' */
#define K_SHIFT 0x2000                  /* with a movement key */

/* The next key press, or K_NONE if no event is waiting.  Characters come back as themselves. */
static int next_key(struct ui *u)
{
    int ev;
    while (sys_pollchar() >= 0) ;       /* the BIOS queues characters too: keep that queue empty */
    while ((ev = sys_key_event()) >= 0) {
        int code = ev & 0x2FF, down = !(ev & KEY_RELEASE);
        if (code == 0x12 || code == 0x59) { u->shift = down; continue; }
        if (code == 0x14 || code == 0x214) { u->ctrl = down; continue; }
        if (!down) continue;
        if (code == 0x58) { u->caps = !u->caps; continue; }
        int k = K_NONE;
        switch (code) {
        case 0x275: k = K_UP; break;
        case 0x272: k = K_DOWN; break;
        case 0x26B: k = K_LEFT; break;
        case 0x274: k = K_RIGHT; break;
        case 0x26C: k = K_HOME; break;
        case 0x269: k = K_END; break;
        case 0x27D: k = K_PGUP; break;
        case 0x27A: k = K_PGDN; break;
        case 0x271: k = K_DEL; break;
        case 0x004: k = K_F3; break;
        case 0x25A: return '\r';        /* keypad Enter */
        }
        if (k) return k | (u->shift ? K_SHIFT : 0) | (u->ctrl ? K_CTRL : 0);
        if (code & KEY_EXTENDED) continue;
        int c = (unsigned char)(u->shift ? map_shift : map_plain)[code & 0x7F];
        if (!c) continue;
        if (u->caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c ^= 0x20;
        if (u->ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return K_CTRL | (c | 0x20);
        return c;
    }
    return K_NONE;
}

static int wait_key(struct ui *u)
{
    int k;
    while ((k = next_key(u)) == K_NONE) sys_wait_vblank();
    return k;
}

/* ---- prompts on the status line -------------------------------------------------------------------- */

/* A line of input after `label`; returns 0 on Enter, -1 on Esc.  `buf` holds the default. */
static int prompt(struct ui *u, const char *label, char *buf, size_t size)
{
    size_t n = strlen(buf);
    for (;;) {
        snprintf(u->message, sizeof u->message, "%s%s_", label, buf);
        status(u);
        int k = wait_key(u);
        if (k == '\r') { u->message[0] = 0; return 0; }
        if (k == 0x1B) { u->message[0] = 0; return -1; }
        if (k == '\b') { if (n) buf[--n] = 0; continue; }
        if (k >= 32 && k < 127 && n + 1 < size) { buf[n++] = (char)k; buf[n] = 0; }
    }
}

/* 'y', 'n' or 0 for Esc */
static int ask(struct ui *u, const char *question)
{
    for (;;) {
        snprintf(u->message, sizeof u->message, "%s (y/n, Esc cancels)", question);
        status(u);
        int k = wait_key(u);
        if (k == 'y' || k == 'Y') { u->message[0] = 0; return 'y'; }
        if (k == 'n' || k == 'N') { u->message[0] = 0; return 'n'; }
        if (k == 0x1B) { u->message[0] = 0; return 0; }
    }
}

static void say(struct ui *u, const char *text) { snprintf(u->message, sizeof u->message, "%s", text); }

/* ---- commands ------------------------------------------------------------------------------------------ */

static void find_next(struct ui *u)
{
    if (!u->find[0]) { say(u, "Nothing to find: press ^F"); return; }
    long at = eb_find(&u->e, u->find, u->e.cursor);
    if (at < 0) { say(u, "Not found"); return; }
    eb_goto(&u->e, (size_t)at, 0);                      /* select the match: typing replaces it */
    eb_goto(&u->e, (size_t)at + strlen(u->find), 1);
}

static int do_save(struct ui *u)
{
    size_t n = eb_flatten(&u->e);
    int rc = u->s->save(u->s->ctx, u->e.b, n);
    if (rc) { snprintf(u->message, sizeof u->message, "Not saved: error %d", rc); return rc; }
    u->e.modified = 0;
    say(u, "Saved");
    return 0;
}

/* returns EDIT_KEEP or EDIT_DISCARD to end the session, -1 to carry on */
static int command(struct ui *u, int k)
{
    struct edit_buf *e = &u->e;
    int sel = (k & K_SHIFT) != 0, ctrl = (k & K_CTRL) != 0;
    int oom = 0;
    char num[12];

    switch (k & ~(K_SHIFT | K_CTRL)) {
    case K_UP:    eb_move(e, EB_UP, sel); return -1;
    case K_DOWN:  eb_move(e, EB_DOWN, sel); return -1;
    case K_LEFT:  eb_move(e, ctrl ? EB_WORD_LEFT : EB_LEFT, sel); return -1;
    case K_RIGHT: eb_move(e, ctrl ? EB_WORD_RIGHT : EB_RIGHT, sel); return -1;
    case K_HOME:  eb_move(e, ctrl ? EB_TOP : EB_HOME, sel); return -1;
    case K_END:   eb_move(e, ctrl ? EB_BOTTOM : EB_END, sel); return -1;
    case K_PGUP:  eb_move_lines(e, -(TEXT_ROWS - 1), sel); return -1;
    case K_PGDN:  eb_move_lines(e, TEXT_ROWS - 1, sel); return -1;
    case K_DEL:   oom = eb_delete(e); break;
    case K_F3:    find_next(u); return -1;
    default:
        if (ctrl) {
            switch (k & 0xFF) {
            case 's':
                if (!u->s->save) return EDIT_KEEP;
                do_save(u);
                return -1;
            case 'q': {
                if (!e->modified) return EDIT_DISCARD;
                int a = ask(u, u->s->save ? "Save changes?" : "Keep changes?");
                if (a == 'n') return EDIT_DISCARD;
                if (a == 'y') { if (!u->s->save) return EDIT_KEEP; if (!do_save(u)) return EDIT_KEEP; }
                return -1;
            }
            case 'z': if (!eb_undo(e)) say(u, "Nothing to undo"); return -1;
            case 'x': oom = eb_cut(e); break;
            case 'c': oom = eb_copy(e); break;
            case 'v': oom = eb_paste(e); break;
            case 'a': eb_select_all(e); return -1;
            case 'f':
                if (prompt(u, "Find: ", u->find, sizeof u->find) == 0) find_next(u);
                return -1;
            case 'g':
                num[0] = 0;
                if (prompt(u, "Go to line: ", num, sizeof num) == 0 && atol(num) > 0)
                    eb_goto(e, eb_line_pos(e, atol(num)), 0);
                return -1;
            }
            return -1;
        }
        if (k == '\r') oom = eb_type(e, '\n');
        else if (k == '\b') oom = eb_backspace(e);
        else if (k == '\t') {
            long col = eb_col_of(e, e->cursor);
            int n = TAB_STOP - (int)(col % TAB_STOP);
            while (n-- && !oom) oom = eb_type(e, ' ');
        } else if (k >= 32 && k < 127) oom = eb_type(e, k);
        break;
    }
    if (oom) say(u, "Out of memory");
    return -1;
}

int edit_run(struct edit_session *s)
{
    struct video_info vi;
    struct ui *u = calloc(1, sizeof *u);
    if (!u) return E_NOMEM;
    if (sys_video_info(&vi) || eb_init(&u->e, s->text, s->len, s->cap, UNDO_BYTES)) { free(u); return E_NOMEM; }
    u->s = s;
    u->fb = (uint8_t *)vi.page_addr[1];
    u->stride = vi.bytes_per_line;
    u->top_line = 1;
    make_patterns(u);
    memset(u->fb, C_BACK, vi.bytes_per_line * vi.height);
    memset(u->ch, ' ', sizeof u->ch);                   /* matches the cleared page: A_NORMAL blanks */
    if (s->start_line > 1) {
        eb_goto(&u->e, eb_line_pos(&u->e, s->start_line), 0);
        size_t ls = eb_line_start(&u->e, u->e.cursor);  /* open with that line a few rows down */
        u->top = ls;
        u->top_line = eb_line_of(&u->e, ls);
        for (int i = 0; i < 5 && u->top > 0; i++) { u->top = eb_line_start(&u->e, u->top - 1); u->top_line--; }
    }
    while (sys_key_event() >= 0) ;                      /* keys from before the editor started */
    u->drawn_top_line = u->top_line;
    render(u);
    sys_video_set_page(1);

    int rc = -1;
    while (rc < 0) {
        int k = wait_key(u);
        u->message[0] = 0;
        do {                                            /* take every waiting key before drawing */
            rc = command(u, k);
        } while (rc < 0 && (k = next_key(u)) != K_NONE);
        if (rc < 0) {
#ifdef EDIT_TIMING                                      /* diagnostic: frames the redraw took */
            uint32_t t0 = sys_ticks();
            render(u);
            if (!u->message[0]) { snprintf(u->message, sizeof u->message, "redraw %lu frames", (unsigned long)(sys_ticks() - t0)); status(u); }
#else
            render(u);
#endif
        }
    }

    s->len = eb_flatten(&u->e);
    s->text = u->e.b;
    s->cap = u->e.cap;
    s->modified = u->e.modified;
    eb_free(&u->e);
    free(u);
    sys_video_set_page(0);
    while (sys_key_event() >= 0) ;
    while (sys_pollchar() >= 0) ;
    return rc;
}
