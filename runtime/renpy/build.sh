#!/bin/bash
# Build the Ren'Py 8.5.3 engine for the Switch. Runs inside the build container
# (scripts/build.sh renpy) after `scripts/build.sh python`. Steps:
#   1. official Ren'Py 8.5.3 source tarball and Cython 3.2.4 sdist (both sha256-pinned)
#   2. Cython -> C with Ren'Py's own `setup.py generate` (host CPython 3.12.8)
#   3. native modules -> build/renpy/dist/lib/librenpy.a (+ inittab)
#   4. Ren'Py's Python package + common/ with the compatibility overlay,
#      compiled to .pyc -> build/renpy/dist/renpy-base/
set -euo pipefail

# Official release source (sha256 from https://www.renpy.org/dl/8.5.3/checksums.txt,
# PGP-signed). It carries renpy/vc_version.py (8.5.3.26051504), which a git
# checkout lacks.
RENPY_VER=8.5.3
RENPY_SHA256=4fd3d7bca7349b0d182830d3cdcf65940fa32e66be1aea0b3690e048be6282a5
# Official ARM SDK: only its precompiled renpy/common/*.rpyc are used, and
# only after their digests match our source (stage_engine.py).
SDK_SHA256=0579782517f203ba3535dcc2dab54e34bfc318f2f2a7510a5130b6f809f901f6
CYTHON_VER=3.2.4
CYTHON_SHA256=84226ecd313b233da27dc2eb3601b4f222b8209c3a7216d8733b031da1dc64e6

TOP=$(pwd)
B=$TOP/build/renpy
HERE=$TOP/runtime/renpy
HOSTPY=$TOP/build/python/host/bin/python3.12
PYINC=$TOP/build/python/dist/include/python3.12
export JOBS=${JOBS:-4}
[ -x "$HOSTPY" ] || { echo "run scripts/build.sh python first"; exit 1; }

mkdir -p "$B/dl"

# 1. Sources.
RENPYTB=$B/dl/renpy-$RENPY_VER-source.tar.bz2
[ -f "$RENPYTB" ] || curl -fL -o "$RENPYTB" "https://www.renpy.org/dl/$RENPY_VER/renpy-$RENPY_VER-source.tar.bz2"
echo "$RENPY_SHA256  $RENPYTB" | sha256sum -c -
if [ ! -d "$B/renpy" ]; then
    tar -xjf "$RENPYTB" -C "$B"
    mv "$B/renpy-$RENPY_VER-source" "$B/renpy"
