#!/bin/sh
# Fetch unrpyc (MIT), pinned and checksummed, into build/unrpyc/unrpyc, the way
# third_party/lvgl/fetch.sh fetches LVGL. importer/rsl_import.py --decompile runs
# build/unrpyc/unrpyc/unrpyc.py on its own Python (3.9+). Only the decompiler and its
# licence are extracted, not the test cases.
set -eu
cd "$(dirname "$0")/../.."
VER=2.0.4
SHA256=bf2b176a82747bb98ece3a20c33872c9d1e448072096626ee084e805c7c8177d
D=build/unrpyc
[ "$(cat $D/unrpyc/.version 2>/dev/null)" = "$VER" ] && exit 0
T=$D/dl/unrpyc-$VER.tar.gz
mkdir -p $D/dl
[ -f "$T" ] || curl -fL -o "$T" "https://github.com/CensoredUsername/unrpyc/archive/refs/tags/v$VER.tar.gz"
if command -v sha256sum >/dev/null; then
    echo "$SHA256  $T" | sha256sum -c -
else
    echo "$SHA256  $T" | shasum -a 256 -c -
fi
rm -rf $D/unrpyc && mkdir -p $D/unrpyc
tar -xzf "$T" -C $D/unrpyc --strip-components=1 \
    unrpyc-$VER/unrpyc.py unrpyc-$VER/deobfuscate.py unrpyc-$VER/decompiler unrpyc-$VER/LICENSE
echo "$VER" > $D/unrpyc/.version
