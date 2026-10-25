#include "ftpd.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0           /* macOS: the host harness ignores SIGPIPE instead */
#endif

#define TICK_MS   200            /* how often a wait looks at the stop flag */
#define IDLE_MS   300000         /* a client silent this long is dropped */
#define DATA_MS   15000          /* waiting for a data connection, or for data on it */
#define BLOCK     (64 << 10)     /* bytes per recv and send */
/* Bigger buffers move more per system call; tune both on the console, where
 * Wi-Fi and the SD card set the speed (sphaira's server: ~1-2 MB/s). */
#define FILE_BUF  (256 << 10)    /* stdio buffer of a received file */
#define SOCK_BUF  (256 << 10)

#define LOCKED(...) do { pthread_mutex_lock(&srv.lock); __VA_ARGS__; pthread_mutex_unlock(&srv.lock); } while (0)

static struct {
    pthread_t thread;
    pthread_mutex_t lock;        /* st, which the screen reads */
    atomic_bool stop;
    bool started;
    int listen_fd;
    char root[200], jail[128];
    rsl_ftpd_status st;
} srv = { .lock = PTHREAD_MUTEX_INITIALIZER };

struct client {
    int ctl, pasv;               /* control connection; passive listener, -1 without one */
    char cwd[256];
    char from[256];              /* RNFR's path, until RNTO */
    char in[1024];               /* control input not yet taken as a line */
    size_t len;
    char *buf;                   /* BLOCK bytes, for transfers and listings */
};

/* Commands that name a path, which must be inside the jail. */
static const char *const path_cmds[] = { "RETR", "STOR", "SIZE", "MKD", "RMD", "DELE", "RNFR", "RNTO" };

/* 1 when fd is readable, 0 after ms (never if < 0) or once the server is
 * stopping, -1 on an error. */
static int wait_fd(int fd, int ms)
{
    for (;;) {
        if (atomic_load(&srv.stop))
            return 0;
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        struct timeval tv = { 0, TICK_MS * 1000 };
        int n = select(fd + 1, &r, NULL, NULL, &tv);
        if (n > 0)
            return 1;
        if (n < 0 && errno != EINTR)
            return -1;
        if (ms >= 0 && (ms -= TICK_MS) <= 0)
            return 0;
    }
}

static bool send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}

