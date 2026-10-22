"""What would load the Switch: image sizes, video probing (ffprobe), --optimize re-encoding,
and the cover the loader shows."""
import io
import json
import os
import re
import struct
import tempfile

from .common import examples, IMAGE_EXTS, read_text, Refused, VIDEO_EXTS
from .rpa import archive_media, extract_member, read_member
from .tools import FFMPEG, FFPROBE, find_tool, run


# What keeps the Switch smooth and cool. Video is decoded in software on a ~1 GHz
# Cortex-A57, and the handheld screen is 1280x720 (1920x1080 docked).
SWITCH_SCREEN = (1280, 720)
DOCKED_SCREEN = (1920, 1080)
MAX_VIDEO_FPS = 30
MAX_TEXTURE = 4096                 # GL_MAX_TEXTURE_SIZE on the Switch (Ren'Py log, run 13)
HEAVY_CODECS = ("hevc", "av1")     # the most expensive to decode in software

# --optimize re-encodes in the same codec family (the game names the file), at
# constant quality so it stays sharp. VP9 tiles let the Switch decode on several cores.
ENCODERS = {
    "vp9": ["-c:v", "libvpx-vp9", "-crf", "31", "-b:v", "0", "-deadline", "good", "-cpu-used", "2",
            "-row-mt", "1", "-tile-columns", "2"],
    "vp8": ["-c:v", "libvpx", "-crf", "8", "-b:v", "8M", "-deadline", "good", "-cpu-used", "1"],
    "h264": ["-c:v", "libx264", "-crf", "20", "-preset", "slow", "-profile:v", "high"],
}


def probe_video(ffprobe, path):
    """{codec, width, height, fps, alpha} of the first video stream, or None."""
    r = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
             "stream=codec_name,width,height,avg_frame_rate,r_frame_rate:stream_tags=alpha_mode",
             "-of", "json", path])
    try:
        st = json.loads(r.stdout)["streams"][0]
        rate = st.get("avg_frame_rate", "0/0")
        if rate.endswith("/0") or rate.startswith("0/"):
            rate = st.get("r_frame_rate", "0/1")
        num, den = rate.split("/")
        return {"codec": st["codec_name"], "width": int(st["width"]), "height": int(st["height"]),
                "fps": float(num) / float(den), "alpha": st.get("tags", {}).get("alpha_mode") == "1"}
    except (ValueError, KeyError, IndexError, ZeroDivisionError):
        return None


def image_size(path):
    """(width, height) from a PNG, JPEG or WebP header (a path or a binary file), or None."""
    if not isinstance(path, str):
        return _image_size(path)
    with open(path, "rb") as f:
        return _image_size(f)


