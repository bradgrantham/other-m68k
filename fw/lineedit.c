/* See lineedit.h. The line may wrap over several screen rows; the editor remembers where it
 * starts (x0, y0) and redraws from the changed character onward, correcting y0 when the
 * console scrolls. Escape sequences are the ones ps2.c sends (ESC [ A/B/C/D arrows, H/F
 * Home/End, 3~ Delete, 1~/4~ Home/End). */
#include "bios.h"
#include "lineedit.h"

static char hist[LE_HISTORY][LE_LINE_MAX];
static int hist_n, hist_head;               /* hist_head: slot the next line goes to */

static const char *hist_get(int back)       /* 0 = most recent */
{
    return hist[(hist_head - 1 - back + LE_HISTORY) % LE_HISTORY];
}

static void hist_push(const char *line)
{
    if (!line[0]) return;
    if (hist_n && !strcmp(hist_get(0), line)) return;
    strcpy(hist[hist_head], line);
    hist_head = (hist_head + 1) % LE_HISTORY;
    if (hist_n < LE_HISTORY) hist_n++;
}

void lineedit_history_print(void)
{
    for (int i = hist_n - 1; i >= 0; i--) kprintf("%2d  %s\n", hist_n - i, hist_get(i));
}

struct le {
    char *buf; int max, n, pos, prev_n;
    int x0, y0;                             /* screen cell of buf[0] */
    const char *prompt;
};

static void cell(const struct le *e, int i, int *x, int *y)
{
    *x = (e->x0 + i) % CON_COLS;
    *y = e->y0 + (e->x0 + i) / CON_COLS;
}

/* redraw buf[from..] and erase what the line no longer covers, then put the cursor at pos */
static void redraw(struct le *e, int from)
{
    int x, y;
    cell(e, from, &x, &y);
    con_set_cursor(x, y);
    for (int i = from; i < e->n; i++) con_putc(e->buf[i]);
    int last = e->n;                        /* cells that were drawn or erased: 0..last-1 */
    if (e->prev_n > e->n) { for (int i = e->n; i < e->prev_n; i++) con_putc(' '); last = e->prev_n; }
    if (last > 0) {                         /* did the console scroll while drawing? */
        int row = e->y0 + (e->x0 + last - 1) / CON_COLS;
        if (row > CON_ROWS - 1) e->y0 -= row - (CON_ROWS - 1);
    }
    e->prev_n = e->n;
    cell(e, e->pos, &x, &y);
    con_set_cursor(x, y);
}

static void set_line(struct le *e, const char *s)
{
    int n = (int)strlen(s);
    if (n > e->max - 1) n = e->max - 1;
    memcpy(e->buf, s, (size_t)n);
    e->buf[n] = 0;
    e->n = e->pos = n;
    redraw(e, 0);
}

static void insert(struct le *e, const char *s, int len)
{
    if (e->n + len > e->max - 1) len = e->max - 1 - e->n;
    if (len <= 0) return;
    memmove(e->buf + e->pos + len, e->buf + e->pos, (size_t)(e->n - e->pos));
    memcpy(e->buf + e->pos, s, (size_t)len);
    e->n += len; e->buf[e->n] = 0;
    int from = e->pos;
    e->pos += len;
    redraw(e, from);
}

static void erase(struct le *e, int at, int len)          /* remove buf[at..at+len) */
{
    if (at < 0 || len <= 0 || at + len > e->n) return;
    memmove(e->buf + at, e->buf + at + len, (size_t)(e->n - at - len));
    e->n -= len; e->buf[e->n] = 0;
    if (e->pos > at) e->pos = (e->pos >= at + len) ? e->pos - len : at;
    redraw(e, at);
}

/* the token under the cursor: [start, pos), quotes stripped into word */
static int token(const struct le *e, char *word, int wmax, int *start, int *quoted)
{
    int i = e->pos, q = 0;
    for (int k = 0; k < e->pos; k++) if (e->buf[k] == '"') q = !q;      /* inside quotes at pos? */
    while (i > 0 && (q || (e->buf[i - 1] != ' ' && e->buf[i - 1] != '\t'))) {
        i--;
        if (e->buf[i] == '"') q = !q;
    }
    *start = i;
    int n = 0;
    *quoted = 0;
    for (int k = i; k < e->pos; k++) {
        if (e->buf[k] == '"') { *quoted = 1; continue; }
        if (n < wmax - 1) word[n++] = e->buf[k];
    }
    word[n] = 0;
    int first = 1;
    for (int k = 0; k < i; k++) if (e->buf[k] != ' ' && e->buf[k] != '\t') { first = 0; break; }
    return first;
}