static bool reply(struct client *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static bool reply(struct client *c, const char *fmt, ...)
{
    char b[640];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return false;
    size_t len = (size_t)n < sizeof b - 3 ? (size_t)n : sizeof b - 3;
    memcpy(b + len, "\r\n", 2);
    return send_all(c->ctl, b, len + 2);
}

/* The next command, without its line end; false once the client is gone,
 * silent too long, or the server stops. */
static bool read_line(struct client *c, char *out, size_t n)
{
    for (;;) {
        char *nl = memchr(c->in, '\n', c->len);
        if (nl) {
            size_t k = (size_t)(nl - c->in), m = k && c->in[k - 1] == '\r' ? k - 1 : k;
            if (m >= n)
                m = n - 1;
            memcpy(out, c->in, m);
            out[m] = '\0';
            c->len -= k + 1;
            memmove(c->in, nl + 1, c->len);
            return true;
        }
        if (c->len == sizeof c->in || wait_fd(c->ctl, IDLE_MS) <= 0)
            return false;
        ssize_t k = recv(c->ctl, c->in + c->len, sizeof c->in - c->len, 0);
        if (k <= 0)
            return false;
        c->len += (size_t)k;
    }
}

/* arg (absolute, or relative to the working folder) as a clean absolute path
 * in out: "." and ".." resolved, no doubled or trailing "/". False if it is
 * too long or outside the jail. */
static bool resolve(const struct client *c, const char *arg, char *out, size_t n)
{
    char tmp[512], *save;
    size_t len = 0;
    out[0] = '\0';
    if (snprintf(tmp, sizeof tmp, "%s/%s", arg[0] == '/' ? "" : c->cwd, arg) >= (int)sizeof tmp)
        return false;
    for (char *p = strtok_r(tmp, "/", &save); p; p = strtok_r(NULL, "/", &save)) {
        if (!strcmp(p, "."))
            continue;
        if (!strcmp(p, "..")) {
            while (len && out[--len] != '/')
                ;
            out[len] = '\0';
            continue;
        }
        size_t k = strlen(p);
        if (len + 1 + k >= n) {
            out[0] = '\0';
            return false;
        }
        out[len++] = '/';
        memcpy(out + len, p, k + 1);
        len += k;
    }
    if (!len)
        snprintf(out, n, "/");
    size_t j = strlen(srv.jail);
    return !strncmp(out, srv.jail, j) && (out[j] == '\0' || out[j] == '/');
}

static void fs_path(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s%s", srv.root, path);
}

/* For "/" and the folders above the jail ("/switch"): the rest of the jail's
 * path below it, whose first name is the only thing shown there; NULL for
 * any other path. Clients start at "/" and give paths from there, as with
 * sphaira's server (scripts/switch-ftp.sh, curl). */
static const char *way_down(const char *path)
{
    size_t n = strcmp(path, "/") ? strlen(path) : 0;
    if (path[0] != '/' || strncmp(srv.jail, path, n) || srv.jail[n] != '/')
        return NULL;
    return srv.jail + n + 1;
}

/* One line of a listing in out; its length, or -1 if it does not fit. */
static int list_line(char *out, size_t n, const char *cmd, const char *name, int name_len, bool dir,
                     long long size, time_t mtime)
{
    int k;
    if (!strcmp(cmd, "MLSD")) {
        k = snprintf(out, n, "type=%s;size=%lld; %.*s\r\n", dir ? "dir" : "file", size, name_len, name);
    } else if (!strcmp(cmd, "NLST")) {
        k = snprintf(out, n, "%.*s\r\n", name_len, name);
    } else {
        char when[16];
        struct tm tm = { 0 };
        localtime_r(&mtime, &tm);
        strftime(when, sizeof when, "%b %d %H:%M", &tm);
        k = snprintf(out, n, "%s 1 rpynx rpynx %lld %s %.*s\r\n", dir ? "drwxr-xr-x" : "-rw-r--r--", size, when,
                     name_len, name);
    }
    return k < 0 || k >= (int)n ? -1 : k;
}

/* Timeouts, so a client that vanishes cannot hold the thread, and bigger
 * socket buffers. */
static void tune(int fd)
{
    struct timeval tv = { DATA_MS / 1000, 0 };
    int size = SOCK_BUF;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
}

static void drop_pasv(struct client *c)
{
    if (c->pasv >= 0)
        close(c->pasv);
    c->pasv = -1;
}

/* PASV / EPSV: a listener for the next transfer, on any free port. */
static void pasv(struct client *c, bool extended)
{
    struct sockaddr_in a = { .sin_family = AF_INET }, me;
    socklen_t al = sizeof a, ml = sizeof me;
    drop_pasv(c);
    c->pasv = socket(AF_INET, SOCK_STREAM, 0);
    if (c->pasv < 0 || bind(c->pasv, (struct sockaddr *)&a, sizeof a) || listen(c->pasv, 1) ||
        getsockname(c->pasv, (struct sockaddr *)&a, &al) || getsockname(c->ctl, (struct sockaddr *)&me, &ml)) {
        drop_pasv(c);
        reply(c, "425 Cannot open a data port: %s.", strerror(errno));
        return;
    }
    int port = ntohs(a.sin_port);
    const unsigned char *ip = (const unsigned char *)&me.sin_addr.s_addr;
    if (extended)
        reply(c, "229 Entering Extended Passive Mode (|||%d|)", port);
    else
        reply(c, "227 Entering Passive Mode (%u,%u,%u,%u,%d,%d)", ip[0], ip[1], ip[2], ip[3], port >> 8, port & 255);
}

/* The client's data connection on the PASV port, then "150"; -1, with the
 * reply saying why, without one. */
static int open_data(struct client *c)
{
    int fd = c->pasv >= 0 && wait_fd(c->pasv, DATA_MS) > 0 ? accept(c->pasv, NULL, NULL) : -1;
    drop_pasv(c);
    if (fd < 0) {
        reply(c, "425 No data connection: send PASV first.");
        return -1;
    }
    tune(fd);
    reply(c, "150 Opening data connection.");
    return fd;
}

static void list(struct client *c, const char *cmd, const char *arg)
{
    char path[256], fs[512], p[800], line[640];
    while (*arg == '-') {            /* LIST -la: options, ignored */
        const char *sp = strchr(arg, ' ');
        arg = sp ? sp + 1 : "";
    }
    DIR *d = NULL;
    const char *down = NULL;
    if (resolve(c, arg, path, sizeof path)) {
        fs_path(path, fs, sizeof fs);
        d = opendir(fs);
    } else {
        down = way_down(path);
    }
    if (!d && !down) {
        drop_pasv(c);
        reply(c, "550 No such folder.");
        return;
    }
    int fd = open_data(c);
    if (fd < 0) {
        if (d)
            closedir(d);
        return;
    }
    bool ok = true;
    size_t used = 0;
    int k = down ? list_line(c->buf, BLOCK, cmd, down, (int)strcspn(down, "/"), true, 0, 0) : 0;
    used = k > 0 ? (size_t)k : 0;
    for (struct dirent *e; ok && d && (e = readdir(d));) {
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", fs, e->d_name);
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || stat(p, &st))
            continue;
        bool dir = S_ISDIR(st.st_mode);
        int n = list_line(line, sizeof line, cmd, e->d_name, (int)strlen(e->d_name), dir,
                          dir ? 0 : (long long)st.st_size, st.st_mtime);
        if (n < 0)
            continue;
        if (used + (size_t)n > BLOCK) {
            ok = send_all(fd, c->buf, used);
            used = 0;
        }
        memcpy(c->buf + used, line, (size_t)n);
        used += (size_t)n;
    }
    ok = ok && send_all(fd, c->buf, used);
    if (d)
        closedir(d);
    close(fd);
    reply(c, ok ? "226 Listed." : "426 Listing cut off.");
}

