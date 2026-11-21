/* Host tests for sdk/edit_core.c: fixed cases, then random operations checked against a
 * plain-string model, with every change undone again at the end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit.h"

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *text_of(struct edit_buf *e)
{
    size_t n = eb_len(e);
    char *s = malloc(n + 1);
    for (size_t i = 0; i < n; i++) s[i] = (char)eb_char(e, i);
    s[n] = 0;
    return s;
}

static void start(struct edit_buf *e, const char *s, size_t undo)
{
    size_t n = strlen(s), cap = n + 8;
    char *b = malloc(cap);
    memcpy(b, s, n);
    eb_init(e, b, n, cap, undo);
}

static void done(struct edit_buf *e) { eb_free(e); free(e->b); }

#define TEXT_IS(e, want) do { char *t_ = text_of(e); CHECK(!strcmp(t_, want), "text is \"%s\", want \"%s\"", t_, want); free(t_); } while (0)

static void fixed(void)
{
    struct edit_buf e;
    size_t a, b;

    start(&e, "one\ntwo\nthree", 4096);
    CHECK(eb_len(&e) == 13, "len %zu", eb_len(&e));
    CHECK(eb_line_of(&e, 0) == 1 && eb_line_of(&e, 4) == 2 && eb_line_of(&e, 13) == 3, "line_of");
    CHECK(eb_line_pos(&e, 2) == 4 && eb_line_pos(&e, 3) == 8 && eb_line_pos(&e, 99) == 8, "line_pos");
    CHECK(eb_line_end(&e, 4) == 7 && eb_line_start(&e, 6) == 4, "line start/end");

    /* typing is one undo step; a newline ends the run */
    eb_goto(&e, 3, 0);
    eb_type(&e, '!'); eb_type(&e, '?'); eb_type(&e, '\n'); eb_type(&e, 'x');
    TEXT_IS(&e, "one!?\nx\ntwo\nthree");
    CHECK(eb_undo(&e) && eb_undo(&e), "two undos");
    TEXT_IS(&e, "one!?\ntwo\nthree");
    CHECK(e.cursor == 5, "cursor after undo %zu", e.cursor);
    CHECK(eb_undo(&e), "third undo");
    TEXT_IS(&e, "one\ntwo\nthree");
    CHECK(!eb_undo(&e), "nothing left to undo");

    /* a run of backspaces is one step */
    eb_goto(&e, 7, 0);
    eb_backspace(&e); eb_backspace(&e); eb_backspace(&e); eb_backspace(&e);
    TEXT_IS(&e, "one\nthree");
    CHECK(eb_undo(&e), "undo backspaces");
    TEXT_IS(&e, "one\ntwo\nthree");
    CHECK(e.cursor == 7, "cursor back at %zu", e.cursor);

    /* selection, cut, paste, typing over a selection */
    eb_goto(&e, 4, 0);
    eb_move(&e, EB_END, 1);
    CHECK(eb_selection(&e, &a, &b) && a == 4 && b == 7, "selection %zu..%zu", a, b);
    eb_cut(&e);
    TEXT_IS(&e, "one\n\nthree");
    eb_move(&e, EB_BOTTOM, 0);
    eb_paste(&e); eb_paste(&e);
    TEXT_IS(&e, "one\n\nthreetwotwo");
    eb_select_all(&e);
    eb_type(&e, 'Z');
    TEXT_IS(&e, "Z");
    eb_undo(&e);
    TEXT_IS(&e, "one\n\nthreetwotwo");
    eb_undo(&e); eb_undo(&e); eb_undo(&e);
    TEXT_IS(&e, "one\ntwo\nthree");

    /* up and down keep the column where they can */
    eb_goto(&e, 12, 0);                         /* "thre|e", column 4 */
    eb_move(&e, EB_UP, 0);
    CHECK(e.cursor == 7, "up to the end of the shorter line: %zu", e.cursor);
    eb_move(&e, EB_DOWN, 0);
    CHECK(e.cursor == 12, "down returns to column 4: %zu", e.cursor);
    eb_move(&e, EB_TOP, 0);
    eb_move_lines(&e, 50, 1);
    CHECK(eb_selection(&e, &a, &b) && a == 0 && b == 8, "shift page down %zu..%zu", a, b);

    /* words */
    done(&e);
    start(&e, "LET total = total + 1", 256);
    eb_move(&e, EB_WORD_RIGHT, 0);
    CHECK(e.cursor == 4, "word right %zu", e.cursor);
    eb_move(&e, EB_BOTTOM, 0);
    eb_move(&e, EB_WORD_LEFT, 0);
    CHECK(e.cursor == 20, "word left %zu", e.cursor);

    /* find: case-insensitive, wraps */
    CHECK(eb_find(&e, "TOTAL", 0) == 4, "find");
    CHECK(eb_find(&e, "total", 5) == 12, "find next");
    CHECK(eb_find(&e, "total", 13) == 4, "find wraps");
    CHECK(eb_find(&e, "zebra", 0) == -1, "find absent");
    done(&e);

    /* a log too small for a change drops the log and still makes the change */
    start(&e, "0123456789abcdefghijklmnopqrstuvwxyz", 40);
    eb_select_all(&e);
    eb_cut(&e);
    TEXT_IS(&e, "");
    CHECK(!eb_undo(&e), "the change did not fit in the log");
    eb_paste(&e);
    TEXT_IS(&e, "0123456789abcdefghijklmnopqrstuvwxyz");
    done(&e);

    /* line ends */
    char crlf[] = "a\r\nb\r\n\rc", out[32];
    int had;
    size_t n = edit_strip_cr(crlf, 8, &had);
    CHECK(n == 6 && had && !memcmp(crlf, "a\nb\n\rc", 6), "strip_cr %zu", n);
    n = edit_add_cr(out, "a\nb\n", 4);
    CHECK(n == 6 && !memcmp(out, "a\r\nb\r\n", 6), "add_cr %zu", n);
}

