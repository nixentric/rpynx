/* Add games over Wi-Fi (+ menu): while the page is open, the loader's FTP
 * server (platform/transfer/ftpd.c) takes games from the desktop importer.
 * Leaving it stops the server and reads the library again. */
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "ftpd.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

#define FTP_PORT 5000      /* the importer's default (importer/rsl/library.py) */

static bool serving;
static rsl_ftpd_status last;    /* the previous frame's, for the log lines */
static uint32_t since;          /* when the client connected */
static rsl_ftpd_status at;      /* the state then */

void open_ftp(void)
{
    char err[128];
    u32 ip = 0;
    go(SCR_FTP);
    memset(&g.ftp, 0, sizeof g.ftp);
    g.ftp_addr[0] = g.ftp_title[0] = '\0';
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_SUCCEEDED(rc)) {
        rc = nifmGetCurrentIpAddress(&ip);
        nifmExit();
    }
    if (R_FAILED(rc) || !ip) {
        status(ST_BAD, "Not connected to Wi-Fi: connect in System Settings, then open this page again.");
        return;
    }
    if (R_FAILED(rc = socketInitializeDefault())) {
        status(ST_BAD, "The network cannot be used (0x%x).", (unsigned)rc);
        return;
    }
    if (!rsl_ftpd_start("sdmc:", ROOT + strlen("sdmc:"), FTP_PORT, err, sizeof err)) {
        socketExit();
        status(ST_BAD, "The FTP server did not start: %s.", err);
        return;
    }
    serving = true;
    memset(&last, 0, sizeof last);
    appletSetAutoSleepDisabled(true);   /* a big game takes minutes */
    snprintf(g.ftp_addr, sizeof g.ftp_addr, "%u.%u.%u.%u:%d", (unsigned)(ip & 255), (unsigned)(ip >> 8 & 255),
             (unsigned)(ip >> 16 & 255), (unsigned)(ip >> 24), FTP_PORT);
    rsl_log("ftp: serving on %s", g.ftp_addr);
}

void ftp_tick(void)
{
    if (!serving)
        return;
    rsl_ftpd_get(&g.ftp);
    if (g.ftp.connected && !last.connected) {
        since = g.now;
        at = g.ftp;
    }
    /* One line per connection that sent something (the importer opens several
     * per game): the whole connection, and the time spent receiving alone. */
    if (!g.ftp.connected && last.connected && g.ftp.bytes != at.bytes) {
        double mb = (double)(g.ftp.bytes - at.bytes) / 1e6, s = (double)(g.now - since) / 1000.0,
               recv = (double)(g.ftp.recv_ms - at.recv_ms) / 1000.0;
        rsl_log("ftp: %s sent %u file(s), %.1f MB in %.0f s (%.1f MB/s; %.1f MB/s while receiving)", last.client,
                g.ftp.files - at.files, mb, s, s > 0 ? mb / s : 0.0, recv > 0 ? mb / recv : 0.0);
    }
    if (g.ftp.done[0] && strcmp(g.ftp.done, last.done)) {   /* a game arrived: the page says so */
        struct game_info gi;
        read_manifest(g.ftp.done, &gi);
        snprintf(g.ftp_title, sizeof g.ftp_title, "%s", gi.title[0] ? gi.title : g.ftp.done);
        rsl_log("ftp: %s received (%s)", g.ftp.done, g.ftp_title);
        g.dialog_open = true;
        g.dialog_sel = ARRIVED_LIBRARY;
    }
    if (!g.ftp.running && last.running)
        status(ST_BAD, "The FTP server stopped: %s. Open this page again.", g.ftp.error);
    last = g.ftp;
}

void close_ftp(void)
{
    if (!serving)
        return;
    rsl_ftpd_get(&g.ftp);
    rsl_ftpd_stop();
    socketExit();
    appletSetAutoSleepDisabled(false);
    serving = false;
    rsl_log("ftp: stopped: %u file(s), %.1f MB received", g.ftp.files, (double)g.ftp.bytes / 1e6);
    /* Games may have come, gone or been replaced; the covers follow the new order. */
    scan_games();
    for (int i = 0; i < MAX_GAMES; i++)
        ui_art_changed(i);
}