static void retr(struct client *c, const char *fs)
{
    struct stat st;
    FILE *f = NULL;
    if (stat(fs, &st) || !S_ISREG(st.st_mode) || !(f = fopen(fs, "rb"))) {
        drop_pasv(c);
        reply(c, "550 No such file.");
        return;
    }
    int fd = open_data(c);
    if (fd < 0) {
        fclose(f);
        return;
    }
    bool ok = true;
    for (size_t k; ok && (k = fread(c->buf, 1, BLOCK, f)) > 0;)
        ok = !atomic_load(&srv.stop) && send_all(fd, c->buf, k);
    ok = ok && !ferror(f);
    fclose(f);
    close(fd);
    reply(c, ok ? "226 Sent." : "426 Transfer cut off.");
}

/* A received file that did not arrive whole is removed. */
static void stor(struct client *c, const char *path, const char *fs)
{
    FILE *f = fopen(fs, "wb");
    if (!f) {
        drop_pasv(c);
        reply(c, "553 Cannot create the file: %s.", strerror(errno));
        return;
    }
    setvbuf(f, NULL, _IOFBF, FILE_BUF);
    int fd = open_data(c);
    if (fd < 0) {
        fclose(f);
        remove(fs);
        return;
    }
    LOCKED(snprintf(srv.st.file, sizeof srv.st.file, "%s", path + strlen(srv.jail)));
    bool ok = true;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        ssize_t k = wait_fd(fd, DATA_MS) > 0 ? recv(fd, c->buf, BLOCK, 0) : -1;
        if (k == 0)
            break;
        if (k < 0 || fwrite(c->buf, 1, (size_t)k, f) != (size_t)k) {
            ok = false;
            break;
        }
        LOCKED(srv.st.bytes += (uint64_t)k);
    }
    ok = fclose(f) == 0 && ok;
    close(fd);
    if (!ok)
        remove(fs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    int64_t ms = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    LOCKED(srv.st.file[0] = '\0'; srv.st.files += ok; srv.st.recv_ms += (uint64_t)(ms > 0 ? ms : 0));
    reply(c, ok ? "226 Received." : "451 Transfer failed; the file was removed.");
}

