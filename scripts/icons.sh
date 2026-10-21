#!/bin/sh
# App icons from one square picture (macOS: ImageMagick, sips, iconutil).
#   scripts/icons.sh PICTURE
# - app/launcher/icon.jpg: the Homebrew Menu icon of renpy-loader.nro, a
#   256x256 baseline JPEG, square (the menu draws its own frame).
# - importer/icon.icns: macOS app icon on Apple's grid (macOS 11 and later):
#   1024x1024 canvas, an 824x824 body with continuous corners (a superellipse,
#   n = 5) centred in a 100 px margin, a soft shadow below; 16 to 512 pt at 1x
#   and 2x.
# - importer/icon.ico: Windows, the same body edge to edge, 16 to 256 px.
set -eu
cd "$(dirname "$0")/.."
[ $# -eq 1 ] || { echo "usage: $0 PICTURE" >&2; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

magick "$1" -resize 256x256^ -gravity center -extent 256x256 -strip -interlace none -quality 92 \
    app/launcher/icon.jpg

# The body: the picture cut to the superellipse, drawn at 2x and scaled down
# for smooth edges.
magick -size 1648x1648 xc:black -fx 'pow(abs(i-823.5)/824,5)+pow(abs(j-823.5)/824,5) <= 1' \
    -resize 824x824 "$tmp/mask.png"
magick "$1" -resize 824x824^ -gravity center -extent 824x824 "$tmp/mask.png" \
    -alpha off -compose copy-opacity -composite "$tmp/body.png"
magick -size 1024x1024 xc:none \
    \( "$tmp/body.png" -background black -shadow 30x10+0+10 \) -geometry +88+98 -composite \
    "$tmp/body.png" -geometry +100+100 -composite "$tmp/1024.png"

# Full RGBA: a 256-colour palette loses the soft alpha of the edges and the
# shadow (the corners turn black), for a .icns of about 3 MB either way.
png() {
    magick "$tmp/1024.png" -resize "$1x$1" -define png:compression-level=9 "$tmp/icon.iconset/$2.png"
}
mkdir "$tmp/icon.iconset"
for pt in 16 32 128 256 512; do
    png $pt icon_${pt}x$pt
    png $((pt * 2)) icon_${pt}x$pt@2x
done
iconutil -c icns "$tmp/icon.iconset" -o importer/icon.icns
magick "$tmp/body.png" -define icon:auto-resize=256,128,64,48,32,24,16 importer/icon.ico
ls -l app/launcher/icon.jpg importer/icon.icns importer/icon.ico
