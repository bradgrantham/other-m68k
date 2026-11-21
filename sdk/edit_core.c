/* edit_core.c: the text, cursor, selection, clipboard and undo log of the editor (edit.h).
 * No BIOS calls: this file also builds on a host for tests/edit.
 *
 * The text lives in a gap buffer.  Every change goes through replace(), which removes
 * `del` bytes at a position, puts `ins` bytes in their place and writes one undo record:
 *
 *     u32 size | u32 pos | u32 del | u32 ins | u32 cursor_before | del bytes | u32 size
 *
 * Records are packed end to end in a byte arena (fields are read and written with memcpy:
 * the 68000 faults on unaligned words).  The size at both ends lets the newest record be
 * popped and the oldest be dropped when the arena is full.  A run of typed characters, or of
 * Backspaces, extends the newest record instead of adding one, so it undoes as one step.
 */
#include <stdlib.h>
#include <string.h>
#include "edit.h"

#define REC_HEAD 20u                    /* five u32 before the deleted bytes */
#define REC_OVERHEAD (REC_HEAD + 4u)

enum { RUN_NONE, RUN_TYPE, RUN_BACKSPACE };

static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* ---- the gap buffer ---------------------------------------------------------------------- */

size_t eb_len(const struct edit_buf *e) { return e->cap - (e->gap_end - e->gap); }

int eb_char(const struct edit_buf *e, size_t pos)
{
    if (pos >= eb_len(e)) return -1;
    return (unsigned char)(pos < e->gap ? e->b[pos] : e->b[pos + (e->gap_end - e->gap)]);
}

static void gap_to(struct edit_buf *e, size_t pos)
{
    if (pos < e->gap) {
        size_t n = e->gap - pos;
        memmove(e->b + e->gap_end - n, e->b + pos, n);
        e->gap = pos;
        e->gap_end -= n;
    } else if (pos > e->gap) {
        size_t n = pos - e->gap;
        memmove(e->b + e->gap, e->b + e->gap_end, n);
        e->gap += n;
        e->gap_end += n;
    }
}

size_t eb_flatten(struct edit_buf *e)
{
    gap_to(e, eb_len(e));
    return e->gap;
}

static int gap_room(struct edit_buf *e, size_t need)
{
    size_t have = e->gap_end - e->gap;
    if (have >= need) return 0;
    size_t tail = e->cap - e->gap_end;
    size_t cap = e->cap + (need - have) + 4096 + e->cap / 4;
    char *b = realloc(e->b, cap);
    if (!b) return -1;
    memmove(b + cap - tail, b + e->gap_end, tail);
    e->b = b;
    e->gap_end = cap - tail;
    e->cap = cap;
    return 0;
}

static void copy_out(const struct edit_buf *e, size_t pos, size_t n, char *dst)
{
    size_t skip = e->gap_end - e->gap;
    if (pos < e->gap) {
        size_t a = e->gap - pos < n ? e->gap - pos : n;
        memcpy(dst, e->b + pos, a);
        if (n > a) memcpy(dst + a, e->b + e->gap_end, n - a);
    } else {
        memcpy(dst, e->b + pos + skip, n);
    }
}

/* ---- the undo log ------------------------------------------------------------------------- */

static void undo_clear(struct edit_buf *e) { e->ulog_len = 0; e->typing_run = RUN_NONE; }

static void undo_drop_oldest(struct edit_buf *e)
{
    uint32_t size = get32(e->ulog);
    memmove(e->ulog, e->ulog + size, e->ulog_len - size);
    e->ulog_len -= size;
}

/* room for `bytes` more at the end of the arena; 0 if it can never fit */
static int undo_room(struct edit_buf *e, size_t bytes)
{
    if (bytes > e->ulog_cap) return 0;
    while (e->ulog_cap - e->ulog_len < bytes) undo_drop_oldest(e);
    return 1;
}

static uint8_t *undo_last(struct edit_buf *e)
{
    if (!e->ulog_len) return NULL;
    return e->ulog + e->ulog_len - get32(e->ulog + e->ulog_len - 4);
}