def _image_size(f):
    head = f.read(32)
    if head[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", head[16:24])
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        kind = head[12:16]
        if kind == b"VP8X":
            return int.from_bytes(head[24:27], "little") + 1, int.from_bytes(head[27:30], "little") + 1
        if kind == b"VP8L":
            bits = int.from_bytes(head[21:25], "little")
            return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
        if kind == b"VP8 ":
            w, h = struct.unpack("<HH", head[26:30])
            return w & 0x3FFF, h & 0x3FFF
        return None
    if head[:2] != b"\xff\xd8":
        return None
    f.seek(2)
    while True:   # JPEG: walk the segments to the frame header (SOFn)
        marker = f.read(2)
        if len(marker) < 2 or marker[0] != 0xFF:
            return None
        length = struct.unpack(">H", f.read(2))[0]
        if 0xC0 <= marker[1] <= 0xCF and marker[1] not in (0xC4, 0xC8, 0xCC):
            h, w = struct.unpack(">xHH", f.read(5))
            return w, h
        f.seek(length - 2, 1)


def performance(gamedir, files, warnings, optimize, workdir=None):
    """What would load the Switch. Returns (remaining issues, {video: codec} to re-encode,
    {archived video: extracted copy} to export loose, asset coverage, the videos that
    --optimize would re-encode if it were on)."""
    issues, convert, extra = [], {}, {}
    archived, locked = archive_media(gamedir, files, workdir, warnings)
    images = [(r, os.path.join(gamedir, r)) for r, _ in files if r.lower().endswith(IMAGE_EXTS)]
    images += [(r + " (in %s)" % m[0], io.BytesIO(read_member(gamedir, m, 256 << 10)))
               for r, m in sorted(archived.items()) if r.lower().endswith(IMAGE_EXTS)]
    big_images = []
    for label, source in images:
        try:
            size = image_size(source)
        except (OSError, struct.error):
            size = None
        if size and max(size) > MAX_TEXTURE:
            big_images.append("%s %dx%d" % (label, size[0], size[1]))
    if big_images:
        issues.append("%d images exceed the Switch GPU's %dx%d texture limit and may not display "
                      "(%s); shrink them by hand, the game's layout depends on their size"
                      % (len(big_images), MAX_TEXTURE, MAX_TEXTURE, examples(big_images, 2)))
    videos = [(r, os.path.join(gamedir, r), None) for r, _ in files if r.lower().endswith(VIDEO_EXTS)]
    videos += [(r, None, m) for r, m in sorted(archived.items()) if r.lower().endswith(VIDEO_EXTS)]
    coverage = {"loose_media": sum(1 for r, _ in files if r.lower().endswith(VIDEO_EXTS + IMAGE_EXTS)),
                "archived_media": len(archived), "locked_archives": locked}
    if locked:
        issues.append("%d archives could not be checked (%s): their images and videos may not suit "
                      "the Switch" % (len(locked), examples(locked, 2)))
    ffprobe = find_tool(FFPROBE)
    if videos and not ffprobe:
        warnings.append("%d videos not checked: install FFmpeg (ffprobe) or choose its folder "
                        "(--tools) to check their size and frame rate for the Switch" % len(videos))
        return issues, convert, extra, coverage, []
    fast, large, huge, heavy, unfixable, would_fix = [], [], [], [], [], []
    for rel, path, member in videos:
        if member:   # probe a temporary copy; keep it only if it gets re-encoded
            path = os.path.join(workdir or tempfile.gettempdir(), "rpa-media", *rel.split("/"))
            extract_member(gamedir, member, path)
        v = probe_video(ffprobe, path)
        if not v:
            continue
        can_fix = v["fps"] > MAX_VIDEO_FPS + 0.5 and v["codec"] in ENCODERS and not v["alpha"]
        fixable = optimize and can_fix
        # Label what the export will hold: a re-encoded video is MAX_VIDEO_FPS.
        fps = "%d fps (was %.0f)" % (MAX_VIDEO_FPS, v["fps"]) if fixable else "%.0f fps" % v["fps"]
        label = "%s%s %dx%d %s %s" % (rel, " (in %s)" % member[0] if member else "",
                                      v["width"], v["height"], v["codec"], fps)
        if member and fixable:
            extra[rel] = path
        elif member:
            os.remove(path)
        if v["fps"] > MAX_VIDEO_FPS + 0.5:
            if fixable:
                convert[rel] = v["codec"]
            else:
                (unfixable if optimize else fast).append(label)
            if can_fix and not optimize:
                would_fix.append(rel)
        if v["width"] * v["height"] > DOCKED_SCREEN[0] * DOCKED_SCREEN[1]:
            huge.append(label)
        elif v["width"] * v["height"] > SWITCH_SCREEN[0] * SWITCH_SCREEN[1]:
            large.append(label)
        if v["codec"] in HEAVY_CODECS:
            heavy.append(label)
    if fast:
        issues.append("%d videos above %d fps (%s): they cost CPU and heat; --optimize re-encodes "
                      "them at %d fps, same size" % (len(fast), MAX_VIDEO_FPS, examples(fast, 2), MAX_VIDEO_FPS))
    if unfixable:
        issues.append("%d videos above %d fps that --optimize cannot re-encode (alpha or codec): %s"
                      % (len(unfixable), MAX_VIDEO_FPS, examples(unfixable, 2)))
    if huge:
        issues.append("%d videos above 1920x1080 (%s): too heavy to decode smoothly on the Switch; "
                      "not shrunk automatically, because the game may show them at their own size"
                      % (len(huge), examples(huge, 2)))
    if large:
        issues.append("%d videos above the 1280x720 handheld screen (%s): about twice the decoding "
                      "work of 720p" % (len(large), examples(large, 2)))
    if heavy:
        issues.append("%d videos in HEVC/AV1 (%s): the most expensive to decode in software"
                      % (len(heavy), examples(heavy, 2)))
    if convert:
        warnings.append("--optimize re-encodes %d videos at %d fps (same size, constant quality): %s"
                        % (len(convert), MAX_VIDEO_FPS, examples(convert, 3)))
    if extra:
        warnings.append("%d of them come from archives and are exported as loose files, which Ren'Py "
                        "loads before the archive copy" % len(extra))
    return issues, convert, extra, coverage, would_fix


def reencode(source, target, codec):
    """Re-encode a video at MAX_VIDEO_FPS with the same codec family and size; audio is
    copied. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return "FFmpeg not found"
    tmp = target + ".part" + os.path.splitext(target)[1]
    r = run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-map", "0:a?",
             "-vf", "fps=%d" % MAX_VIDEO_FPS] + ENCODERS[codec] + ["-c:a", "copy", tmp])
    if r.returncode != 0:
        if os.path.exists(tmp):
            os.remove(tmp)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(tmp, target)
    return None


# The loader's cover (docs/LOADER_UI.md "Covers"): a baseline JPEG of this size, next
# to game/, named by the manifest's "cover".
ART = {"cover": (300, 450)}
MENU_ART = "gui/main_menu.png"     # Ren'Py's template default for gui.main_menu_background


def convert_art(source, target, size):
    """One picture as the loader reads it: a baseline JPEG of exactly size, cut to its
    aspect from the middle (TJpgDec on the Switch reads no progressive JPEG). Any image
    FFmpeg reads will do. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return "FFmpeg is needed for covers; install it or choose its folder (--tools)"
    w, h = size
    part = target + ".part"
    r = run([ffmpeg, "-v", "error", "-y", "-i", source, "-frames:v", "1", "-vf",
             "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d" % (w, h, w, h),
             "-pix_fmt", "yuvj420p", "-q:v", "2", "-f", "mjpeg", part])
    if r.returncode != 0 or not os.path.isfile(part):
        if os.path.exists(part):
            os.remove(part)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(part, target)
    return None


def menu_art(gamedir, files, workdir):
    """The game's main menu picture as a local file: gui.main_menu_background from gui.rpy
    when it is a plain string naming an image, else gui/main_menu.png; loose or inside a
    standard .rpa. Returns (name, path) or None."""
    text = read_text(os.path.join(gamedir, "gui.rpy")) or ""
    m = re.search(r"^\s*(?:define\s+)?gui\.main_menu_background\s*=\s*[uU]?(['\"])([^'\"\\]+)\1\s*(?:#.*)?$",
                  text, re.M)
    names = [m.group(2)] if m and m.group(2).lower().endswith(IMAGE_EXTS) else []
    loose = {r.lower(): r for r, _ in files}
    archived = {r.lower(): (r, member) for r, member in archive_media(gamedir, files, workdir, [])[0].items()}
    for name in names + [MENU_ART]:
        key = name.lower()
        if key in loose:
            return loose[key], os.path.join(gamedir, *loose[key].split("/"))
        if key in archived:
            rel, member = archived[key]
            path = os.path.join(workdir, "art", "menu" + os.path.splitext(rel)[1])
            extract_member(gamedir, member, path)
            return "%s (in %s)" % (rel, member[0]), path
    return None


def make_art(manifest, gamedir, files, workdir, cover=None):
    """The cover that export() writes next to game/, made in workdir. cover is a picture
    the user chose (marked "cover_custom" in the manifest, which --replace keeps); without
    one it comes from the game's menu art. Names it in the manifest and returns
    {key: path}. A chosen picture that cannot be converted is refused; the game's own is a
    warning, and without one the loader shows a placeholder."""
    chosen = {"cover": cover}
    os.makedirs(os.path.join(workdir, "art"), exist_ok=True)
    menu = None if cover else menu_art(gamedir, files, workdir)
    art, failed = {}, {}
    for key, size in ART.items():
        source = chosen[key] or (menu and menu[1])
        if not source:
            continue
        path = os.path.join(workdir, "art", key + ".jpg")
        error = convert_art(source, path, size)
        if error and chosen[key]:
            raise Refused("%s %s: %s" % (key, chosen[key], error))
        if error:
            failed[key] = error
        else:
            art[key] = path
    for key in ART:
        manifest.pop(key, None)
        manifest.pop(key + "_custom", None)
        if key in art:
            manifest[key] = key + ".jpg"
            if chosen[key]:
                manifest[key + "_custom"] = True
    if failed:
        manifest["warnings"].append("no %s from %s: %s" % (" or ".join(failed), menu[0],
                                                           "; ".join(sorted(set(failed.values())))))
    return art
