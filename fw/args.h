/* Command-line words: quote-aware splitting, DOS-style wildcard matching and the expansion of
 * wildcard arguments against the filesystem (spec "filesystem"). Pure C over fs.h; host
 * tested. */
#ifndef ARGS_H
#define ARGS_H
#include <stdint.h>

#define ARGS_MAX_WORDS 32       /* what a program may receive (exec.h EXEC_MAX_ARGS agrees) */
#define ARGS_MAX_BYTES 512      /* the argument string after expansion, NUL included */

/* Split `line` in place on blanks into at most `max` words; a word may be grouped with double
 * quotes ("a long name.txt"), which are removed. quoted[i] (may be NULL) is set to 1 when word
 * i had quotes. Returns the word count. */
int  args_split(char *line, char **argv, uint8_t *quoted, int max);
/* DOS-style pattern match, case-insensitive: * any run, ? one character. */
int  glob_match(const char *pat, const char *s);
int  has_wild(const char *s);
/* Split "D:/GAMES/STAR.BMP" into dir "D:/GAMES/" (kept with its separator, so names can be
 * appended; "" for the current directory) and name "STAR.BMP" (a pointer into path). */
void split_path(const char *path, char *dir, const char **name);
/* The directory to open for a split prefix: "" is the current directory; a trailing
 * separator comes off unless it is the root ("/", "C:/"). Edits dir in place and returns it. */
const char *dir_for_open(char *dir);
/* dir + name into out (outsz bytes), adding a separator when dir has none. 1 ok, 0 too long. */
int  join_path(char *out, unsigned outsz, const char *dir, const char *name);
/* Expand the unquoted words of `args` that hold * or ? against the directory they name
 * (cwd when none); each match becomes a word, quoted when it has a blank; a pattern that
 * matches nothing stays as typed. The result (words separated by one blank) goes to `out`
 * (outsz bytes). Returns the word count, or E_INVAL when more than ARGS_MAX_WORDS words or
 * outsz bytes would result. */
int  args_expand(const char *args, char *out, unsigned outsz);

#endif
