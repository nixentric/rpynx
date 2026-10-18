#!/bin/sh
# Build dist/renpy-loader.nro in the pinned devkitPro container.
# Works on Apple Silicon (native arm64 image) and x86-64 Linux.
#   scripts/build.sh          build
#   scripts/build.sh clean    any other make target
set -eu
# devkitpro/devkita64 tag 20260219, multi-arch index digest (amd64 + arm64).
IMAGE="devkitpro/devkita64:20260219@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282"
cd "$(dirname "$0")/.."
exec docker run --rm -v "$PWD":/src -w /src "$IMAGE" make -j4 "$@"
