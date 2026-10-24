#!/usr/bin/env python3
"""Decode the raw textures a frame dump writes (texturas_NN/, ds3_gpu.cpp DumpTexture) into PNG files.

Each texture is <base>_<w>x<h>_f<format>.bin (its base level as the guest has it: tiled or linear, big-endian as the
fetch constant says), .mips (its mip chain) and .txt (the six fetch constant words). The base level is untiled with
Xenos' 2D tiling (as Xenia's texture_address Tiled2D), byte-swapped per the fetch's endianness and decoded: DXT1, DXT2/3,
DXT4/5, DXN, 8_8_8_8, 8, 5_6_5. The PNG shows what the game had in memory when the frame was traced.

Usage: decode_texture_dump.py <texturas_NN folder> <out folder> [--montage]     (only the standard library)
--montage also writes montage.png: every texture as a 128x128 thumbnail, numbered in the order of montage.txt.
"""
import os
import struct
import sys
import zlib


def png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def tiled_offset(x, y, pitch, log2_bpb):
    # Xenia texture_address::Tiled2D: row part, then column part (x, y in blocks; pitch in blocks, multiple of 32).
    macro = ((y >> 5) * (pitch >> 5)) << (log2_bpb + 7)
    micro = ((y & 6) << 2) << log2_bpb
    row = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 8) << (3 + log2_bpb)) + ((y & 1) << 4)
    macro = (x >> 5) << (log2_bpb + 7)
    micro = (x & 7) << log2_bpb
    off = row + macro + ((micro & ~0xF) << 1) + (micro & 0xF)
    return ((off & ~0x1FF) << 3) + ((off & 0x1C0) << 2) + (off & 0x3F) + ((y & 16) << 7) + \
        (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)


def swap(data, endian):
    if endian == 1:  # 8 in 16
        return b"".join(data[i + 1:i + 2] + data[i:i + 1] for i in range(0, len(data) - 1, 2))
    if endian == 2:  # 8 in 32
        return b"".join(data[i:i + 4][::-1] for i in range(0, len(data) - 3, 4))
    if endian == 3:  # 16 in 32
        return b"".join(data[i + 2:i + 4] + data[i:i + 2] for i in range(0, len(data) - 3, 4))
    return data


def c565(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def dxt_color(b, punch):
    c0, c1, bits = struct.unpack_from("<HHI", b, 0)
    p0, p1 = c565(c0), c565(c1)
    if c0 > c1 or not punch:
        pal = [p0, p1, tuple((2 * a + c) // 3 for a, c in zip(p0, p1)), tuple((a + 2 * c) // 3 for a, c in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a + c) // 2 for a, c in zip(p0, p1)), (0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def dxt5_alpha(b):
    a0, a1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(blob, words, w, h):
    fmt = words[1] & 0x3F
    endian = (words[1] >> 6) & 3
    tiled = (words[0] >> 31) & 1
    pitch = ((words[0] >> 22) & 0x1FF) << 5
    block, bpb = {18: (4, 8), 19: (4, 16), 20: (4, 16), 49: (4, 16), 59: (4, 8), 2: (1, 1), 4: (1, 2), 6: (1, 4)}[fmt]
    log2 = {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[bpb]
    bw, bh, pb = (w + block - 1) // block, (h + block - 1) // block, max(pitch // block, 32)
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            o = tiled_offset(bx, by, pb, log2) if tiled else (by * pb + bx) * bpb
            raw = swap(blob[o:o + bpb], endian)
            if len(raw) < bpb:
                continue
            if fmt == 18:
                px = [c + (255,) for c in dxt_color(raw, True)]
            elif fmt in (19, 20):
                cols = dxt_color(raw[8:], False)
                alpha = dxt5_alpha(raw[:8]) if fmt == 20 else [((int.from_bytes(raw[:8], "little") >> (4 * i)) & 15) * 17
                                                                for i in range(16)]
                px = [c + (a,) for c, a in zip(cols, alpha)]
            elif fmt == 49:
                r, g = dxt5_alpha(raw[:8]), dxt5_alpha(raw[8:])
                px = [(a, b, 255, 255) for a, b in zip(r, g)]
            elif fmt == 59:
                px = [(a, a, a, 255) for a in dxt5_alpha(raw[:8])]
            elif fmt == 2:
                px = [(raw[0], raw[0], raw[0], 255)]
            elif fmt == 4:
                px = [c565(struct.unpack("<H", raw)[0]) + (255,)]
            else:  # 8_8_8_8 as stored (A R G B after the swap)
                px = [(raw[1], raw[2], raw[3], raw[0])]
            for i, p in enumerate(px):
                x, y = bx * block + (i % block), by * block + (i // block)
                if x < w and y < h:
                    out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(p)
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    montage = "--montage" in sys.argv
    thumbs = []
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if not name.endswith(".txt"):
            continue
        stem = name[:-4]
        words = [int(x, 16) for x in open(os.path.join(src, name)).read().split()]
        w, h = (int(v) for v in stem.split("_")[1].split("x"))
        blob = open(os.path.join(src, stem + ".bin"), "rb").read()
        try:
            rgba = decode(blob, words, w, h)
        except KeyError:
            continue
        empty = not any(blob)
        png(os.path.join(dst, stem + (".EMPTY" if empty else "") + ".png"), w, h, rgba)
        if montage and w >= 16 and h >= 16:
            t = 128
            thumbs.append((stem, bytes(b for y in range(t) for x in range(t)
                                      for b in rgba[((y * h // t) * w + x * w // t) * 4:((y * h // t) * w + x * w // t) * 4 + 3] + b"\xff")))
        print(f"{stem}: tiled {(words[0] >> 31) & 1} endian {(words[1] >> 6) & 3} pitch {((words[0] >> 22) & 0x1FF) << 5}"
              f" mips {'yes' if (words[5] >> 12) & 0x1FFFF else 'no'} levels {(words[4] >> 2) & 15}-{(words[4] >> 6) & 15}"
              f"{' ALL ZERO' if empty else ''}")
    if montage and thumbs:
        write_montage(dst, thumbs)


def write_montage(dst, thumbs):
    t, cols = 128, 8
    rows = (len(thumbs) + cols - 1) // cols
    W, H = cols * t, rows * t
    out = bytearray(W * H * 4)
    for k, (_, px) in enumerate(thumbs):
        ox, oy = (k % cols) * t, (k // cols) * t
        for y in range(t):
            out[((oy + y) * W + ox) * 4:((oy + y) * W + ox + t) * 4] = px[y * t * 4:(y + 1) * t * 4]
    png(os.path.join(dst, "montage.png"), W, H, out)
    open(os.path.join(dst, "montage.txt"), "w").write("".join(f"{k} {s}\n" for k, (s, _) in enumerate(thumbs)))


if __name__ == "__main__":
    main()
