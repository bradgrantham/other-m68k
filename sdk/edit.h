/* edit.h: a full-screen text editor as a library (in build/libsys.a).
 *
 *     struct edit_session s = { .text = buf, .len = n, .cap = cap, .title = "NOTES.TXT" };
 *     int rc = edit_run(&s);          // EDIT_KEEP, EDIT_DISCARD or a negative E_* code
 *     ... s.text / s.len now hold the edited text (the block may have been realloc'd) ...
 *
 * The text is a malloc'd block of bytes with LF line ends (the caller strips CRs on load and
 * puts them back on save if the file had them: edit_strip_cr / edit_add_cr).  edit_run draws
 * on framebuffer page 1 and shows it, reads raw keyboard events, and on return shows page 0
 * again with the console as it was.  If `save` is set, Ctrl-S calls it with the current text
 * and the editor carries on; without it Ctrl-S ends the session with EDIT_KEEP (the caller,
 * BASIC for example, takes the text back).  Ctrl-Q ends the session: with unsaved changes it
 * asks "Save? (y/n)" when there is a `save`, or "Keep changes? (y/n)" when there is not.
 *
 * The editing itself (edit_core.c) has no BIOS calls and builds on a host for the tests in
 * tests/edit; the screen and keyboard are in edit_ui.c.
 */
#ifndef EDIT_H
#define EDIT_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDIT_KEEP     1         /* use s->text / s->len */
#define EDIT_DISCARD  0         /* the user threw the changes away: ignore the text */

struct edit_session {
    char       *text;           /* malloc'd; edit_run may realloc it */
    size_t      len;            /* bytes of text */
    size_t      cap;            /* size of the block */
    const char *title;          /* shown on the status line (a file name, "BASIC program") */
    long        start_line;     /* 1-based line to open at; 0 or 1 = the top */
    int       (*save)(void *ctx, const char *text, size_t len);   /* 0 = saved, else E_* */
    void       *ctx;
    int         modified;       /* out: the text differs from what came in (or was last saved) */
};

int edit_run(struct edit_session *s);

/* Line-end helpers for callers that load and save files.  edit_strip_cr removes every CR
 * that is followed by LF, in place, and returns the new length; *had_crlf is set if any was
 * found.  edit_add_cr writes `len` bytes to `out` (room for 2 * len) with CR LF line ends and
 * returns the new length. */
size_t edit_strip_cr(char *text, size_t len, int *had_crlf);
size_t edit_add_cr(char *out, const char *text, size_t len);

/* ---- the core, for the UI and the host tests --------------------------------------------- */

struct edit_undo;                       /* one reversible change */

struct edit_buf {
    char   *b;                          /* gap buffer: text is b[0..gap) + b[gap_end..cap) */
    size_t  cap, gap, gap_end;
    size_t  cursor;                     /* offset in the text, 0..len */
    size_t  anchor;                     /* selection is between anchor and cursor */
    int     selecting;                  /* anchor is valid */
    long    want_col;                   /* column that Up/Down try to keep; -1 = take it from the cursor */
    int     modified;
    /* undo log: a byte arena of records, oldest dropped when full */
    uint8_t *ulog;
    size_t  ulog_cap, ulog_len;
    int     typing_run;                 /* the last change was a typed character at the cursor */
    /* clipboard */
    char   *clip;
    size_t  clip_len, clip_cap;
};

int    eb_init(struct edit_buf *e, char *text, size_t len, size_t cap, size_t undo_bytes);
void   eb_free(struct edit_buf *e);                 /* frees the undo log and clipboard, not b */
size_t eb_len(const struct edit_buf *e);
int    eb_char(const struct edit_buf *e, size_t pos);       /* byte at pos, or -1 past the end */
size_t eb_flatten(struct edit_buf *e);              /* close the gap at the end: b[0..len) is the text */

/* positions */
size_t eb_line_start(const struct edit_buf *e, size_t pos);
size_t eb_line_end(const struct edit_buf *e, size_t pos);   /* offset of the LF or the end */
long   eb_line_of(const struct edit_buf *e, size_t pos);    /* 1-based */
long   eb_count_lines(const struct edit_buf *e, size_t from, size_t to);   /* LFs in [from, to) */
size_t eb_line_pos(const struct edit_buf *e, long line);    /* start of a 1-based line (clamped) */
long   eb_col_of(const struct edit_buf *e, size_t pos);     /* 0-based column within the line */

/* movement: `select` extends the selection (Shift held), otherwise it is dropped */
enum { EB_LEFT, EB_RIGHT, EB_UP, EB_DOWN, EB_HOME, EB_END, EB_TOP, EB_BOTTOM,
       EB_WORD_LEFT, EB_WORD_RIGHT };
void   eb_move(struct edit_buf *e, int how, int select);
void   eb_move_lines(struct edit_buf *e, long lines, int select);    /* PgUp / PgDn */
void   eb_goto(struct edit_buf *e, size_t pos, int select);
void   eb_select_all(struct edit_buf *e);
int    eb_selection(const struct edit_buf *e, size_t *from, size_t *to);   /* 1 if not empty */

/* changes (each is one undo step, except that a run of typed characters is one step);
 * return 0, or -1 when the text cannot grow */
int    eb_insert(struct edit_buf *e, const char *s, size_t n);   /* replaces the selection */
int    eb_type(struct edit_buf *e, int c);                       /* one typed character */
int    eb_backspace(struct edit_buf *e);
int    eb_delete(struct edit_buf *e);
int    eb_copy(struct edit_buf *e);                              /* selection to the clipboard */
int    eb_cut(struct edit_buf *e);
int    eb_paste(struct edit_buf *e);
int    eb_undo(struct edit_buf *e);                              /* 1 if something was undone */

/* search forward from `from` (wrapping once), ignoring case for ASCII letters; -1 if absent */
long   eb_find(const struct edit_buf *e, const char *needle, size_t from);

#ifdef __cplusplus
}
#endif
#endif
