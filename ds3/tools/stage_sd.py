#!/usr/bin/env python3
"""Make the SD card folder for ds3-nx from your own discs and title update.

    python3 stage_sd.py --disc1 "Dead Space 3 (Disc 1).iso" [--disc2 "Dead Space 3 (Disc 2).iso"] \
        --tu <Title Update 1: the package, a folder or .zip holding it> -o <out>/ds3-nx
    python3 stage_sd.py --disc1 ... --tu ... -o <folder> --tu-only      (only tu/: what tools/codegen.sh needs)

A disc can be the ISO or a .7z holding it (read through 7-Zip's `7zz`/`7z` without unpacking the ISO first: an Xbox 360
disc is ~8.7 GB). Writes, beside where the NRO goes (src/ds3_app.h reads this layout):
    ds3-nx/disc1/   the extracted Disc 1 (bigfile0.viv 3.6 GB and bigfile1.viv 3.8 GB: under FAT32's 4 GB)
    ds3-nx/disc2/   the extracted Disc 2, if given (the game asks for it at chapter 5)
    ds3-nx/tu/      Disc 1's default.xex and deadspace_game_f.xex with Title Update 1's default.xexp and
                    deadspace_game_f.xexp (disc001/ in the package)

Dead Space 3 ships one TU1 package per disc edition (Media ID); the patches only fit the disc they were made for, so the
package whose Media ID is Disc 1's is taken (a folder or .zip with all four, as they are often passed around, works).
Every input is checked against the hashes the recompiled code was made from (TESTED below): the code is TU1's.
Only the standard library (the title update package is read by the small STFS reader below).
"""
import argparse
import hashlib
import io
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

TITLE_ID = 0x4541099D
GAME = "deadspace_game_f"
# Disc 1 Media ID -> (edition, SHA-256 of disc 1's deadspace_game_f.xex, of TU1's disc001/deadspace_game_f.xexp).
# Disc 1 Media IDs with a TU1 package: 4DC6CE0C (Russia), 0992A619, 0CC756B7, 7139B889 (not yet seen here).
TESTED = {
    0x4DC6CE0C: ("Russia", "74776c594043eeffd9ee5eea70201ff9e24dd63b490b9239719d22929a2dbb39",
                 "bc4337a54a1d8f4427afc6d044b0ed444a9be9548fe7e35e87809ad8bc5e0886"),
}
SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
CHUNK = 8 << 20


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# --- Disc sources: an ISO (random access) or a .7z holding one (read front to back, one pass per request) ---------
class Iso:
    def __init__(self, path: Path):
        self.path = path

    def ranges(self, wanted):
        """{(offset, length): bytes} for each wanted range."""
        out = {}
        with self.path.open("rb") as f:
            for off, n in wanted:
                f.seek(off)
                out[(off, n)] = f.read(n)
        return out

    def copy(self, files, sink):
        """files: [(offset, length, key)] in any order; sink(key, chunk) gets each file's bytes in order."""
        with self.path.open("rb") as f:
            for off, n, key in sorted(files):
                f.seek(off)
                while n:
                    chunk = f.read(min(CHUNK, n))
                    if not chunk:
                        sys.exit(f"{self.path}: image ends inside a file")
                    sink(key, chunk)
                    n -= len(chunk)


