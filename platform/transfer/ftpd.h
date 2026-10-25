/* The loader's FTP server, for the desktop importer (importer/rsl/library.py):
 * anonymous, one client at a time, passive mode, in a thread of its own.
 * Plain POSIX sockets and stdio, so it also runs on the host
 * (tests/unit/test_ftpd.py). The client sees `root` as "/" and starts there,
 * like sphaira's server, but only reads and writes inside `jail`, a folder
 * under it; the folders above the jail show just the way down to it. On the
 * Switch root is "sdmc:" and jail "/switch/renpy-loader", where the importer
 * writes. */
#ifndef RSL_FTPD_H
#define RSL_FTPD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool running;          /* listening; false once stopped or after an error */
    int port;              /* the port it listens on */
    bool connected;        /* a client is connected */
    char client[16];       /* its IPv4 address, "" without one */
    char file[128];        /* the file being received, "" between files */
    unsigned files;        /* files received since the start */
    uint64_t bytes;        /* bytes received since the start */
    uint64_t recv_ms;      /* time spent receiving them, for the speed */
    /* The importer's upload of one game: ALLO announces its size (bytes then
     * counts from expect_from), and renaming its .uploading-<id> folder into
     * place (importer/rsl/exporter.py) finishes it. */
    uint64_t expect;       /* bytes announced, 0 when none is under way */
    uint64_t expect_from;
    char done[64];         /* the id of the last game moved into place, "" if none */
    char error[128];       /* why it stopped by itself, "" if it did not */
} rsl_ftpd_status;

/* Listen on port (0: any free one) and serve in a new thread. The sockets
 * must be usable already (libnx: socketInitialize). False with err filled in
 * if it cannot listen. */
bool rsl_ftpd_start(const char *root, const char *jail, int port, char *err, size_t errlen);

/* Stop serving and wait for the thread; a transfer in progress is cut off. */
void rsl_ftpd_stop(void);

/* A copy of the current state, for the screen. */
void rsl_ftpd_get(rsl_ftpd_status *st);

#endif