static void complete(struct le *e, le_complete_fn fn)
{
    char word[LE_LINE_MAX], out[LE_LINE_MAX], repl[LE_LINE_MAX + 4];
    int start, quoted, unique = 0;
    if (!fn) return;
    int first = token(e, word, sizeof word, &start, &quoted);
    int matches = fn(word, first, out, sizeof out, &unique, 0);
    if (matches <= 0) return;
    int need_q = quoted || strchr(out, ' ') != 0;
    int k = 0;
    if (need_q) repl[k++] = '"';
    strcpy(repl + k, out); k += (int)strlen(out);
    if (unique && out[0] && out[strlen(out) - 1] != '/') {
        if (need_q) repl[k++] = '"';
        repl[k++] = ' ';
    }
    repl[k] = 0;
    int cur_len = e->pos - start;
    if (k == cur_len && !memcmp(e->buf + start, repl, (size_t)k)) {
        if (matches > 1) {                  /* nothing more to add: show the choices */
            con_cursor_show(0);
            int x, y;
            cell(e, e->n, &x, &y);
            con_set_cursor(x, y);
            kprintf("\n");
            fn(word, first, out, sizeof out, &unique, 1);
            kprintf("%s", e->prompt);
            con_get_cursor(&e->x0, &e->y0);
            e->prev_n = 0;
            redraw(e, 0);
            con_cursor_show(1);
        }
        return;
    }
    e->pos = start;
    erase(e, start, cur_len);
    insert(e, repl, k);
}

/* ESC was read: consume the rest of the sequence, return a key code (negative = ignore) */
enum { K_UP = 1000, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_DEL };
static int escape(void)
{
    int c = input_getchar();
    if (c == 'O') { input_getchar(); return -1; }                    /* F1-F4 */
    if (c != '[') return -1;
    int num = 0;
    for (;;) {
        c = input_getchar();
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); continue; }
        break;
    }
    switch (c) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case '~': return num == 3 ? K_DEL : num == 1 ? K_HOME : num == 4 ? K_END : -1;
    default: return -1;
    }
}

int lineedit_read(const char *prompt, char *buf, int max, le_complete_fn fn, int history)
{
    struct le e;
    char stash[LE_LINE_MAX];
    static int prev_cr;
    int hidx = -1;                          /* -1: the new line; 0..: history entry shown */
    if (max > LE_LINE_MAX) max = LE_LINE_MAX;
    kprintf("%s", prompt);
    e.buf = buf; e.max = max; e.n = e.pos = e.prev_n = 0; e.prompt = prompt;
    con_get_cursor(&e.x0, &e.y0);
    buf[0] = 0;
    stash[0] = 0;
    con_cursor_show(1);
    for (;;) {
        int c = input_getchar();
        if (c == '\n' && prev_cr) { prev_cr = 0; continue; }       /* CRLF is one Enter */
        prev_cr = (c == '\r');
        if (c == 0x1B) { c = escape(); if (c < 0) continue; }
        switch (c) {
        case '\r': case '\n': {
            int x, y;
            cell(&e, e.n, &x, &y);
            con_set_cursor(x, y);
            con_cursor_show(0);
            putchar_both('\n');
            if (history) hist_push(buf);
            return e.n;
        }
        case '\b': case 0x7F: if (e.pos > 0) erase(&e, e.pos - 1, 1); break;
        case K_DEL: case 0x04: if (e.pos < e.n) erase(&e, e.pos, 1); break;
        case K_LEFT: case 0x02: if (e.pos > 0) { e.pos--; redraw(&e, e.n); } break;
        case K_RIGHT: case 0x06: if (e.pos < e.n) { e.pos++; redraw(&e, e.n); } break;
        case K_HOME: case 0x01: e.pos = 0; redraw(&e, e.n); break;
        case K_END: case 0x05: e.pos = e.n; redraw(&e, e.n); break;
        case 0x0B: erase(&e, e.pos, e.n - e.pos); break;                     /* ^K */
        case 0x15: case 0x03: e.pos = 0; erase(&e, 0, e.n); break;          /* ^U ^C */
        case 0x17: {                                                         /* ^W */
            int i = e.pos;
            while (i > 0 && buf[i - 1] == ' ') i--;
            while (i > 0 && buf[i - 1] != ' ') i--;
            erase(&e, i, e.pos - i);
            break;
        }
        case K_UP:
            if (history && hidx + 1 < hist_n) {
                if (hidx == -1) strcpy(stash, buf);
                hidx++;
                set_line(&e, hist_get(hidx));
            }
            break;
        case K_DOWN:
            if (hidx > 0) { hidx--; set_line(&e, hist_get(hidx)); }
            else if (hidx == 0) { hidx = -1; set_line(&e, stash); }
            break;
        case '\t': complete(&e, fn); break;
        default:
            if (c >= 0x20 && c <= 0x7E) { char ch = (char)c; insert(&e, &ch, 1); }
            break;
        }
    }
}
