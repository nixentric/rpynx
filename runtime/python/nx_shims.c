/* newlib system hooks that libnx leaves unimplemented but CPython needs. */
#include <errno.h>
#include <reent.h>

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
