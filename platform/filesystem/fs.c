#include "fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

char *rsl_read_file(const char *path, size_t max, size_t *len, char *err, size_t errlen)
{
    char *buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        FAIL("open %s: %s", path, strerror(errno));
    size_t cap = max < 4096 ? max : 4096, n = 0;
    buf = malloc(cap + 1);
    if (!buf)
        FAIL("out of memory reading %s", path);
    for (;;) {
        n += fread(buf + n, 1, cap - n, f);
        if (ferror(f))
            FAIL("read %s: %s", path, strerror(errno));
        if (n < cap)
            break;   /* short read without error: EOF */
        if (cap == max) {
            if (fgetc(f) != EOF)
                FAIL("%s is larger than %zu bytes", path, max);
            break;
        }
        cap = cap * 2 > max ? max : cap * 2;
        char *nb = realloc(buf, cap + 1);
        if (!nb)
            FAIL("out of memory reading %s", path);
        buf = nb;
    }
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
fail:
    if (f)
        fclose(f);
    free(buf);
    return NULL;
}

bool rsl_write_atomic(const char *path, const void *data, size_t len, char *err, size_t errlen)
{
    char tmp[1024], bak[1024];
    FILE *f = NULL;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp ||
        snprintf(bak, sizeof bak, "%s.bak", path) >= (int)sizeof bak) {
        snprintf(err, errlen, "path too long: %s", path);
        return false;
    }
    f = fopen(tmp, "wb");
    if (!f)
        FAIL("create %s: %s", tmp, strerror(errno));
    if (fwrite(data, 1, len, f) != len || fflush(f) != 0 || fsync(fileno(f)) != 0)
        FAIL("write %s: %s (SD card full?)", tmp, strerror(errno));
    if (fclose(f) != 0) {
        f = NULL;
        FAIL("close %s: %s", tmp, strerror(errno));
    }
    f = NULL;

    struct stat st;
    bool had_old = stat(path, &st) == 0;
    if (had_old) {
        /* Horizon's rename does not overwrite, so clear the slot first. */
        remove(bak);
        if (rename(path, bak) != 0)
            FAIL("backup %s: %s", path, strerror(errno));
    }
    if (rename(tmp, path) != 0) {
        int e = errno;
        if (had_old)
            rename(bak, path);
        snprintf(err, errlen, "replace %s: %s", path, strerror(e));
        remove(tmp);
        return false;
    }
    return true;
fail:
    if (f)
        fclose(f);
    remove(tmp);
    return false;
}

bool rsl_mkdirs(const char *path)
{
    char buf[1024];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof buf)
        return false;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            mkdir(buf, 0777);   /* errors (EEXIST, "sdmc:") are checked below */
            buf[i] = c;
        }
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
