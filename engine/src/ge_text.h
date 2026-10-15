#ifndef GE_TEXT_H
#define GE_TEXT_H

/* Internal helpers shared by the text-format loaders (.anim, .layers). */

#include <stddef.h>

/* Whole file as a NUL-terminated string; caller frees. NULL if unreadable. */
char *ge_read_file(const char *path);

/* Copy the next line of `*p` into `line` (comments after '#' removed), advance `*p`.
 * Returns 0 at end of text. */
int ge_next_line(const char **p, char *line, size_t cap);

/* Split `line` in place on spaces/tabs. Returns the token count (at most `max`). */
int ge_tokenize(char *line, char **tok, int max);

#endif
