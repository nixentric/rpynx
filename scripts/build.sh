#!/bin/sh
# Run the Switch build in the pinned container (docker/Dockerfile).
# Works on Apple Silicon (native arm64 image) and x86-64 Linux.
#   scripts/build.sh            build dist/renpy-loader.nro
#   scripts/build.sh python     build static CPython for Switch (runtime/python)
#   scripts/build.sh renpy      build Ren'Py 8.5.3 for Switch (runtime/renpy)
#   scripts/build.sh runtime    link dist/renpy-runtime.nro
#   scripts/build.sh clean      any other make target
set -eu
cd "$(dirname "$0")/.."
hash=$( (shasum -a 256 2>/dev/null || sha256sum) < docker/Dockerfile | cut -c1-12)
IMAGE="rsl-build:$hash"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" docker
exec docker run --rm -v "$PWD":/src -w /src "$IMAGE" make -j4 "$@"
