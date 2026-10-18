#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_file;
static char g_ring[RSL_LOG_KEEP][256];
static int g_count, g_head;

void rsl_log_open(const char *path)
{
    g_file = fopen(path, "a");
    if (g_file)
        rsl_log("---- log opened ----");
    else
        rsl_log("could not open log file %s", path);
}

void rsl_log(const char *fmt, ...)
{
    char line[256];
    time_t t = time(NULL);
    struct tm tm;
    size_t n = localtime_r(&t, &tm) ? strftime(line, sizeof line, "%Y-%m-%d %H:%M:%S ", &tm) : 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n, fmt, ap);
    va_end(ap);

    if (g_file) {
        fprintf(g_file, "%s\n", line);
        fflush(g_file);
    }
    snprintf(g_ring[g_head], sizeof g_ring[0], "%s", line + n);
    g_head = (g_head + 1) % RSL_LOG_KEEP;
    if (g_count < RSL_LOG_KEEP)
        g_count++;
}

const char *rsl_log_recent(int i)
{
    if (i < 0 || i >= g_count)
        return NULL;
    return g_ring[(g_head - g_count + i + RSL_LOG_KEEP) % RSL_LOG_KEEP];
}

void rsl_log_close(void)
{
    if (g_file) {
        rsl_log("---- log closed ----");
        fclose(g_file);
        g_file = NULL;
    }
}
