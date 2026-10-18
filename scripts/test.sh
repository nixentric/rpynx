#!/bin/sh
# Host-side tests: the portable C core (input + filesystem) and the Python tools.
set -eu
cd "$(dirname "$0")/.."
out=build-host
mkdir -p "$out"
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined \
    -Iplatform/input -Iplatform/filesystem -Ithird_party/cjson \
    tests/unit/test_input.c platform/input/input.c platform/input/input_config.c \
    platform/filesystem/fs.c third_party/cjson/cJSON.c \
    -lm -o "$out/test_input"
"$out/test_input"