fi
# Upstream modifications (documented in runtime/renpy/README.md), applied once.
for p in "$HERE"/patches/*.diff; do
    stamp="$B/renpy/.applied-$(basename "$p")"
    [ -f "$stamp" ] || { patch -d "$B/renpy" -p1 --no-backup-if-mismatch -s <"$p" && touch "$stamp"; }
done

CYTGZ=$B/dl/cython-$CYTHON_VER.tar.gz
[ -f "$CYTGZ" ] || curl -fL -o "$CYTGZ" "https://files.pythonhosted.org/packages/source/c/cython/cython-$CYTHON_VER.tar.gz"
echo "$CYTHON_SHA256  $CYTGZ" | sha256sum -c -
CY=$B/cython-$CYTHON_VER
[ -d "$CY" ] || tar -xzf "$CYTGZ" -C "$B"

# 2. Cython -> C (cached in renpy/tmp/gen3; delete it to regenerate).
# setuplib runs RENPY_CYTHON as one executable path, and its thread pool drops
# launch errors, so use a wrapper and check the output afterwards.
GEN=$B/renpy/tmp/gen3
printf '#!/bin/sh\nexec "%s" "%s/cython.py" "$@"\n' "$HOSTPY" "$CY" >"$B/cython"
chmod +x "$B/cython"
if [ ! -f "$GEN/renpy.text.bidi.c" ]; then
    (cd "$B/renpy" &&
        PYTHONPATH="$HERE/hoststub" RENPY_CYTHON="$B/cython" RENPY_REGENERATE_CYTHON=1 \
            "$HOSTPY" setup.py generate >"$B/generate.log" 2>&1) || { tail -40 "$B/generate.log"; exit 1; }
    [ -f "$GEN/renpy.text.bidi.c" ] || { grep -B2 -A8 -E "Error|error" "$B/generate.log" | head -60; exit 1; }
fi

# 3. Native modules.
source "$DEVKITPRO/switchvars.sh"
PKGS="sdl2 SDL2_image harfbuzz freetype2 fribidi libavformat libavcodec libavutil libswresample libswscale libpng libjpeg zlib"
export CC=aarch64-none-elf-gcc AR=aarch64-none-elf-ar
export CFLAGS="$CFLAGS $CPPFLAGS -I$PYINC $(aarch64-none-elf-pkg-config --cflags $PKGS)"
D=$B/dist
mkdir -p "$D/lib"
"$HOSTPY" "$HERE/build_modules.py" "$B/renpy" "$GEN" "$B/obj" 2>"$B/modules.log" || { tail -80 "$B/modules.log"; exit 1; }
cp "$B/obj/librenpy.a" "$D/lib/"
aarch64-none-elf-pkg-config --libs $PKGS >"$D/lib/librenpy.deps"

# 4. Python side (pure-Python engine, common/ scripts, compatibility overlay).
SDKTB=$B/dl/renpy-$RENPY_VER-sdkarm.tar.bz2
[ -f "$SDKTB" ] || curl -fL -o "$SDKTB" "https://www.renpy.org/dl/$RENPY_VER/renpy-$RENPY_VER-sdkarm.tar.bz2"
echo "$SDK_SHA256  $SDKTB" | sha256sum -c -
SDKCOMMON=$B/sdk/renpy-$RENPY_VER-sdkarm/renpy/common
[ -d "$SDKCOMMON" ] || { mkdir -p "$B/sdk" && tar -xjf "$SDKTB" -C "$B/sdk" "renpy-$RENPY_VER-sdkarm/renpy/common"; }
"$HOSTPY" "$HERE/stage_engine.py" "$B/renpy" "$SDKCOMMON" "$D/renpy-base" \
    "$TOP/runtime/compatibility" "$HERE/boot"
"$HOSTPY" "$HERE/make_cursor.py" "$D/renpy-base/renpy/common/_rsl_cursor.png"

# Third-party pure-Python packages Ren'Py imports (ecdsa and six at start-up;
# rsa, requests and its dependencies for updates/downloads). Taken as
# compiled .pyc from the verified official SDK, the set Ren'Py 8.5.3 is
# tested with (CPython 3.12 bytecode). Licences: see runtime/renpy/README.md.
SDKLIB="renpy-$RENPY_VER-sdkarm/lib/python3.12"
EXTRA="ecdsa six.pyc rsa pyasn1 requests urllib3 idna chardet certifi socks.pyc"
rm -rf "$B/sdklib" && mkdir -p "$B/sdklib"
tar -xjf "$SDKTB" -C "$B/sdklib" $(for m in $EXTRA; do echo "$SDKLIB/$m"; done)
for m in $EXTRA; do cp -R "$B/sdklib/$SDKLIB/$m" "$D/renpy-base/"; done
find "$D/renpy-base" -name "test_*.pyc" -path "*/ecdsa/*" -delete
echo "added SDK packages: $EXTRA"

# The SDK's fonts (Source Han Sans Lite for Chinese/Japanese/Korean, Vazirmatn; SIL OFL 1.1).
# Games copied straight from the SDK (The Question, the Tutorial) add renpy_base/sdk-fonts
# to their search path; without it their preferences screen stopped the game (run 25).
tar -xjf "$SDKTB" -C "$B/sdklib" "renpy-$RENPY_VER-sdkarm/sdk-fonts"
rm -rf "$D/renpy-base/sdk-fonts" && cp -R "$B/sdklib/renpy-$RENPY_VER-sdkarm/sdk-fonts" "$D/renpy-base/"
ls -l "$D/lib"
