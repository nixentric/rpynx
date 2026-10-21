#!/bin/bash
# Ren'Py compat fixes in rsl_bridge.py, on synthetic games (no game content):
#  langcompat  a 7.5.3 game's gui value set in `init python` survives Ren'Py
#              8.5.3's language change (plain 8.5.3 resets it; hardware run 13)
#  input       renpy.input() with the full Switch keyboard (emulated by the
#  inputname   _rslnx mock): it opens once, its text replaces the field's, and
#              nothing moves on until the player presses a button (runs 14-16)
#  cards       on a screen of cards the first is selected at once; D-pad + A picks
#              another (Forbidden Fantasy's outfit screen, run 16)
#  pause       a pause with only the quick menu up: A continues it
#  overlays    Forbidden Fantasy's debug_screen + overlay quick menu: Back is never
#              selected for the player (run 18)
#  menufocus   the main menu's first button is selected and the cursor hidden (runs 14-15)
# Needs build/test/sdk from tests/integration/renpy_desktop/run.sh.
# usage (host): tests/integration/renpy_desktop/compat.sh
set -euo pipefail
cd "$(dirname "$0")/../../.."
exec docker run --rm --network none -v "$PWD":/src -w /src rsl-test:2 bash -c '
set -euo pipefail
S=build/test/sdk P=build/test/sdk/lib/py3-linux-aarch64/python O=build/test/compat-out
rm -rf $O && mkdir -p $O && cp -R tests/integration/renpy_desktop/compat-games/. $O/
cp runtime/renpy/boot/*.py tests/integration/renpy_desktop/{_rslnx,scenario,horizon_sim}.py $S/
export SDL_AUDIODRIVER=dummy RENPY_RENDERER=gles2 RSL_SCENARIO=0
run() {  # run <result name> <game> plain|bridge [timeout]
    rm -f $O/$1.txt $O/$1.kbd
    local t=${4:-60}
    if [ $3 = plain ]; then set -- $1 $2 $S/renpy.py /src/$O/$2 run
    else set -- $1 $2 $S/horizon_sim.py $S/switch_main.py /src/$O/$2 run --savedir /src/$O/$2-saves; fi
    RSL_KEYBOARD_LOG=/src/$O/$1.kbd RSL_LOGDIR=/src/$O/$2-logs RSL_RESULT=/src/$O/$1.txt \
        timeout $t xvfb-run -a $P "${@:3}" >/dev/null 2>&1 || true
    printf "%s keyboard opened %s time(s)" "$(cat $O/$1.txt 2>/dev/null || echo "(no result)")" \
        "$(grep -c open $O/$1.kbd 2>/dev/null || echo 0)"
}
fails=0
check() {  # check <label> <text> <must contain>
    case "$2" in *"$3"*) echo "PASS $1: $2";; *) echo "FAIL $1: $2"; fails=$((fails+1));; esac
}
check "plain 8.5.3 resets the 7.x gui value (repro)" "$(run lang-plain langcompat plain)" "after=0"
check "bridge keeps the 7.x gui value" "$(run lang-bridge langcompat bridge)" "before=40 after=40"
check "renpy.input: typed text stays, nothing moves on by itself" \
    "$(RSL_FAKE_KEYBOARD=type:Nina run input-wait input bridge 12)" "(no result) keyboard opened 1 time"
check "renpy.input: A on a screen without buttons finishes it" \
    "$(RSL_FAKE_KEYBOARD=type:Nina RSL_TAPS=3:confirm run input-a input bridge)" "name='"'"'Nina'"'"' keyboard opened 1 time"
check "name screen: typed text stays, nothing moves on by itself" \
    "$(RSL_FAKE_KEYBOARD=type:Nina run name-wait inputname bridge 12)" "(no result) keyboard opened 1 time"
check "name screen: A with nothing selected reopens the keyboard; D-pad + A on OK" \
    "$(RSL_FAKE_KEYBOARD=type:Nina RSL_TAPS=2:confirm,4:down,5:confirm run name-ok inputname bridge)" "name='"'"'Nina'"'"' keyboard opened 2 time"
check "name screen: Cancel keeps the default" \
    "$(RSL_FAKE_KEYBOARD=cancel RSL_TAPS=3:down,4:confirm run name-cancel inputname bridge)" "name='"'"'Syd'"'"' keyboard opened 1 time"
check "outfit cards: the first card is selected at once, A picks it" \
    "$(RSL_TAPS=3:confirm run cards-a cards bridge)" "picked='"'"'Elven Archer'"'"'"
check "outfit cards: D-pad right then A picks the second card" \
    "$(RSL_TAPS=2:right,4:confirm run cards cards bridge)" "picked='"'"'Hermes Blessing'"'"'"
check "a pause with only the quick menu: A continues, no Back" \
    "$(RSL_TAPS=3:confirm run pause pause bridge)" "continued"
check "FF-like overlays: A advances dialogue, never the quick menu Back" \
    "$(RSL_PART=dialogue RSL_TAPS=4:confirm run ov-dialogue overlays bridge)" "advanced"
check "FF-like overlays: A picks the first card, not the quick menu Back" \
    "$(RSL_PART=cards RSL_TAPS=3:confirm run ov-cards overlays bridge)" "picked Elven Archer"
menu_plain=$(run menu-plain menufocus plain)
menu_bridge=$(run menu-bridge menufocus bridge)
check "plain 8.5.3 main menu starts unfocused, mouse shown (repro)" "$menu_plain" "focused=None mouse=True"
check "bridge selects the first main menu button, cursor hidden" "$menu_bridge" "focused='"'"'First'"'"' mouse=False"
echo "$fails failed"
exit $fails
'
