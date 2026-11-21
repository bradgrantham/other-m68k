/* edit: the full-screen text editor (sdk/edit.h) on one file.
 *
 *   edit FILE        open FILE, or start a new file of that name
 *
 * Ctrl-S saves, Ctrl-Q quits (asking first if there are unsaved changes), Ctrl-Z undoes,
 * Shift with the cursor keys selects, Ctrl-X/C/V cut, copy and paste, Ctrl-F finds, F3 finds
 * again, Ctrl-G goes to a line.  A file with CR LF line ends is written back with them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "syscalls.h"
#include "edit.h"

struct file { const char *path; int crlf; };

static int save(void *ctx, const char *text, size_t len)
{
    struct file *f = ctx;
    char *out = NULL;
    if (f->crlf) {
        out = malloc(len * 2 + 1);
        if (!out) return E_NOMEM;
        len = edit_add_cr(out, text, len);
        text = out;
    }
    FILE *fp = fopen(f->path, "wb");
    int rc = 0;
    if (!fp) rc = -errno;
    else {
        if (fwrite(text, 1, len, fp) != len) rc = E_IO;
        if (fclose(fp) && !rc) rc = E_IO;
    }
    free(out);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc != 2) { printf("usage: edit FILE\n"); return 1; }
    struct file f = { argv[1], 0 };
    struct edit_session s = { 0 };
    long size = 0;

    FILE *fp = fopen(f.path, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        rewind(fp);
    } else if (errno != ENOENT) {
        printf("edit: %s: %s\n", f.path, strerror(errno));
        return 1;
    }
    s.cap = (size_t)size + 4096;
    s.text = malloc(s.cap);
    if (!s.text) { printf("edit: %s does not fit in memory\n", f.path); if (fp) fclose(fp); return 1; }
    if (fp) {
        s.len = fread(s.text, 1, (size_t)size, fp);
        fclose(fp);
        s.len = edit_strip_cr(s.text, s.len, &f.crlf);
    }
    const char *name = f.path;                          /* the title is the name without its directories */
    for (const char *p = f.path; *p; p++) if (*p == '/' || *p == ':') name = p + 1;
    s.title = name;
    s.save = save;
    s.ctx = &f;

    int rc = edit_run(&s);
    free(s.text);
    if (rc < 0) { printf("edit: error %d\n", rc); return 1; }
    return 0;
}
