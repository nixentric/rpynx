/* platform/transfer/ftpd.c on the host, for tests/unit/test_ftpd.py: serves
 * ROOT (the SD card) with the loader's jail on a free port, prints
 * "ready PORT", and stops when stdin closes, printing its last state. */
#include <signal.h>
#include <stdio.h>

#include "ftpd.h"

int main(int argc, char **argv)
{
    char err[128];
    rsl_ftpd_status st;
    if (argc != 2) {
        fprintf(stderr, "usage: ftpd_host ROOT\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    if (!rsl_ftpd_start(argv[1], "/switch/renpy-loader", 0, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    rsl_ftpd_get(&st);
    printf("ready %d\n", st.port);
    fflush(stdout);
    while (getchar() != EOF)
        ;
    rsl_ftpd_get(&st);
    rsl_ftpd_stop();
    printf("received %u file(s), %llu bytes; expecting %llu; done %s%s%s\n", st.files,
           (unsigned long long)st.bytes, (unsigned long long)st.expect, st.done[0] ? st.done : "-",
           st.error[0] ? "; error: " : "", st.error);
    return 0;
}
