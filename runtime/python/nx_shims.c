/* newlib functions that libnx leaves unimplemented but CPython or Ren'Py need. */
#include <errno.h>
#include <malloc.h>
#include <reent.h>
#include <stdlib.h>

#include <switch.h>

/* getentropy() -> _getentropy_r. CPython uses it for os.urandom() and to seed
 * hash randomisation at startup; without it Py_Initialize fails. randomGet()
 * is libnx's CSPRNG, seeded from kernel entropy. */
int _getentropy_r(struct _reent *r, void *buf, size_t len)
{
    if (len > 256) {           /* getentropy() contract; CPython chunks to 256 */
        r->_errno = EIO;
        return -1;
    }
    randomGet(buf, len);
    return 0;
}

/* posix_memalign() is declared by newlib but not implemented; Ren'Py's video
 * decoder (src/ffmedia.c) uses it. newlib's memalign() memory is released
 * with free(), as posix_memalign() requires. */
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return EINVAL;
    void *p = memalign(alignment, size);
    if (!p && size)
        return ENOMEM;
    *memptr = p;
    return 0;
}
