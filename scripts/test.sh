#!/bin/sh
# Host-side tests: the portable C core (input + filesystem) and the Python tools.
set -eu
cd "$(dirname "$0")/.."
out=build/host
mkdir -p "$out"
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined \
    -Iplatform/input -Iplatform/filesystem -Ithird_party/cjson \
    tests/unit/test_input.c platform/input/input.c platform/input/input_config.c \
    platform/filesystem/fs.c third_party/cjson/cJSON.c \
    -lm -o "$out/test_input"
"$out/test_input"

# Loader screens (app/ui) through LVGL on the host, under ASan/UBSan: renders
# the mockups and fails if a settled screen keeps redrawing or a tap misses
# its target (macOS: sips).
scripts/mockups.sh >/dev/null

# Importer: macOS system Python, standard library only.
/usr/bin/python3 -m unittest tests/unit/test_importer.py tests/unit/test_importer_gui.py

# The loader's FTP server on the host, under ASan/UBSan, driven by the importer's FTP code.
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Iplatform/transfer \
    tests/unit/ftpd_host.c platform/transfer/ftpd.c -o "$out/ftpd_host"
/usr/bin/python3 -m unittest tests/unit/test_ftpd.py

# Python-side tests need Docker and the host CPython from `scripts/build.sh python`.
if command -v docker >/dev/null && [ -x build/python/host/bin/python3.12 ]; then
    scripts/build.sh test-python
else
    echo "SKIPPED python tests (needs Docker and scripts/build.sh python)"
fi
