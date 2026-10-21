"""The Ren'Py Switch Loader importer. importer/rsl_import.py is its command line and
importer/rsl_import_gui.py its window; both use only what this file lists. Standard
library only, Python 3.9+. It never runs game code and never unpickles game data.

The modules, in the order a game goes through them:
    archives   unpack a .zip, .7z or .rar safely
    analysis   find the game, its Ren'Py version and Python, check it, build the manifest
    rpa        read standard .rpa indexes as plain data; write the non-ASCII archive
    media      what would load the Switch, --optimize re-encoding, the cover and backdrop
    recompile  --recompile and --decompile, for Python 2 games
    exporter   write a game into the library: an SD card folder, or the Switch over FTP
    library    list, delete and compare what a library holds
    tools      find and run 7-Zip, FFmpeg and the decompilers
    common     errors, ids and small helpers
"""
from .analysis import analyze, load, report
from .archives import member_parts, unpack
from .common import ERRORS, ID_MAX, Refused, make_id, sha256
from .exporter import export, set_art
from .library import compare, delete_game, exported_games, games_dir, is_ftp, read_art
from .media import ART, image_size, make_art, probe_video
from .recompile import DECOMPILERS, find_unrpyc
from .rpa import read_member, rpa_index
from .tools import FFMPEG, FFPROBE, TOOL_DIRS, WINDOWS, find_tool, rmtree, run_7z, seven_zip
