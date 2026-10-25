#!/bin/bash
# Build CPython for the Nintendo Switch. Runs inside the build container
# (scripts/build.sh python). Steps:
#   1. host CPython 3.12.8 (the cross build needs an identical build interpreter)
#   2. patched source tree + configure for aarch64-none-elf (Horizon), every
#      extension module built in statically (no dlopen on Horizon)
#   3. libpython3.12.a, headers, and the stdlib compiled to .pyc in a zip
# Output: build/python/dist/{include,lib/libpython3.12.a,lib/python312.zip}
set -euo pipefail

PYVER=3.12.8
SHA256=c909157bb25ec114e5869124cc2a9c4a4d4c1e957ca4ff553f1edc692101154e
TOP=$(pwd)
B=$TOP/build/python
HERE=$TOP/runtime/python
JOBS=${JOBS:-4}
# Fixed __DATE__/__TIME__ in sys.version (CPython 3.12.8 release day) so
# rebuilds are byte-identical.
export SOURCE_DATE_EPOCH=1733270400

mkdir -p "$B/dl"
TARBALL=$B/dl/Python-$PYVER.tar.xz
[ -f "$TARBALL" ] || curl -fL -o "$TARBALL" "https://www.python.org/ftp/python/$PYVER/Python-$PYVER.tar.xz"
echo "$SHA256  $TARBALL" | sha256sum -c -

# 1. Host interpreter (cached).
HOSTPY=$B/host/bin/python3.12
if [ ! -x "$HOSTPY" ]; then
    rm -rf "$B/host-src" && mkdir -p "$B/host-src"
    tar -xf "$TARBALL" -C "$B/host-src"
    # In a clean environment: devkitPro's Makefile rules export to every recipe,
    # `make python` included, the Switch compiler as CC (configure: "cannot run C
    # compiled programs") and the loader's folders as VPATH (the bootstrap
    # interpreter then looks for its stdlib there). First seen on a clean CI checkout.
    host() { env -i PATH=/usr/local/bin:/usr/bin:/bin HOME="${HOME:-/root}" SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" "$@"; }
    (cd "$B/host-src/Python-$PYVER" &&
        host ./configure --prefix="$B/host" --without-ensurepip >"$B/host-configure.log" &&
        host make -j"$JOBS" >"$B/host-make.log" 2>&1 &&
        host make install >"$B/host-install.log" 2>&1)
fi
"$HOSTPY" -c 'import sys, zlib; assert sys.version.startswith("'$PYVER'")'

# 2. Patched source.
SRC=$B/src/Python-$PYVER
rm -rf "$B/src" && mkdir -p "$B/src"
tar -xf "$TARBALL" -C "$B/src"
for p in "$HERE"/patches/*.diff; do
    patch -d "$SRC" -p1 --no-backup-if-mismatch -s <"$p"
done

# Toolchain environment for libnx (devkitPro's own portlibs settings), plus
# the libnx link spec so configure's link tests see the real libc/libnx.
source "$DEVKITPRO/switchvars.sh"
export LDFLAGS="$LDFLAGS -specs=$DEVKITPRO/libnx/switch.specs"
export CONFIG_SITE=$HERE/config.site
export MODULE_BUILDTYPE=static
export PKG_CONFIG=aarch64-none-elf-pkg-config

OBJ=$B/obj
rm -rf "$OBJ" && mkdir -p "$OBJ"
cd "$OBJ"
"$SRC/configure" \
    --host=aarch64-none-elf --build="$("$SRC/config.guess")" \
    --with-build-python="$HOSTPY" \
    --prefix=/ \
    --disable-shared --disable-ipv6 --disable-test-modules \
    --without-ensurepip --without-pymalloc-debug \
    >"$B/configure.log" 2>&1 || { tail -40 "$B/configure.log"; exit 1; }

# newlib has no SSIZE_MAX; ssize_t is long on AArch64. pyconfig.h is the one
# header every CPython and embedder translation unit includes first.
cat >>pyconfig.h <<'EOF2'

/* Horizon (newlib): added by runtime/python/build.sh */
#ifndef SSIZE_MAX
#define SSIZE_MAX __LONG_MAX__
#endif
EOF2

if [ "${CONFIGURE_ONLY:-}" = 1 ]; then
    grep -E "^checking for stdlib extension module|^checking for .*module" "$B/configure.log" || true
    exit 0
fi

# 3. Static library, headers, compiled stdlib.
# libmpdec, expat and HACL SHA-2 are separate archives that python's own link
# line adds; fold them into libpython so embedders link one library.
HELPERS="Modules/_decimal/libmpdec/libmpdec.a Modules/expat/libexpat.a Modules/_hacl/libHacl_Hash_SHA2.a"
make -j"$JOBS" libpython3.12.a $HELPERS >"$B/make.log" 2>&1 || { grep -E -A3 "error:|\*\*\*" "$B/make.log" | head -80; exit 1; }

D=$B/dist
rm -rf "$D" && mkdir -p "$D/include/python3.12" "$D/lib"
{
    echo "create $D/lib/libpython3.12.a"
    for a in libpython3.12.a $HELPERS; do echo "addlib $a"; done
    echo save; echo end
} | aarch64-none-elf-ar -M
cp -R "$SRC/Include/." "$D/include/python3.12/"
cp pyconfig.h "$D/include/python3.12/"
"$HOSTPY" "$HERE/pack_stdlib.py" "$SRC/Lib" "$D/lib/python312.zip"
ls -l "$D/lib"
