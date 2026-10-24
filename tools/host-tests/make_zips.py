#!/usr/bin/env python3
"""Zip files for test_romulus: usage make_zips.py OUTDIR"""
import os, struct, sys, zipfile, zlib
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rom = b"NES\x1a" + bytes((i * 31) & 255 for i in range(100000))
def zf(name, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(os.path.join(out, name), "w", method) as z:
        for n, d in entries: z.writestr(n, d)
zf("ok.zip", [("readme.txt", b"hi"), ("sub/dir/Game One.nes", rom)])
zf("stored.zip", [("Game.gba", rom)], zipfile.ZIP_STORED)
zf("evil.zip", [("../../evil.nes", rom)])
zf("evil2.zip", [("..\\..\\evil2.nes", rom)])
zf("norom.zip", [("a.txt", b"x"), ("b.png", b"y")])
zf("folder.zip", [("dir/", b""), ("dir/Real.nes", rom)])
zf("upper.zip", [("GAME.NES", rom)])
# corrupt: flip a byte in the compressed data
data = bytearray(open(os.path.join(out, "ok.zip"), "rb").read())
with zipfile.ZipFile(os.path.join(out, "ok.zip")) as z:
    info = z.getinfo("sub/dir/Game One.nes")
pos = info.header_offset + 30 + len(info.filename) + 20
data[pos] ^= 0xFF
open(os.path.join(out, "corrupt.zip"), "wb").write(data)
# wrong CRC in the central directory
data = bytearray(open(os.path.join(out, "stored.zip"), "rb").read())
cd = data.rfind(b"PK\x01\x02")
data[cd + 16] ^= 0x55
open(os.path.join(out, "badcrc.zip"), "wb").write(data)
# truncated
data = open(os.path.join(out, "ok.zip"), "rb").read()
open(os.path.join(out, "truncated.zip"), "wb").write(data[: len(data) // 2])
open(os.path.join(out, "notzip.zip"), "wb").write(b"this is not a zip file at all, just text" * 10)
open(os.path.join(out, "empty.zip"), "wb").write(b"")
# claims a huge size (zip bomb style): patch the uncompressed size in the central directory
data = bytearray(open(os.path.join(out, "stored.zip"), "rb").read())
cd = data.rfind(b"PK\x01\x02")
struct.pack_into("<I", data, cd + 24, 0x7FFFFFFF)
open(os.path.join(out, "huge.zip"), "wb").write(data)
# deflate stream that expands past its declared size
big = b"\0" * 5_000_000
data = bytearray()
with zipfile.ZipFile(os.path.join(out, "bomb.zip"), "w", zipfile.ZIP_DEFLATED) as z: z.writestr("bomb.nes", big)
data = bytearray(open(os.path.join(out, "bomb.zip"), "rb").read())
cd = data.rfind(b"PK\x01\x02")
struct.pack_into("<I", data, cd + 24, 1000)  # declared 1000, really 5 MB
open(os.path.join(out, "bomb.zip"), "wb").write(data)
# encrypted flag on the only rom entry
data = bytearray(open(os.path.join(out, "stored.zip"), "rb").read())
cd = data.rfind(b"PK\x01\x02")
struct.pack_into("<H", data, cd + 8, 1)
open(os.path.join(out, "encrypted.zip"), "wb").write(data)
print("ok")