/* A new record for: at `pos`, the `del` bytes now in the text are about to be replaced by
 * `ins` bytes.  If it cannot be recorded the whole log is dropped (older records would no
 * longer apply to the text). */
static void undo_record(struct edit_buf *e, size_t pos, size_t del, size_t ins)
{
    size_t size = REC_OVERHEAD + del;
    if (!e->ulog || !undo_room(e, size)) { undo_clear(e); return; }
    uint8_t *r = e->ulog + e->ulog_len;
    put32(r, (uint32_t)size);
    put32(r + 4, (uint32_t)pos);
    put32(r + 8, (uint32_t)del);
    put32(r + 12, (uint32_t)ins);
    put32(r + 16, (uint32_t)e->cursor);
    copy_out(e, pos, del, (char *)r + REC_HEAD);
    put32(r + size - 4, (uint32_t)size);
    e->ulog_len += size;
}

/* ---- every change ---------------------------------------------------------------------------- */

static int replace(struct edit_buf *e, size_t pos, size_t del, const char *ins, size_t n, int record)
{
    if (gap_room(e, n > del ? n - del : 0)) return -1;
    if (record) undo_record(e, pos, del, n);
    gap_to(e, pos);
    e->gap_end += del;
    memcpy(e->b + e->gap, ins, n);
    e->gap += n;
    e->cursor = pos + n;
    e->selecting = 0;
    e->want_col = -1;
    e->modified = 1;
    return 0;
}

int eb_init(struct edit_buf *e, char *text, size_t len, size_t cap, size_t undo_bytes)
{
    memset(e, 0, sizeof *e);
    e->b = text;
    e->cap = cap;
    e->gap = len;
    e->gap_end = cap;
    e->want_col = -1;
    e->ulog_cap = undo_bytes;
    e->ulog = undo_bytes ? malloc(undo_bytes) : NULL;
    return (undo_bytes && !e->ulog) ? -1 : 0;
}

void eb_free(struct edit_buf *e)
{
    free(e->ulog);
    free(e->clip);
    e->ulog = NULL;
    e->clip = NULL;
}

/* ---- positions --------------------------------------------------------------------------------- */

size_t eb_line_start(const struct edit_buf *e, size_t pos)
{
    while (pos > 0 && eb_char(e, pos - 1) != '\n') pos--;
    return pos;
}

size_t eb_line_end(const struct edit_buf *e, size_t pos)
{
    size_t len = eb_len(e);
    while (pos < len && eb_char(e, pos) != '\n') pos++;
    return pos;
}

static long count_lf(const char *p, size_t n)
{
    long lines = 0;
    const char *end = p + n;
    while ((p = memchr(p, '\n', (size_t)(end - p))) != NULL) { lines++; p++; }
    return lines;
}

long eb_count_lines(const struct edit_buf *e, size_t from, size_t to)
{
    size_t skip = e->gap_end - e->gap;
    if (to <= from) return 0;
    if (to <= e->gap) return count_lf(e->b + from, to - from);
    if (from >= e->gap) return count_lf(e->b + from + skip, to - from);
    return count_lf(e->b + from, e->gap - from) + count_lf(e->b + e->gap_end, to - e->gap);
}

long eb_line_of(const struct edit_buf *e, size_t pos) { return 1 + eb_count_lines(e, 0, pos); }

size_t eb_line_pos(const struct edit_buf *e, long line)
{
    size_t pos = 0, len = eb_len(e);
    while (line > 1 && pos < len) {
        size_t end = eb_line_end(e, pos);
        if (end >= len) break;                  /* the last line: stay on it */
        pos = end + 1;
        line--;
    }
    return pos;
}

long eb_col_of(const struct edit_buf *e, size_t pos) { return (long)(pos - eb_line_start(e, pos)); }

/* ---- movement ------------------------------------------------------------------------------------ */

