"""Ren'Py .rpa archives: reading a standard index as plain data (never unpickled), and writing one."""
import os
import pickle
import pickletools
import shutil
import zlib

from .archives import member_parts
from .common import IMAGE_EXTS, Refused, VIDEO_EXTS


MAX_RPA_INDEX = 64 << 20           # decompressed archive index
PACKED = "rsl-nonascii"             # game/rsl-nonascii[-N].rpa: files the Switch SD cannot name

# Opcodes that only build plain data. Anything else (GLOBAL, REDUCE, BUILD, ...)
# could run code when unpickled, so an index using it is refused, never loaded.
_PLAIN = {"PROTO", "FRAME", "STOP", "MARK", "EMPTY_DICT", "DICT", "EMPTY_LIST", "LIST",
          "APPEND", "APPENDS", "EMPTY_TUPLE", "TUPLE", "TUPLE1", "TUPLE2", "TUPLE3", "SETITEM",
          "SETITEMS", "NONE", "NEWTRUE", "NEWFALSE", "INT", "BININT", "BININT1", "BININT2", "LONG",
          "LONG1", "LONG4", "UNICODE", "BINUNICODE", "SHORT_BINUNICODE", "BINUNICODE8", "STRING",
          "BINSTRING", "SHORT_BINSTRING", "BINBYTES", "SHORT_BINBYTES", "BINBYTES8", "MEMOIZE",
          "PUT", "BINPUT", "LONG_BINPUT", "GET", "BINGET", "LONG_BINGET"}


def plain_pickle(data):
    """The value of a pickle made only of dicts, lists, tuples, strings, bytes and ints,
    interpreted opcode by opcode (pickletools); nothing is imported or called."""
    stack, marks, memo = [], [], {}

    def pop_mark():
        k = marks.pop()
        items = stack[k:]
        del stack[k:]
        return items

    for op, arg, _ in pickletools.genops(data):
        n = op.name
        if n not in _PLAIN:
            raise ValueError("pickle opcode %s is not plain data" % n)
        if n in ("PROTO", "FRAME"):
            continue
        if n == "STOP":
            return stack[-1]
        if n == "MARK":
            marks.append(len(stack))
        elif n in ("EMPTY_DICT", "EMPTY_LIST", "EMPTY_TUPLE", "NONE", "NEWTRUE", "NEWFALSE"):
            stack.append({"EMPTY_DICT": {}, "EMPTY_LIST": [], "EMPTY_TUPLE": (), "NONE": None,
                          "NEWTRUE": True, "NEWFALSE": False}[n])
        elif n in ("TUPLE1", "TUPLE2", "TUPLE3"):
            k = int(n[-1])
            stack[-k:] = [tuple(stack[-k:])]
        elif n in ("TUPLE", "LIST"):
            items = pop_mark()
            stack.append(tuple(items) if n == "TUPLE" else items)
        elif n == "DICT":
            items = pop_mark()
            stack.append(dict(zip(items[::2], items[1::2])))
        elif n == "APPEND":
            v = stack.pop()
            stack[-1].append(v)
        elif n == "APPENDS":
            items = pop_mark()
            stack[-1].extend(items)
        elif n == "SETITEM":
            v, key = stack.pop(), stack.pop()
            stack[-1][key] = v
        elif n == "SETITEMS":
            items = pop_mark()
            stack[-1].update(zip(items[::2], items[1::2]))
        elif n == "MEMOIZE":
            memo[len(memo)] = stack[-1]
        elif n in ("PUT", "BINPUT", "LONG_BINPUT"):
            memo[arg] = stack[-1]
        elif n in ("GET", "BINGET", "LONG_BINGET"):
            stack.append(memo[arg])
        else:   # ints, strings, bytes
            stack.append(arg)
    raise ValueError("pickle has no STOP")