static void session(int ctl)
{
    struct client c = { .ctl = ctl, .pasv = -1, .buf = malloc(BLOCK) };
    char line[512], path[256], fs[512];
    bool in = false;
    snprintf(c.cwd, sizeof c.cwd, "/");
    if (c.buf && reply(&c, "220 rpynx is ready."))
        while (read_line(&c, line, sizeof line)) {
            char *sp = strchr(line, ' ');
            const char *arg = sp ? sp + 1 : "";
            if (sp)
                *sp = '\0';
            for (char *p = line; *p; p++)
                *p = (char)toupper((unsigned char)*p);
            if (!strcmp(line, "CDUP")) {
                strcpy(line, "CWD");
                arg = "..";
            }
            bool path_cmd = false;
            for (size_t i = 0; i < sizeof path_cmds / sizeof path_cmds[0]; i++)
                path_cmd |= !strcmp(line, path_cmds[i]);
            bool named = resolve(&c, arg, path, sizeof path);
            fs_path(path, fs, sizeof fs);
            struct stat st;

            if (!strcmp(line, "USER")) {
                reply(&c, "331 Any password will do.");
            } else if (!strcmp(line, "PASS")) {
                in = true;
                reply(&c, "230 Logged in.");
            } else if (!strcmp(line, "QUIT")) {
                reply(&c, "221 Bye.");
                break;
            } else if (!in) {
                reply(&c, "530 Log in first.");
            } else if (!strcmp(line, "SYST")) {
                reply(&c, "215 UNIX Type: L8");
            } else if (!strcmp(line, "FEAT")) {
                reply(&c, "211-Features:\r\n MLSD\r\n SIZE\r\n UTF8\r\n211 End");
            } else if (!strcmp(line, "TYPE") || !strcmp(line, "MODE") || !strcmp(line, "STRU") ||
                       !strcmp(line, "OPTS") || !strcmp(line, "NOOP")) {
                reply(&c, "200 OK.");
            } else if (!strcmp(line, "PWD")) {
                reply(&c, "257 \"%s\"", c.cwd);
            } else if (!strcmp(line, "CWD")) {
                if (named ? !stat(fs, &st) && S_ISDIR(st.st_mode) : way_down(path) != NULL) {
                    snprintf(c.cwd, sizeof c.cwd, "%s", path);
                    reply(&c, "250 OK.");
                } else {
                    reply(&c, "550 No such folder.");
                }
            } else if (!strcmp(line, "ALLO")) {
                uint64_t n = strtoull(arg, NULL, 10);
                LOCKED(srv.st.expect = n; srv.st.expect_from = srv.st.bytes; srv.st.done[0] = '\0');
                reply(&c, "200 Expecting %llu bytes.", (unsigned long long)n);
            } else if (!strcmp(line, "PASV") || !strcmp(line, "EPSV")) {
                pasv(&c, line[0] == 'E');
            } else if (!strcmp(line, "MLSD") || !strcmp(line, "LIST") || !strcmp(line, "NLST")) {
                list(&c, line, arg);
            } else if (!path_cmd) {
                reply(&c, "502 Not supported.");
            } else if (!named) {
                drop_pasv(&c);
                reply(&c, "550 Only %s can be reached.", srv.jail);
            } else if (!strcmp(line, "RETR")) {
                retr(&c, fs);
            } else if (!strcmp(line, "STOR")) {
                stor(&c, path, fs);
            } else if (!strcmp(line, "SIZE")) {
                if (!stat(fs, &st) && S_ISREG(st.st_mode))
                    reply(&c, "213 %lld", (long long)st.st_size);
                else
                    reply(&c, "550 No such file.");
            } else if (!strcmp(line, "MKD")) {
                if (mkdir(fs, 0777))
                    reply(&c, "550 %s.", strerror(errno));
                else
                    reply(&c, "257 \"%s\" created.", path);
            } else if (!strcmp(line, "RMD") || !strcmp(line, "DELE")) {
                if (line[0] == 'R' ? rmdir(fs) : unlink(fs))
                    reply(&c, "550 %s.", strerror(errno));
                else
                    reply(&c, "250 Removed.");
            } else if (!strcmp(line, "RNFR")) {
                if (stat(fs, &st)) {
                    reply(&c, "550 No such file or folder.");
                } else {
                    snprintf(c.from, sizeof c.from, "%s", path);
                    reply(&c, "350 Send RNTO.");
                }
            } else {   /* RNTO */
                char from[512];
                fs_path(c.from, from, sizeof from);
                if (!c.from[0]) {
                    reply(&c, "503 Send RNFR first.");
                } else if (rename(from, fs)) {
                    reply(&c, "550 %s.", strerror(errno));
                } else {
                    if (!strncmp(strrchr(c.from, '/') + 1, ".uploading-", 11))   /* a game moved into place */
                        LOCKED(snprintf(srv.st.done, sizeof srv.st.done, "%s", strrchr(path, '/') + 1);
                               srv.st.expect = 0);
                    reply(&c, "250 Renamed.");
                }
                c.from[0] = '\0';
            }
        }
    drop_pasv(&c);
    free(c.buf);
}

