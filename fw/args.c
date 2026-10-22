/* See args.h. */
#include "bios.h"
#include "fs.h"
#include "args.h"

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int args_split(char *line, char **argv, uint8_t *quoted, int max)
{
    int argc = 0;
    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) break;
        char *out = line;                       /* the word is rewritten in place without its quotes */
        if (quoted) quoted[argc] = 0;
        char *start = line;
        int q = 0;
        while (*line && (q || (*line != ' ' && *line != '\t'))) {
            if (*line == '"') { q = !q; line++; if (quoted) quoted[argc] = 1; continue; }
            *out++ = *line++;
        }
        if (*line) line++;                      /* past the blank (it may be overwritten below) */
        *out = 0;
        argv[argc++] = start;
    }
    if (quoted) for (int i = argc; i < max; i++) quoted[i] = 0;
    return argc;
}

int glob_match(const char *pat, const char *s)
{
    for (;;) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            for (; *s; s++) if (glob_match(pat, s)) return 1;
            return 0;
        }
        if (!*s) return *pat == 0;
        if (*pat != '?' && lower(*pat) != lower(*s)) return 0;
        pat++; s++;
    }
}

int has_wild(const char *s) { return strchr(s, '*') != 0 || strchr(s, '?') != 0; }

void split_path(const char *path, char *dir, const char **name)
{
    const char *p = path + strlen(path);
    while (p > path && p[-1] != '/' && p[-1] != '\\' && p[-1] != ':') p--;
    memcpy(dir, path, (size_t)(p - path));
    dir[p - path] = 0;
    *name = p;
}

const char *dir_for_open(char *dir)
{
    size_t n = strlen(dir);
    if (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\') && dir[n - 2] != ':') dir[n - 1] = 0;
    return dir;
}

int join_path(char *out, unsigned outsz, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int sep = n && dir[n - 1] != '/' && dir[n - 1] != '\\' && dir[n - 1] != ':';
    if (n + (size_t)sep + strlen(name) + 1 > outsz) return 0;
    strcpy(out, dir);
    if (sep) out[n++] = '/';
    strcpy(out + n, name);
    return 1;
}

/* append word w (quoted if it has a blank) to out; 0 when it does not fit */
static int put_word(char *out, unsigned outsz, unsigned *len, const char *w, int *words)
{
    int q = strchr(w, ' ') != 0;
    size_t need = strlen(w) + (q ? 2 : 0) + (*len ? 1 : 0);
    if (*words >= ARGS_MAX_WORDS || *len + need + 1 > outsz) return 0;
    if (*len) out[(*len)++] = ' ';
    if (q) out[(*len)++] = '"';
    strcpy(out + *len, w); *len += (unsigned)strlen(w);
    if (q) out[(*len)++] = '"';
    out[*len] = 0;
    (*words)++;
    return 1;
}

int args_expand(const char *args, char *out, unsigned outsz)
{
    static char line[ARGS_MAX_BYTES];
    char *argv[ARGS_MAX_WORDS + 1];
    uint8_t quoted[ARGS_MAX_WORDS + 1];
    if (strlen(args) >= sizeof line) return E_INVAL;
    strcpy(line, args);
    int argc = args_split(line, argv, quoted, ARGS_MAX_WORDS + 1);
    if (argc > ARGS_MAX_WORDS) return E_INVAL;
    unsigned len = 0;
    int words = 0;
    out[0] = 0;
    for (int i = 0; i < argc; i++) {
        if (quoted[i] || !has_wild(argv[i])) {
            if (!put_word(out, outsz, &len, argv[i], &words)) return E_INVAL;
            continue;
        }
        char dir[FS_PATH_MAX], opendir_name[FS_PATH_MAX], full[FS_PATH_MAX];
        const char *pat;
        split_path(argv[i], dir, &pat);
        strcpy(opendir_name, dir);
        int h = fs_opendir(dir_for_open(opendir_name)), n = 0;
        if (h >= 0) {
            struct dirent e;
            while (fs_readdir(h, &e) > 0) {
                if (!glob_match(pat, e.name)) continue;
                if (!join_path(full, sizeof full, dir, e.name) || !put_word(out, outsz, &len, full, &words)) { fs_closedir(h); return E_INVAL; }
                n++;
            }
            fs_closedir(h);
        }
        if (n == 0 && !put_word(out, outsz, &len, argv[i], &words)) return E_INVAL;   /* no match: as typed */
    }
    return words;
}
