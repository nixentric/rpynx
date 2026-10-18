#ifndef RSL_LOG_H
#define RSL_LOG_H

/* Append-only text log, flushed per line so a crash still leaves the tail on
 * the SD card. The last RSL_LOG_KEEP lines are also kept for the UI. */
#define RSL_LOG_KEEP 12

void rsl_log_open(const char *path);
void rsl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *rsl_log_recent(int i);   /* 0 = oldest kept, NULL past the end */
void rsl_log_close(void);

#endif
