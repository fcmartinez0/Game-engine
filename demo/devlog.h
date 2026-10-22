#ifndef DEVLOG_H
#define DEVLOG_H

/* Play log for playtesting: a plain-text file you can send back to whoever is
 * tuning the game. Every line is stamped with the session's play time.
 * On a 3DS it lands on the SD card next to the game. */

#include <stdbool.h>
#include <stdint.h>

bool devlog_open(void);                 /* picks the path; starts a fresh file, the last one becomes *_previous.txt */
bool devlog_is_open(void);
const char *devlog_path(void);
void devlog_set_clock(uint64_t start_us, uint64_t now_us);
void devlog(const char *fmt, ...);      /* collected in memory... */
void devlog_flush(void);                /* ...and written to the file here: call it at calm moments */
void devlog_flush_if_due(uint64_t max_age_us);   /* flush if anything has waited this long */
void devlog_close(void);

/* "hh:mm:ss" for a duration in seconds */
const char *devlog_hms(uint64_t seconds);

#endif