def rpa_index(path):
    """{name: (offset, length, prefix)} of a standard RPA-3.0 or RPA-2.0 archive (Ren'Py's
    own format, read as renpy/loader.py does), None for any other format."""
    with open(path, "rb") as f:
        head = f.read(40)
        if head.startswith(b"RPA-3.0 "):
            offset, key = int(head[8:24], 16), int(head[25:33], 16)
        elif head.startswith(b"RPA-2.0 "):
            offset, key = int(head[8:24], 16), 0
        else:
            return None
        f.seek(offset)
        raw = zlib.decompressobj().decompress(f.read(), MAX_RPA_INDEX)
    index, size, out = plain_pickle(raw), os.path.getsize(path), {}
    for name, parts in index.items():
        if not isinstance(name, str) or not parts:
            raise ValueError("bad index entry %r" % (name,))
        o, n = parts[0][0] ^ key, parts[0][1] ^ key
        prefix = parts[0][2] if len(parts[0]) > 2 else b""
        prefix = prefix.encode("latin-1") if isinstance(prefix, str) else prefix
        if o < 0 or n < 0 or o + n > size:
            raise ValueError("entry %r lies outside the archive" % name)
        out[name.replace("\\", "/")] = (o, n, prefix)
    return out


def packable(rel):
    """True for a non-ASCII name that the export puts into a PACKED archive. The Switch SD
    parses names as Shift-JIS and lists none of them (hardware run 20); Ren'Py reads
    names inside an archive itself. It loads no scripts or archives from an archive, so
    those stay loose."""
    return not rel.isascii() and not rel.lower().endswith((".rpy", ".rpym", "_ren.py", ".rpa"))


class Rpa:
    """Writes an RPA-3.0 archive (Ren'Py's own format, key 0), one member at a time."""

    def __init__(self, path):
        self.path, self.index, self.f = path, {}, open(path, "wb")
        self.f.write(b"RPA-3.0 %016x %08x\n" % (0, 0))

    def add(self, name, source):
        start = self.f.tell()
        with open(source, "rb") as src:
            shutil.copyfileobj(src, self.f, 1 << 20)
        self.index[name] = [(start, self.f.tell() - start)]

    def close(self):
        offset = self.f.tell()
        self.f.write(zlib.compress(pickle.dumps(self.index, 2)))
        self.f.seek(0)
        self.f.write(b"RPA-3.0 %016x %08x\n" % (offset, 0))
        self.f.close()


def archive_media(gamedir, files, workdir, warnings):
    """Media inside the game's .rpa archives, as {rel: (archive, offset, length, prefix)},
    plus the archives whose contents cannot be checked."""
    loose = {r for r, _ in files}
    media, locked = {}, []
    for rpa in [r for r in sorted(loose) if r.lower().endswith(".rpa")]:
        try:
            index = rpa_index(os.path.join(gamedir, rpa))
        except (ValueError, zlib.error, OSError, KeyError, IndexError, TypeError) as e:
            locked.append("%s (index not plain data: %s)" % (rpa, e))
            continue
        if index is None:
            locked.append("%s (custom format)" % rpa)
            continue
        for name, entry in index.items():
            try:
                member_parts(name)
            except Refused:
                warnings.append("archive %s has an unsafe name, skipped: %r" % (rpa, name))
                continue
            if name.lower().endswith(VIDEO_EXTS + IMAGE_EXTS) and name not in loose:
                media[name] = (rpa,) + entry
    return media, locked


def read_member(gamedir, member, limit=None):
    rpa, offset, length, prefix = member
    with open(os.path.join(gamedir, rpa), "rb") as f:
        f.seek(offset)
        return prefix + f.read(length if limit is None else min(length, limit))


def extract_member(gamedir, member, target):
    rpa, offset, length, prefix = member
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(os.path.join(gamedir, rpa), "rb") as src, open(target, "wb") as dst:
        dst.write(prefix)
        src.seek(offset)
        left = length
        while left:
            chunk = src.read(min(left, 1 << 20))
            if not chunk:
                break
            dst.write(chunk)
            left -= len(chunk)