/* ---- random operations against a model ------------------------------------------------- */

static char model[1 << 16];
static size_t mlen;

static void m_replace(size_t pos, size_t del, const char *s, size_t n)
{
    memmove(model + pos + n, model + pos + del, mlen - pos - del);
    memcpy(model + pos, s, n);
    mlen = mlen - del + n;
}

static unsigned long rnd_state = 12345;
static unsigned rnd(unsigned n) { rnd_state = rnd_state * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rnd_state >> 33) % n; }

static void random_run(unsigned seed, size_t undo_bytes, int expect_full_undo)
{
    struct edit_buf e;
    const char *init = "10 PRINT \"HELLO\"\n20 GOTO 10\n";
    char clip[1 << 16];
    size_t clip_len = 0;
    rnd_state = seed;
    start(&e, init, undo_bytes);
    mlen = strlen(init);
    memcpy(model, init, mlen);

    for (int step = 0; step < 4000 && mlen < 30000; step++) {
        size_t a = e.cursor, b = e.cursor;
        int sel = eb_selection(&e, &a, &b);
        char ch;
        switch (rnd(12)) {
        case 0: case 1: case 2:
            ch = rnd(8) ? (char)('a' + rnd(26)) : '\n';
            eb_type(&e, ch);
            m_replace(a, b - a, &ch, 1);
            break;
        case 3:
            if (sel) m_replace(a, b - a, "", 0); else if (a > 0) m_replace(a - 1, 1, "", 0);
            eb_backspace(&e);
            break;
        case 4:
            if (sel) m_replace(a, b - a, "", 0); else if (a < mlen) m_replace(a, 1, "", 0);
            eb_delete(&e);
            break;
        case 5:
            eb_move(&e, (int)rnd(10), (int)rnd(2));
            break;
        case 6:
            eb_goto(&e, rnd((unsigned)mlen + 1), (int)rnd(2));
            break;
        case 7:
            if (sel) { clip_len = b - a; memcpy(clip, model + a, clip_len); }
            eb_copy(&e);
            break;
        case 8:
            if (sel) { clip_len = b - a; memcpy(clip, model + a, clip_len); m_replace(a, b - a, "", 0); }
            eb_cut(&e);
            break;
        case 9:
            if (clip_len) m_replace(a, b - a, clip, clip_len);
            eb_paste(&e);
            break;
        case 10:
            eb_move_lines(&e, (long)rnd(7) - 3, (int)rnd(2));
            break;
        case 11:
            eb_select_all(&e);
            if (rnd(4)) eb_goto(&e, rnd((unsigned)mlen + 1), 0);
            break;
        }
        if (eb_len(&e) != mlen) { CHECK(0, "seed %u step %d: length %zu, model %zu", seed, step, eb_len(&e), mlen); break; }
        if (e.cursor > mlen) { CHECK(0, "seed %u step %d: cursor %zu past %zu", seed, step, e.cursor, mlen); break; }
    }
    char *t = text_of(&e);
    CHECK(strlen(t) == mlen && !memcmp(t, model, mlen), "seed %u: text differs from the model", seed);
    free(t);
    CHECK(eb_line_of(&e, eb_len(&e)) == 1 + (long)({ long n = 0; for (size_t i = 0; i < mlen; i++) n += model[i] == '\n'; n; }),
          "seed %u: line count", seed);

    int undone = 0;
    while (eb_undo(&e)) undone++;
    if (expect_full_undo) {
        TEXT_IS(&e, init);
    }
    CHECK(undone > 0, "seed %u: nothing was undone", seed);
    size_t n = eb_flatten(&e);
    CHECK(n == eb_len(&e), "flatten");
    done(&e);
}

int main(void)
{
    fixed();
    for (unsigned seed = 1; seed <= 40; seed++) random_run(seed, 4u << 20, 1);  /* a log that holds everything */
    for (unsigned seed = 100; seed <= 120; seed++) random_run(seed, 300, 0);   /* a log that overflows */
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
