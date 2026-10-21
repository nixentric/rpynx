#!/bin/sh
# Render the loader screens (app/ui) on the Mac with sample data:
# build/host/mockups/*.png. Same LVGL code and configuration as the Switch
# build; only the font differs (Arial Unicode here, the console's own font on
# the Switch). Covers come from the Ren'Py SDK if `scripts/build.sh renpy` has
# run, otherwise every game gets a placeholder. macOS only (sips).
set -eu
cd "$(dirname "$0")/.."
out=build/host/mockups
font="/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
mkdir -p "$out/covers"

third_party/lvgl/fetch.sh
make -s -j8 -f third_party/lvgl/Makefile OUT=build/host/lvgl CC=cc AR=ar \
    CFLAGS="-O2 -g -Wno-unused-function"
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iapp/ui -Iapp/launcher -Iapp/diagnostics -Iplatform/input -Iplatform/filesystem -Ithird_party/lvgl \
    -Ibuild/lvgl/lvgl tests/ui/mockups.c app/ui/ui*.c app/diagnostics/log.c platform/input/input.c \
    platform/filesystem/fs.c \
    build/host/lvgl/liblvgl.a -lm -o "$out/mockups"

sdk=build/renpy/renpy
# Covers as the importer will make them: 300x450 baseline JPEGs (portrait 2:3).
# Automatic ones are the middle of the game's main menu art (The Question, the
# tutorial); the other sample games borrow SDK backgrounds. Through PNG because
# sips keeps a progressive JPEG progressive, and the loader reads baseline only.
# Backdrops (Cinematic layout): the same art at 640x360, bd-<name>.jpg.
for img in "the_question/game/gui/main_menu.png" "tutorial/game/gui/main_menu.jpg" \
           "the_question/game/images/bg uni.jpg" \
           "the_question/game/images/bg club.jpg" "the_question/game/images/bg lecturehall.jpg" \
           "tutorial/game/images/bg cave.jpg" "tutorial/game/images/bg washington.jpg"; do
    [ -f "$sdk/$img" ] || continue
    name=$(echo "${img%/game/*}-${img##*/}" | tr -d ' ' | sed 's/\.[a-z]*$//')
    sips -s format png -z 360 640 "$sdk/$img" --out "$out/covers/bd-$name.png" >/dev/null
    sips -s format jpeg "$out/covers/bd-$name.png" --out "$out/covers/bd-$name.jpg" >/dev/null
    sips -s format png --resampleHeight 450 "$sdk/$img" --out "$out/covers/$name.png" >/dev/null
    sips -c 450 300 "$out/covers/$name.png" >/dev/null
    sips -s format jpeg "$out/covers/$name.png" --out "$out/covers/$name.jpg" >/dev/null
done
# An Album screenshot for the picture page (Change picture): 1280x720, as the
# Switch takes them. On the Switch the Album service decodes them.
if [ -f "$sdk/the_question/game/images/bg club.jpg" ]; then
    sips -s format png -z 720 1280 "$sdk/the_question/game/images/bg club.jpg" --out "$out/covers/shot.png" >/dev/null
    sips -s format jpeg "$out/covers/shot.png" --out "$out/covers/shot.jpg" >/dev/null
fi
# A custom cover, as a user would pick one: Sylvie over a background (ImageMagick).
q="$sdk/the_question/game/images"
if command -v magick >/dev/null && [ -f "$q/bg uni.jpg" ]; then
    magick "$q/bg uni.jpg" -resize x450 -gravity center -crop 300x450+0+0 +repage \
        \( "$q/sylvie blue smile.png" -resize x410 \) -gravity south -composite \
        -interlace none -quality 90 "$out/covers/custom.jpg"
fi

rm -f "$out"/*.bmp "$out"/*.png
# Also the redraw regression test: it fails if a settled screen keeps drawing.
rc=0
"$out/mockups" "$font" "$out" "$PWD/$out/covers" >/dev/null 2>"$out/mockups.err" || rc=1
grep -v "jd_restart\|jd_mcu_load\|NULL display" "$out/mockups.err" >&2 || true
for f in "$out"/*.bmp; do
    sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null && rm "$f"
done
ls "$out"/*.png
exit $rc
