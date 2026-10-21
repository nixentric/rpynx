#!/bin/sh
# Fetch LVGL (MIT), pinned and checksummed, into build/lvgl/lvgl, the way
# runtime/python/build.sh fetches CPython. The release tarball is 111 MB
# (tests, docs, demos); only the library and its licence are extracted.
# Runs on macOS (shasum) and in the build container (sha256sum).
set -eu
cd "$(dirname "$0")/../.."
VER=9.6.0
SHA256=b20ee3acc1bba13c62d854f9ebd62e4c51e0b443b1e0225892e86442defa84df
D=build/lvgl
[ "$(cat $D/lvgl/.version 2>/dev/null)" = "$VER" ] && exit 0
T=$D/dl/lvgl-$VER.tar.gz
mkdir -p $D/dl
[ -f "$T" ] || curl -fL -o "$T" "https://github.com/lvgl/lvgl/archive/refs/tags/v$VER.tar.gz"
if command -v sha256sum >/dev/null; then
    echo "$SHA256  $T" | sha256sum -c -
else
    echo "$SHA256  $T" | shasum -a 256 -c -
fi
rm -rf $D/lvgl && mkdir -p $D/lvgl
tar -xzf "$T" -C $D/lvgl --strip-components=1 \
    lvgl-$VER/src lvgl-$VER/include lvgl-$VER/lvgl.h lvgl-$VER/LICENCE.txt
echo "$VER" > $D/lvgl/.version