static void *serve(void *arg)
{
    (void)arg;
    for (;;) {
        int r = wait_fd(srv.listen_fd, -1);
        int fd = -1;
        struct sockaddr_in a;
        socklen_t al = sizeof a;
        if (r > 0 && (fd = accept(srv.listen_fd, (struct sockaddr *)&a, &al)) < 0 &&
            (errno == EINTR || errno == ECONNABORTED))
            continue;
        if (fd < 0) {
            if (r != 0)
                LOCKED(snprintf(srv.st.error, sizeof srv.st.error, "the network went away (%s)", strerror(errno)));
            break;
        }
        tune(fd);
        const unsigned char *ip = (const unsigned char *)&a.sin_addr.s_addr;
        LOCKED(srv.st.connected = true;
               snprintf(srv.st.client, sizeof srv.st.client, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]));
        session(fd);
        close(fd);
        LOCKED(srv.st.connected = false; srv.st.client[0] = srv.st.file[0] = '\0');
    }
    close(srv.listen_fd);
    LOCKED(srv.st.running = false);
    return NULL;
}

bool rsl_ftpd_start(const char *root, const char *jail, int port, char *err, size_t errlen)
{
    if (srv.started)
        return true;
    if (strlen(root) >= sizeof srv.root || strlen(jail) >= sizeof srv.jail) {
        snprintf(err, errlen, "folder name too long: %s%s", root, jail);
        return false;
    }
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    socklen_t al = sizeof a;
    int one = 1, fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0)
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 2) ||
        getsockname(fd, (struct sockaddr *)&a, &al)) {
        snprintf(err, errlen, "cannot listen on port %d: %s", port, strerror(errno));
        if (fd >= 0)
            close(fd);
        return false;
    }
    snprintf(srv.root, sizeof srv.root, "%s", root);
    snprintf(srv.jail, sizeof srv.jail, "%s", jail);
    srv.listen_fd = fd;
    atomic_store(&srv.stop, false);
    memset(&srv.st, 0, sizeof srv.st);   /* no thread yet */
    srv.st.running = true;
    srv.st.port = ntohs(a.sin_port);
    if (pthread_create(&srv.thread, NULL, serve, NULL)) {
        snprintf(err, errlen, "cannot start its thread");
        close(fd);
        srv.st.running = false;
        return false;
    }
    srv.started = true;
    return true;
}

void rsl_ftpd_stop(void)
{
    if (!srv.started)
        return;
    atomic_store(&srv.stop, true);
    pthread_join(srv.thread, NULL);
    srv.started = false;
}

void rsl_ftpd_get(rsl_ftpd_status *st)
{
    LOCKED(*st = srv.st);
}