static int is_word(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static void begin_move(struct edit_buf *e, int select)
{
    e->typing_run = RUN_NONE;
    if (select) {
        if (!e->selecting) { e->anchor = e->cursor; e->selecting = 1; }
    } else {
        e->selecting = 0;
    }
}

void eb_goto(struct edit_buf *e, size_t pos, int select)
{
    begin_move(e, select);
    size_t len = eb_len(e);
    e->cursor = pos > len ? len : pos;
    e->want_col = -1;
}

void eb_move_lines(struct edit_buf *e, long lines, int select)
{
    begin_move(e, select);
    if (e->want_col < 0) e->want_col = eb_col_of(e, e->cursor);
    size_t pos = eb_line_start(e, e->cursor), len = eb_len(e);
    for (; lines > 0; lines--) {
        size_t end = eb_line_end(e, pos);
        if (end >= len) break;
        pos = end + 1;
    }
    for (; lines < 0 && pos > 0; lines++) pos = eb_line_start(e, pos - 1);
    size_t end = eb_line_end(e, pos);
    e->cursor = (size_t)e->want_col < end - pos ? pos + (size_t)e->want_col : end;
}

void eb_move(struct edit_buf *e, int how, int select)
{
    size_t len = eb_len(e), pos = e->cursor;
    if (how == EB_UP) { eb_move_lines(e, -1, select); return; }
    if (how == EB_DOWN) { eb_move_lines(e, 1, select); return; }
    begin_move(e, select);
    switch (how) {
    case EB_LEFT:   if (pos > 0) pos--; break;
    case EB_RIGHT:  if (pos < len) pos++; break;
    case EB_HOME:   pos = eb_line_start(e, pos); break;
    case EB_END:    pos = eb_line_end(e, pos); break;
    case EB_TOP:    pos = 0; break;
    case EB_BOTTOM: pos = len; break;
    case EB_WORD_LEFT:
        while (pos > 0 && !is_word(eb_char(e, pos - 1))) pos--;
        while (pos > 0 && is_word(eb_char(e, pos - 1))) pos--;
        break;
    case EB_WORD_RIGHT:
        while (pos < len && is_word(eb_char(e, pos))) pos++;
        while (pos < len && !is_word(eb_char(e, pos))) pos++;
        break;
    }
    e->cursor = pos;
    e->want_col = -1;
}

void eb_select_all(struct edit_buf *e)
{
    e->typing_run = RUN_NONE;
    e->anchor = 0;
    e->cursor = eb_len(e);
    e->selecting = 1;
    e->want_col = -1;
}

int eb_selection(const struct edit_buf *e, size_t *from, size_t *to)
{
    if (!e->selecting || e->anchor == e->cursor) return 0;
    *from = e->anchor < e->cursor ? e->anchor : e->cursor;
    *to = e->anchor < e->cursor ? e->cursor : e->anchor;
    return 1;
}

/* ---- changes ----------------------------------------------------------------------------------------- */

int eb_insert(struct edit_buf *e, const char *s, size_t n)
{
    size_t from = e->cursor, to = e->cursor;
    eb_selection(e, &from, &to);
    e->typing_run = RUN_NONE;
    if (from == to && n == 0) return 0;
    return replace(e, from, to - from, s, n, 1);
}

int eb_type(struct edit_buf *e, int c)
{
    char ch = (char)c;
    size_t from, to;
    uint8_t *r = undo_last(e);
    if (!eb_selection(e, &from, &to) && e->typing_run == RUN_TYPE && c != '\n' && r
        && get32(r + 8) == 0 && get32(r + 4) + get32(r + 12) == e->cursor) {
        if (replace(e, e->cursor, 0, &ch, 1, 0)) return -1;
        put32(r + 12, get32(r + 12) + 1);       /* one more inserted byte in the same step */
        return 0;
    }
    if (eb_insert(e, &ch, 1)) return -1;
    e->typing_run = (c == '\n') ? RUN_NONE : RUN_TYPE;
    return 0;
}

int eb_backspace(struct edit_buf *e)
{
    size_t from, to;
    if (eb_selection(e, &from, &to)) return eb_insert(e, "", 0);
    if (e->cursor == 0) return 0;
    uint8_t *r = undo_last(e);
    if (e->typing_run == RUN_BACKSPACE && r && get32(r + 12) == 0 && get32(r + 4) == e->cursor
        && undo_room(e, 1) && (r = undo_last(e)) != NULL) {
        /* grow the newest record by one deleted byte at its front */
        uint32_t size = get32(r), del = get32(r + 8);
        memmove(r + REC_HEAD + 1, r + REC_HEAD, del);
        r[REC_HEAD] = (uint8_t)eb_char(e, e->cursor - 1);
        put32(r, size + 1);
        put32(r + 4, (uint32_t)(e->cursor - 1));
        put32(r + 8, del + 1);
        put32(r + size + 1 - 4, size + 1);
        e->ulog_len += 1;
        return replace(e, e->cursor - 1, 1, "", 0, 0);
    }
    if (replace(e, e->cursor - 1, 1, "", 0, 1)) return -1;
    e->typing_run = RUN_BACKSPACE;
    return 0;
}

int eb_delete(struct edit_buf *e)
{
    size_t from, to;
    if (eb_selection(e, &from, &to)) return eb_insert(e, "", 0);
    e->typing_run = RUN_NONE;
    if (e->cursor >= eb_len(e)) return 0;
    return replace(e, e->cursor, 1, "", 0, 1);
}

int eb_copy(struct edit_buf *e)
{
    size_t from, to;
    e->typing_run = RUN_NONE;
    if (!eb_selection(e, &from, &to)) return 0;
    size_t n = to - from;
    if (n > e->clip_cap) {
        char *c = realloc(e->clip, n);
        if (!c) return -1;
        e->clip = c;
        e->clip_cap = n;
    }
    copy_out(e, from, n, e->clip);
    e->clip_len = n;
    return 0;
}

int eb_cut(struct edit_buf *e)
{
    size_t from, to;
    if (!eb_selection(e, &from, &to)) return 0;
    if (eb_copy(e)) return -1;
    return eb_insert(e, "", 0);
}

int eb_paste(struct edit_buf *e)
{
    if (!e->clip_len) return 0;
    return eb_insert(e, e->clip, e->clip_len);
}

int eb_undo(struct edit_buf *e)
{
    uint8_t *r = undo_last(e);
    e->typing_run = RUN_NONE;
    if (!r) return 0;
    uint32_t size = get32(r), pos = get32(r + 4), del = get32(r + 8), ins = get32(r + 12);
    uint32_t cursor = get32(r + 16);
    /* put the deleted bytes back in place of the inserted ones; the record's bytes stay
     * valid while replace() runs because nothing is appended to the log (record = 0) */
    if (replace(e, pos, ins, (const char *)r + REC_HEAD, del, 0)) return 0;
    e->ulog_len -= size;
    e->cursor = cursor;
    e->want_col = -1;
    return 1;
}

/* ---- search ----------------------------------------------------------------------------------------------- */

static int fold(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

long eb_find(const struct edit_buf *e, const char *needle, size_t from)
{
    size_t n = strlen(needle), len = eb_len(e);
    if (!n || n > len) return -1;
    size_t last = len - n;
    if (from > last) from = 0;
    size_t pos = from;
    do {
        size_t k = 0;
        while (k < n && fold(eb_char(e, pos + k)) == fold((unsigned char)needle[k])) k++;
        if (k == n) return (long)pos;
        pos = pos == last ? 0 : pos + 1;
    } while (pos != from);
    return -1;
}

/* ---- line ends ------------------------------------------------------------------------------------------------ */

size_t edit_strip_cr(char *text, size_t len, int *had_crlf)
{
    size_t o = 0;
    if (had_crlf) *had_crlf = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') {
            if (had_crlf) *had_crlf = 1;
            continue;
        }
        text[o++] = text[i];
    }
    return o;
}

size_t edit_add_cr(char *out, const char *text, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') out[o++] = '\r';
        out[o++] = text[i];
    }
    return o;
}
