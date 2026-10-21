/* Small filesystem helpers shared by the loader. Plain stdio/POSIX, which
 * libnx maps onto the SD card (sdmc:/) through its fsdev layer. */
#ifndef RSL_FS_H
#define RSL_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read a whole file (at most `max` bytes) into a NUL-terminated malloc'd
 * buffer. Returns NULL and fills err on failure. */
char *rsl_read_file(const char *path, size_t max, size_t *len, char *err, size_t errlen);

/* Replace `path` with `data` so that an interruption at any point leaves
 * either the old or the new content readable: write path.tmp, move the old
 * file to path.bak, move path.tmp to path. path.bak is kept as the previous
 * version. A short write (full SD card) leaves the old file untouched. */
bool rsl_write_atomic(const char *path, const void *data, size_t len, char *err, size_t errlen);

/* px (w x h pixels, XRGB8888: B,G,R,X in memory) as a 24-bit bottom-up BMP,
 * written with rsl_write_atomic. LVGL's BMP decoder reads it. */
bool rsl_write_bmp(const char *path, const uint32_t *px, int w, int h, char *err, size_t errlen);

/* mkdir -p. Returns true if the directory exists afterwards. */
bool rsl_mkdirs(const char *path);

#endif