class SevenZip:
    def __init__(self, path: Path):
        self.path = path
        self.tool = shutil.which("7zz") or shutil.which("7z")
        if not self.tool:
            sys.exit(f"{path}: reading a .7z needs 7-Zip (brew install sevenzip), or unpack the ISO first")

    def _stream(self, end, visit):
        p = subprocess.Popen([self.tool, "e", "-so", str(self.path)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        pos = 0
        try:
            while pos < end:
                d = p.stdout.read(min(CHUNK, end - pos))
                if not d:
                    sys.exit(f"{self.path}: the archive ends before offset {end:#x}")
                visit(pos, d)
                pos += len(d)
        finally:
            p.kill()
            p.wait()

    def ranges(self, wanted):
        bufs = {w: bytearray() for w in wanted}

        def visit(pos, d):
            for off, n in wanted:
                a, b = max(off, pos), min(off + n, pos + len(d))
                if a < b:
                    bufs[(off, n)] += d[a - pos:b - pos]
        self._stream(max(o + n for o, n in wanted), visit)
        return {w: bytes(b) for w, b in bufs.items()}

    def copy(self, files, sink):
        files = sorted(files)

        def visit(pos, d):
            for off, n, key in files:
                a, b = max(off, pos), min(off + n, pos + len(d))
                if a < b:
                    sink(key, d[a - pos:b - pos])
        if files:
            self._stream(max(o + n for o, n, _ in files), visit)


def source(path: Path):
    return SevenZip(path) if path.suffix.lower() == ".7z" else Iso(path)


# --- XDVDFS (Xbox 360 disc) -------------------------------------------------------------------------------
def partition(src) -> int:
    bases = (0x0, 0x2080000, 0xFD90000, 0x18300000)
    got = src.ranges([(b + 32 * SECTOR, 28) for b in bases])
    for b in bases:
        vd = got[(b + 32 * SECTOR, 28)]
        if vd[:20] == MAGIC:
            return b, struct.unpack_from("<II", vd, 20)
    sys.exit("Not an Xbox 360 disc image (no XDVDFS volume found)")


def entries(table: bytes):
    pending, seen = [0], set()
    while pending:
        o = pending.pop()
        p = o * 4
        if o in seen or p + 14 > len(table):
            continue
        seen.add(o)
        left, right, start, length, attr, nlen = struct.unpack("<HHIIBB", table[p:p + 14])
        if left == 0xFFFF:
            continue
        name = table[p + 14:p + 14 + nlen].decode("latin1")
        if left:
            pending.append(left)
        if right:
            pending.append(right)
        if "/" in name or "\\" in name or name in ("", ".", ".."):
            sys.exit(f"Unsafe name on disc: {name!r}")
        yield name, start, length, bool(attr & 0x10)


def listing(src):
    """[(path, offset, length)] of every file, the directory tables read level by level (one pass each on a .7z)."""
    base, (root, root_size) = partition(src)
    files, level = [], [("", root, root_size)]
    while level:
        tables = src.ranges([(base + s * SECTOR, n) for _, s, n in level])
        nxt = []
        for prefix, s, n in level:
            for name, start, length, is_dir in entries(tables[(base + s * SECTOR, n)]):
                if is_dir:
                    if length:
                        nxt.append((f"{prefix}{name}/", start, length))
                else:
                    files.append((f"{prefix}{name}", base + start * SECTOR, length))
        level = nxt
    return files


def read_files(src, files, names):
    wanted = [(o, n) for p, o, n in files if p.casefold() in names]
    got = src.ranges(wanted)
    return {p.casefold(): got[(o, n)] for p, o, n in files if (o, n) in got}


def extract(src, files, out: Path) -> None:
    todo = []
    for path, off, n in files:
        dest = out / path
        if not (dest.is_file() and dest.stat().st_size == n):
            dest.parent.mkdir(parents=True, exist_ok=True)
            todo.append((off, n, path))
    total = sum(n for _, n, _ in todo) or 1
    handles, done = {}, [0]

    def sink(path, chunk):
        f = handles.get(path)
        if f is None:
            f = handles[path] = (out / path).open("wb")
        f.write(chunk)
        done[0] += len(chunk)
        print(f"\r  {out.name}: {done[0] * 100 // total}%", end="", flush=True)
    try:
        src.copy(todo, sink)
    finally:
        for f in handles.values():
            f.close()
    for path, _, n in files:
        if (out / path).stat().st_size != n:
            sys.exit(f"{out / path}: incomplete")
    print(f"\r  {out.name}: {len(files)} files, {sum(n for _, _, n in files) / 2**30:.1f} GiB -> {out}")


def execution_info(xex: bytes):
    """(media ID, title ID, disc number, disc count) from an XEX2 header."""
    if xex[:4] != b"XEX2":
        sys.exit("not an XEX2 executable")
    for i in range(struct.unpack_from(">I", xex, 0x14)[0]):
        key, value = struct.unpack_from(">II", xex, 0x18 + i * 8)
        if key == 0x40006:
            media, _, _, title, _, _, disc, discs = struct.unpack_from(">IIIIBBBB", xex, value)
            return media, title, disc, discs
    sys.exit("XEX has no execution info")


# --- Title update ------------------------------------------------------------------------------------------
class Stfs:
    """The parts of an Xbox 360 STFS package (CON/LIVE/PIRS) needed to read files from it."""

    BLOCK = 0x1000

    def __init__(self, data: bytes):
        self.d = data
        if data[:4] not in (b"CON ", b"LIVE", b"PIRS"):
            raise ValueError("not an Xbox 360 package (STFS)")
        self.media_id, = struct.unpack_from(">I", data, 0x354)
        self.title_id, = struct.unpack_from(">I", data, 0x360)
        header_size = struct.unpack_from(">I", data, 0x340)[0]
        self.data = (header_size + 0xFFF) & ~0xFFF
        volume = data[0x379:0x379 + 0x24]
        # Read-only packages keep one hash table per level, the others two.
        self.tables = 1 if volume[2] & 1 else 2
        self.table_blocks = struct.unpack_from("<H", volume, 3)[0]
        self.table_start = int.from_bytes(volume[5:8], "little")

    def block(self, b: int) -> bytes:
        # Data blocks are numbered without the hash table blocks between them: one level 0 table every 0xAA blocks,
        # one level 1 every 0x70E4, one level 2 every 0x4AF768.
        backing = b + (b + 0xAA) // 0xAA * self.tables
        if b >= 0xAA:
            backing += (b + 0x70E4) // 0x70E4 * self.tables
            if b >= 0x70E4:
                backing += (b + 0x4AF768) // 0x4AF768 * self.tables
        o = self.data + backing * self.BLOCK
        return self.d[o:o + self.BLOCK]

    def files(self) -> dict:
        es = []
        for k in range(self.table_blocks):
            table = self.block(self.table_start + k)
            for i in range(0, self.BLOCK, 0x40):
                e = table[i:i + 0x40]
                if not e[0]:
                    continue
                flags = e[0x28]
                es.append({"name": e[:flags & 0x3F].decode("latin-1"), "dir": bool(flags & 0x80),
                           "contig": bool(flags & 0x40), "blocks": int.from_bytes(e[0x29:0x2C], "little"),
                           "start": int.from_bytes(e[0x2F:0x32], "little"),
                           "parent": struct.unpack_from(">h", e, 0x32)[0],
                           "size": struct.unpack_from(">I", e, 0x34)[0]})
        out = {}
        for e in es:
            if e["dir"]:
                continue
            parent = es[e["parent"]]["name"] if 0 <= e["parent"] < len(es) else ""
            path = f"{parent}/{e['name']}".lstrip("/").casefold()
            if not e["contig"]:
                out[path] = None  # fragmented: not read by this reader
                continue
            out[path] = b"".join(self.block(e["start"] + k) for k in range(e["blocks"]))[:e["size"]]
        return out


def packages(path: Path):
    """Every STFS package in a file, folder or .zip: [(name, Stfs)]."""
    blobs = []
    if path.is_dir():
        blobs = [(str(p.relative_to(path)), p.read_bytes) for p in sorted(path.rglob("*"))
                 if p.is_file() and p.stat().st_size < 64 << 20]
    elif zipfile.is_zipfile(path):
        z = zipfile.ZipFile(path)
        blobs = [(i.filename, (lambda i=i: z.read(i))) for i in z.infolist()
                 if not i.is_dir() and i.file_size < 64 << 20]
    else:
        blobs = [(path.name, path.read_bytes)]
    out = []
    for name, read in blobs:
        data = read()
        try:
            out.append((name, Stfs(data)))
        except ValueError:
            pass
    return out


def title_update(path: Path, media_id: int):
    found = [(n, p) for n, p in packages(path) if p.title_id == TITLE_ID]
    if not found:
        sys.exit(f"{path}: no Dead Space 3 title update package in it")
    for name, pkg in found:
        if pkg.media_id == media_id:
            files = pkg.files()
            need = [f"disc001/{f}.xexp" for f in (GAME, "default")]
            for f in need:
                if f not in files:
                    sys.exit(f"{name}: no {f} in the package")
                if files[f] is None:
                    sys.exit(f"{name}: {f} is fragmented in this package: extract it with another tool")
            print(f"  title update: {name} (Media ID {media_id:08X})")
            return files[need[0]], files[need[1]]
    sys.exit(f"{path}: none of its packages is for Media ID {media_id:08X} (it has "
             + ", ".join(f"{p.media_id:08X}" for _, p in found) + ")")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc1", type=Path, required=True, help="Disc 1: ISO, or a .7z holding it")
    ap.add_argument("--disc2", type=Path, help="Disc 2: ISO, or a .7z holding it")
    ap.add_argument("--tu", type=Path,
                    help="Title Update 1: the package, or a folder/.zip with it (without it only disc1/ and disc2/ "
                         "are written, for a tu/ made before)")
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--tu-only", action="store_true", help="only write tu/ (what tools/codegen.sh needs)")
    a = ap.parse_args()
    for name, path in (("--disc1", a.disc1), ("--disc2", a.disc2), ("--tu", a.tu)):
        if path is not None and not path.exists():
            sys.exit(f"{name}: {path} not found")

    src1 = source(a.disc1)
    print(f"  {a.disc1.name}: reading the file table")
    files1 = listing(src1)
    exes = read_files(src1, files1, {"default.xex", f"{GAME}.xex"})
    if f"{GAME}.xex" not in exes:
        sys.exit(f"{a.disc1}: no {GAME}.xex on it: is it Dead Space 3's Disc 1?")
    media, title, disc, discs = execution_info(exes[f"{GAME}.xex"])
    if title != TITLE_ID or (disc, discs) != (1, 2):
        sys.exit(f"{a.disc1}: title {title:08X} disc {disc}/{discs}: not Dead Space 3's Disc 1")
    print(f"  Disc 1: Media ID {media:08X}")
    if a.tu is not None:
        game_xexp, default_xexp = title_update(a.tu, media)
        edition = TESTED.get(media)
        if edition is None:
            print(f"  warning: Media ID {media:08X} is not an edition this port was tested with; trying anyway")
        elif sha256(exes[f"{GAME}.xex"]) != edition[1] or sha256(game_xexp) != edition[2]:
            print(f"  warning: the executable or the title update differs from the tested {edition[0]} edition")
        else:
            print(f"  {GAME}.xex + TU1: ok ({edition[0]} edition)")

        tu = a.out / "tu"
        tu.mkdir(parents=True, exist_ok=True)
        (tu / "default.xex").write_bytes(exes["default.xex"])
        (tu / "default.xexp").write_bytes(default_xexp)
        (tu / f"{GAME}.xex").write_bytes(exes[f"{GAME}.xex"])
        (tu / f"{GAME}.xexp").write_bytes(game_xexp)
    elif a.tu_only:
        sys.exit("--tu-only needs --tu")
    else:
        print("  no --tu: tu/ is not written, only disc1/ and disc2/")
        if media not in TESTED:
            print(f"  warning: Media ID {media:08X} is not an edition this port was tested with; trying anyway")
    if a.tu_only:
        print(f"Ready: {tu}")
        return
    extract(src1, files1, a.out / "disc1")
    if a.disc2:
        src2 = source(a.disc2)
        files2 = listing(src2)
        exe2 = read_files(src2, files2, {f"{GAME}.xex"}).get(f"{GAME}.xex")
        if not exe2 or execution_info(exe2)[1:] != (TITLE_ID, 2, 2):
            sys.exit(f"{a.disc2}: not Dead Space 3's Disc 2")
        extract(src2, files2, a.out / "disc2")
    print(f"Ready: copy the contents of {a.out} into sdmc:/switch/ds3-nx/ (next to ds3-nx.nro).")


if __name__ == "__main__":
    main()
