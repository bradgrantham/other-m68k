/* The monitor's line editor: cursor movement (Left/Right/Home/End, Ctrl-A/E), Backspace and
 * Delete anywhere, Ctrl-K/U/W kills, a 32-line history on Up/Down, and Tab completion
 * through a caller-supplied function. Draws on the video console (console.c), reads the
 * keyboard (input_getchar). */
#ifndef LINEEDIT_H
#define LINEEDIT_H

#define LE_HISTORY 32
#define LE_LINE_MAX 128

/* Completion: `word` is the token under the cursor (quotes removed), `first` says it is the
 * command word. Fill `out` (outsz bytes) with the completed word: the whole unique match (a
 * directory ends in '/'), or the longest common prefix of the matches; `*unique` = 1 for a
 * single match. Return the number of matches. With `list` set, print the matches instead
 * (newline first; the editor redraws the line afterwards). */
typedef int (*le_complete_fn)(const char *word, int first, char *out, int outsz, int *unique, int list);

/* Print `prompt`, edit a line into buf (max bytes), return its length (Enter). With `history`
 * set the line is recorded and Up/Down browse earlier ones. */
int  lineedit_read(const char *prompt, char *buf, int max, le_complete_fn complete, int history);
void lineedit_history_print(void);          /* the `history` command */

#endif
