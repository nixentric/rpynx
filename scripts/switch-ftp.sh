#!/bin/sh
# Transfer files to/from the Switch over Wi-Fi with sphaira's FTP server
# (anonymous; verified on hardware 2026-10-18 at port 5000). HOST is IP:PORT.
# Switch and Mac must be on the same network.
#
#   scripts/switch-ftp.sh HOST logs            fetch renpy-loader logs + self-test report
#   scripts/switch-ftp.sh HOST push-runtime    upload dist/renpy-runtime.nro (verified)
#   scripts/switch-ftp.sh HOST push-loader     upload dist/renpy-loader.nro (verified)
#   scripts/switch-ftp.sh HOST get PATH        fetch one file, PATH relative to the SD root
set -eu
cd "$(dirname "$0")/.."
host=$1 cmd=$2
base="ftp://$host/switch/renpy-loader"
out=build-test/switch-logs

fetch() {  # fetch REMOTE LOCAL; a missing file is reported, not fatal
    if curl -sf --connect-timeout 5 "$base/$1" -o "$2"; then echo "got $1"; else echo "missing $1"; rm -f "$2"; fi
}

push() {   # push LOCAL REMOTE_DIR/, then read it back and compare
    curl -sf --connect-timeout 5 --ftp-create-dirs -T "$1" "$base/$2/"
    curl -sf "$base/$2/$(basename "$1")" -o "$out/.verify"
    if cmp -s "$1" "$out/.verify"; then echo "uploaded and verified $2/$(basename "$1")"; else echo "VERIFY FAILED for $1"; exit 1; fi
    rm -f "$out/.verify"
}

mkdir -p "$out"
case $cmd in
logs)
    fetch logs/loader.log "$out/loader.log"
    fetch logs/runtime.log "$out/runtime.log"
    fetch cache/python-selftest.txt "$out/python-selftest.txt"
    fetch cache/python.txt "$out/cache-python.txt"
    for g in $(curl -sf --list-only "$base/logs/" | grep -v '\.log$' || true); do
        mkdir -p "$out/$g"
        for f in python.txt log.txt traceback.txt errors.txt; do fetch "logs/$g/$f" "$out/$g/$f"; done
    done ;;
push-runtime) push dist/renpy-runtime.nro runtimes ;;
push-loader)  push dist/renpy-loader.nro . ;;
get) curl -sf "ftp://$host/$3" -o "$out/$(basename "$3")" && echo "saved $out/$(basename "$3")" ;;
*) echo "unknown command $cmd"; exit 1 ;;
esac
