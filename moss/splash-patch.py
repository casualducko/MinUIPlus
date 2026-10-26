#!/usr/bin/env python3
"""Put a new boot splash into a copy of Moss's /flash/KERNEL. Writes nothing on a device.

usage: splash-patch.py KERNEL NEW.png OUT_KERNEL

The splash is /splash/splash.png in the initramfs the kernel carries (a gzip stream of a cpio archive); the
early boot script (init: load_splash) draws it with ply-image. The new stream is padded with zeros to the
old stream's length, so every offset after it stays where it was (the kernel's unpacker skips zero bytes).
It stops if the new stream would be longer than the old one.
"""
import sys, zlib, gzip, re

def find_stream(d):
    # the gzip stream whose contents are a cpio archive ("070701" newc) containing splash/splash.png
    for m in re.finditer(b'\x1f\x8b\x08', d):
        s = m.start()
        try:
            o = zlib.decompressobj(31)
            out = o.decompress(d[s:])
        except Exception:
            continue
        if out[:6] == b'070701' and b'splash/splash.png' in out:
            return s, len(d) - len(o.unused_data) - s, out
    sys.exit('no initramfs with a splash found in this KERNEL')

def pad4(n): return (4 - n % 4) % 4

def replace_entry(cpio, name, data):
    out = bytearray(); p = 0; done = False
    while p < len(cpio):
        h = cpio[p:p+110]
        if h[:6] != b'070701': sys.exit('cpio: bad header at %d' % p)
        f = [int(h[6+8*i:14+8*i], 16) for i in range(13)]
        size, namesize = f[6], f[11]
        nm = cpio[p+110:p+110+namesize-1].decode()
        body = p + 110 + namesize + pad4(110 + namesize)
        end = body + size + pad4(size)
        if nm == name:
            nh = bytearray(h); nh[6+8*6:14+8*6] = b'%08X' % len(data)
            out += nh + cpio[p+110:body] + data + b'\0' * pad4(len(data)); done = True
        else:
            out += cpio[p:end]
        p = end
        if nm == 'TRAILER!!!': break
    if not done: sys.exit('no %s in the archive' % name)
    return bytes(out) + cpio[p:]

def main():
    kernel, png, out = sys.argv[1:4]
    d = open(kernel, 'rb').read()
    new = open(png, 'rb').read()
    if new[:8] != b'\x89PNG\r\n\x1a\n': sys.exit('not a PNG')
    s, n, cpio = find_stream(d)
    cpio2 = replace_entry(cpio, 'splash/splash.png', new)
    gz = gzip.compress(cpio2, 9, mtime=0)
    print('old stream %d bytes, new %d bytes (%d to spare)' % (n, len(gz), n - len(gz)))
    if len(gz) > n: sys.exit('the new stream is longer than the old one; use a smaller picture')
    res = d[:s] + gz + b'\0' * (n - len(gz)) + d[s+n:]
    assert len(res) == len(d)
    open(out, 'wb').write(res)
    # check: read it back
    s2, n2, c2 = find_stream(res)
    assert replace_entry(c2, 'splash/splash.png', new) == c2 and s2 == s
    print('written', out, '(same length as the original, splash read back)')
main()
