#!/bin/bash
# Desktop integration test: the official Ren'Py 8.5.3 Linux SDK, started
# through runtime/renpy/boot (switch_main.py + rsl_bridge.py) with a scripted
# _rslnx, plays "The Question" headless (Xvfb + Mesa llvmpipe, GLES2).
#
# horizon_sim.py makes the SDK's Python look like our Switch build (sys.platform
# "horizon", the same modules missing). This checks the boot script, the
# controller bridge and Ren'Py's behaviour.
# It does NOT run any Switch code: results are DESKTOP, not hardware.
#
# usage (host): tests/integration/renpy_desktop/run.sh
set -euo pipefail
cd "$(dirname "$0")/../../.."
SDKTB=build-renpy/dl/renpy-8.5.3-sdkarm.tar.bz2
[ -f "$SDKTB" ] || { echo "missing $SDKTB (run scripts/build.sh renpy)"; exit 1; }
docker image inspect rsl-test:2 >/dev/null 2>&1 || docker build -t rsl-test:2 docker/test

exec docker run --rm -v "$PWD":/src -w /src rsl-test:2 bash -c '
set -euo pipefail
T=build-test
[ -d $T/sdk ] || { mkdir -p $T/sdk && tar -xjf '"$SDKTB"' -C $T/sdk --strip-components=1; }
# Fresh copy of the game each run: Ren'"'"'Py may write .rpyc files next to it.
rm -rf $T/game $T/out && mkdir -p $T/game $T/out/saves
cp -R $T/sdk/the_question/. $T/game/
cp runtime/renpy/boot/*.py tests/integration/renpy_desktop/{_rslnx,scenario,horizon_sim}.py $T/sdk/
cp build-renpy/dist/renpy-base/renpy/common/_rsl_cursor.png $T/sdk/renpy/common/ 2>/dev/null || true
export RSL_LOGDIR=/src/$T/out SDL_AUDIODRIVER=dummy RENPY_RENDERER=gles2
set +e
timeout 300 xvfb-run -a -s "-screen 0 1280x720x24" \
    $T/sdk/lib/py3-linux-aarch64/python $T/sdk/horizon_sim.py $T/sdk/switch_main.py /src/$T/game run --savedir /src/$T/out/saves \
    >$T/out/stdout.txt 2>&1
rc=$?
set -e
grep -E "^(PASS|FAIL)" $T/out/stdout.txt || true
echo "exit status $rc"
[ -f $T/out/scenario-report.txt ] && tail -1 $T/out/scenario-report.txt
exit $rc
'
