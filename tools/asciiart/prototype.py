#!/usr/bin/env python3
"""Prototype of generated ASCII box art for the launcher (Python, to pick a look; the device version
would be C). One game name in, one 360x360 PNG out (the launcher draws .res/<rom>.png at the right
edge). The art is a pure function of the normalized title, so the same game always gets the same art
and region variants of a title share it.

usage: prototype.py sheet OUT.png          contact sheet for the sample names
       prototype.py one "Name (USA).gba" GBA OUT.png [style]
       prototype.py image SHOT.png "Name" GBA OUT.png     art made from a screenshot
       prototype.py mock OUT.png              a tile composited onto the launcher list screenshot
"""
import math, re, sys
from PIL import Image, ImageDraw, ImageFont

FONT = '/System/Library/Fonts/Menlo.ttc'
W = H = 360
THEMES = {  # background, bright, dim (the theme's white, dark_text) - stock and warm
    'stock': ((0, 0, 0), (255, 255, 255), (102, 102, 102)),
    'warm':  ((26, 15, 5), (255, 217, 140), (160, 120, 64)),
    'ocean': ((6, 20, 31), (217, 241, 255), (79, 122, 150)),
}
RAMP = ' .:-=+*#%@'

# ---------------------------------------------------------------- name -> seed (all of this ports to C)
def title_of(name):
    """'Legend of Zelda, The (USA) (Rev 1).sfc' -> 'The Legend of Zelda' (the same rule as the cheat matcher)"""
    cuts = [i for i in (name.find(' ('), name.find(' [')) if i >= 0]
    if cuts:
        t = name[:min(cuts)]                 # the tags and the extension go
    else:
        t = re.sub(r'\.[A-Za-z0-9]{1,4}$', '', name)   # no tags: drop the extension
    m = re.match(r'^(.*), (The|A|An)$', t)
    if m:
        t = m.group(2) + ' ' + m.group(1)
    return t.strip()

def fnv1a(s):
    h = 2166136261
    for b in s.lower().encode('utf-8'):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h

class Rng:  # xorshift32
    def __init__(self, seed):
        self.s = seed or 0x9E3779B9
    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x & 0xFFFFFFFF
        return self.s
    def f(self):  # 0..1
        return (self.next() & 0xFFFFFF) / float(0x1000000)
    def i(self, n):
        return self.next() % n

# ---------------------------------------------------------------- the grid
COLS, ROWS = 40, 20          # 9x18 px cells
AX0, AX1, AY0, AY1 = 1, 39, 1, 15   # the art area inside the frame: 38 x 14

class Grid:
    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.ch = [[' '] * cols for _ in range(rows)]
        self.lv = [[0.0] * cols for _ in range(rows)]   # 0..1 brightness
    def put(self, x, y, c, lv=1.0):
        if 0 <= x < self.cols and 0 <= y < self.rows:
            self.ch[y][x] = c
            self.lv[y][x] = lv
    def text(self, x, y, s, lv=1.0):
        for i, c in enumerate(s):
            self.put(x + i, y, c, lv)

def ramp(v):
    v = max(0.0, min(0.999, v))
    return RAMP[int(v * len(RAMP))]

# ---------------------------------------------------------------- style 1: mirrored creature
def style_sprite(g, rng):
    hw, sh = 7, 12                        # half width, height (cells are 2 chars wide, 1 row high: square)
    w = hw * 2
    grid = [[0] * w for _ in range(sh)]
    cx = (hw - 0.5)
    for y in range(sh):
        for x in range(hw):
            dist = math.hypot((x - (hw - 1)) / hw, (y - (sh - 1) / 2.0) / (sh / 2.0))
            p = 1.15 - 0.85 * dist + 0.3 * (rng.f() - 0.5)
            v = 1 if rng.f() < p else 0
            grid[y][hw - 1 - x] = grid[y][hw + x] = v
    for _ in range(2):                    # smooth into blobs, keeping the mirror
        nxt = [row[:] for row in grid]
        for y in range(sh):
            for x in range(hw):
                n = sum(grid[yy][xx] for yy in range(max(0, y-1), min(sh, y+2)) for xx in range(max(0, x-1), min(w, x+2)))
                v = 1 if n >= 4 else 0
                nxt[y][x] = nxt[y][w-1-x] = v
        grid = nxt
    rows_on = [y for y in range(sh) if any(grid[y])]
    if rows_on:                           # a spine, so the two halves are one creature
        for y in range(min(rows_on), max(rows_on) + 1):
            grid[y][hw - 1] = grid[y][hw] = 1
        for y in range(min(rows_on), max(rows_on) + 1):
            for x in (hw - 2, hw + 1):
                if rng.f() < 0.8: grid[y][x] = 1
    ey = sh // 3
    for x in (hw - 3,):
        if grid[ey][x]: grid[ey][x] = grid[ey][w-1-x] = 2
    for x in (hw - 4,):
        if grid[ey][x] and rng.f() < 0.5: grid[ey][x] = grid[ey][w-1-x] = 2
    body = '#%@'[rng.i(3)]
    ox = AX0 + (AX1 - AX0 - w * 2) // 2
    oy = AY0 + (AY1 - AY0 - sh) // 2
    for _ in range(30):
        g.put(AX0 + rng.i(AX1 - AX0), AY0 + rng.i(AY1 - AY0), '.+*'[rng.i(3)], 0.2 + 0.3 * rng.f())
    for y in range(sh):
        for x in range(w):
            v = grid[y][x]
            if not v:
                continue
            nb = [(x-1, y), (x+1, y), (x, y-1), (x, y+1)]
            edge = any(not (0 <= xx < w and 0 <= yy < sh) or grid[yy][xx] == 0 for xx, yy in nb)
            for dx in range(2):
                if v == 2:
                    g.put(ox + x * 2 + dx, oy + y, ' ', 0)         # an eye is a hole
                elif edge:
                    g.put(ox + x * 2 + dx, oy + y, body, 1.0)
                else:
                    g.put(ox + x * 2 + dx, oy + y, ':=+*'[(x + y + dx) % 4], 0.35 + 0.35 * (1 - y / sh))

# ---------------------------------------------------------------- style 4: the title's first letter, big
def style_mono(g, rng, title=''):
    words = [w for w in title.split() if w.lower() not in ('the', 'a', 'an')] or [title or '?']
    letter = words[0][0].upper()
    for y in range(AY0, AY1):             # a quiet field behind it
        for x in range(AX0, AX1):
            v = 0.5 + 0.5 * math.sin(x * 0.35 + rng.f() * 0.4) * math.sin(y * 0.7)
            if rng.f() < 0.18:
                g.put(x, y, '.:'[rng.i(2)], 0.12 + 0.12 * v)
    font = ImageFont.truetype(FONT, 200)
    img = Image.new('L', (200, 260), 0)
    ImageDraw.Draw(img).text((20, 0), letter, font=font, fill=255)
    bbox = img.getbbox()
    if not bbox:
        return
    img = img.crop(bbox)
    th = AY1 - AY0 - 1
    tw = max(1, int(round(th * 2.0 * img.width / img.height)))
    if tw > AX1 - AX0 - 2:
        tw = AX1 - AX0 - 2
        th = max(1, int(round(tw / 2.0 * img.height / img.width)))
    small = img.resize((tw, th), Image.LANCZOS)
    ox = AX0 + (AX1 - AX0 - tw) // 2
    oy = AY0 + (AY1 - AY0 - th) // 2
    px = small.load()
    for y in range(th):
        for x in range(tw):
            v = px[x, y] / 255.0
            if v > 0.12:
                g.put(ox + x + 1, oy + y + (0), ramp(v), 0.35 + 0.65 * v)
    shade = rng.i(2)
    for y in range(th):                    # a drop shadow one cell down-right
        for x in range(tw):
            if px[x, y] > 100 and g.ch[oy + y + 1][ox + x + 2] in ' .:':
                g.put(ox + x + 2, oy + y + 1, '%' if shade else ':', 0.28)

# ---------------------------------------------------------------- style 2: interference field
def style_plasma(g, rng):
    n = 3 + rng.i(2)
    waves = [(0.12 + 0.5 * rng.f(), 0.12 + 0.5 * rng.f(), rng.f() * 6.28) for _ in range(n)]
    cx, cy = AX0 + (AX1 - AX0) * (0.3 + 0.4 * rng.f()), AY0 + (AY1 - AY0) * (0.3 + 0.4 * rng.f())
    k = 0.25 + 0.3 * rng.f()
    for y in range(AY0, AY1):
        for x in range(AX0, AX1):
            xx, yy = x * 0.5, y            # a cell is twice as tall as wide
            v = sum(math.sin(xx * a + yy * b + p) for a, b, p in waves)
            d = math.hypot((x - cx) * 0.5, (y - cy))
            v += 1.6 * math.sin(d * k * 2.2)
            v = (v / (n + 1.6) + 1) / 2
            v = v * v * (3 - 2 * v)         # a little contrast
            g.put(x, y, ramp(v), 0.2 + 0.8 * v)

# ---------------------------------------------------------------- style 3: ridges, sun and stars
def style_land(g, rng):
    for _ in range(34):
        g.put(AX0 + rng.i(AX1 - AX0), AY0 + rng.i(7), '.+*'[rng.i(3)], 0.3 + 0.6 * rng.f())
    sx = AX0 + 6 + rng.i(AX1 - AX0 - 12)
    sy = AY0 + 3 + rng.i(3)
    sr = 3 + rng.i(2)
    for y in range(AY0, AY1):
        for x in range(AX0, AX1):
            d = math.hypot((x - sx) * 0.5, (y - sy))
            if d < sr * 0.62:
                g.put(x, y, '@', 1.0)
            elif d < sr * 0.8:
                g.put(x, y, '%', 0.85)
    layers = []
    for l in range(3):
        waves = [(0.08 + 0.18 * rng.f() + l * 0.05, rng.f() * 6.28, 0.6 + rng.f()) for _ in range(3)]
        layers.append(waves)
    base = [AY0 + 6, AY0 + 8, AY0 + 10]
    amp = [4.0, 3.0, 2.0]
    fills = ['.:', '=+', '#%']
    for l in range(3):
        for x in range(AX0, AX1):
            h = sum(a * math.sin(x * f + p) for f, p, a in layers[l]) / 3.0
            top = int(round(base[l] - amp[l] * (0.5 + 0.5 * h) - 0.5))
            for y in range(max(top, AY0), AY1):
                edge = y == top
                c = '^' if edge and l < 2 else fills[l][(x + y) % 2]
                g.put(x, y, c, 0.35 + 0.3 * l + (0.15 if edge else 0))

STYLES = [style_sprite, style_plasma, style_land, style_mono]
STYLE_NAMES = ['creature', 'plasma', 'landscape', 'monogram']

# ---------------------------------------------------------------- frame and title
def wrap_title(t, width=36, lines=2):
    words, out, cur = t.upper().split(), [], ''
    for w in words:
        if len(cur) + (1 if cur else 0) + len(w) <= width:
            cur = (cur + ' ' + w) if cur else w
        else:
            if cur: out.append(cur)
            cur = w
    if cur: out.append(cur)
    if len(out) > lines:
        out = out[:lines]
        out[-1] = out[-1][:width - 1] + '~'
    return [l[:width] for l in out] or ['']

def frame(g, title, tag):
    g.put(0, 0, '+'); g.put(COLS - 1, 0, '+'); g.put(0, ROWS - 1, '+'); g.put(COLS - 1, ROWS - 1, '+')
    g.put(0, 15, '+'); g.put(COLS - 1, 15, '+')
    for x in range(1, COLS - 1):
        g.put(x, 0, '-', 0.6); g.put(x, ROWS - 1, '-', 0.6); g.put(x, 15, '-', 0.6)
    for y in list(range(1, 15)) + list(range(16, ROWS - 1)):
        g.put(0, y, '|', 0.6); g.put(COLS - 1, y, '|', 0.6)
    lines = wrap_title(title)
    for i, l in enumerate(lines):
        g.text((COLS - len(l)) // 2, 16 + i, l, 1.0)
    badge = '[ %s ]' % tag.upper() if tag else ''
    g.text((COLS - len(badge)) // 2, 18, badge, 0.55)

def make_grid(name, tag, style=None):
    title = title_of(name)
    seed = fnv1a(title)
    rng = Rng(seed)
    s = (seed >> 8) % 4 if style is None else style
    g = Grid(COLS, ROWS)
    if s == 3: style_mono(g, rng, title)
    else: STYLES[s](g, rng)
    frame(g, title, tag)
    return g, s

# ---------------------------------------------------------------- picture -> characters
def grid_from_image(path, name, tag, cols=60, rows=26):
    img = Image.open(path).convert('L')
    cell_aspect = 2.0
    iw, ih = img.size
    # fit inside cols x (rows*cell_aspect) keeping the aspect
    target_w = cols
    target_h = max(1, int(round(target_w * ih / iw / cell_aspect)))
    if target_h > rows:
        target_h = rows
        target_w = max(1, int(round(rows * cell_aspect * iw / ih)))
    from PIL import ImageOps, ImageFilter
    img = ImageOps.equalize(img)                       # use the whole range: a flat sky should not be all dots
    edges = img.filter(ImageFilter.FIND_EDGES)
    img = Image.blend(img, ImageOps.autocontrast(edges), 0.35)   # outlines make shapes read in ASCII
    small = img.resize((target_w, target_h), Image.LANCZOS)
    px = list(small.get_flattened_data() if hasattr(small, 'get_flattened_data') else small.getdata())
    lo, hi = min(px), max(px)
    g = Grid(cols, rows + 3)
    ox, oy = (cols - target_w) // 2, (rows - target_h) // 2
    for y in range(target_h):
        for x in range(target_w):
            v = (px[y * target_w + x] - lo) / float(max(1, hi - lo))
            v = v ** 0.8
            g.put(ox + x, oy + y, ramp(v), 0.15 + 0.85 * v)
    lines = wrap_title(title_of(name), cols - 2)
    for i, l in enumerate(lines):
        g.text((cols - len(l)) // 2, rows + i, l, 1.0)
    return g

# ---------------------------------------------------------------- drawing
def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def render(g, theme='stock', cw=9, ch=18, size=15, transparent=True):
    bg, bright, dim = THEMES[theme]
    font = ImageFont.truetype(FONT, size)
    im = Image.new('RGBA', (g.cols * cw, g.rows * ch), (0, 0, 0, 0) if transparent else bg + (255,))
    d = ImageDraw.Draw(im)
    for y in range(g.rows):
        for x in range(g.cols):
            c = g.ch[y][x]
            if c != ' ':
                d.text((x * cw, y * ch), c, font=font, fill=lerp(dim, bright, g.lv[y][x]) + (255,))
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0) if transparent else bg + (255,))
    out.paste(im, ((W - im.width) // 2, (H - im.height) // 2), im)
    return out

SAMPLES = [
    ('Apotris.gba', 'GBA'), ('Pokemon ROWE (v2.02).gba', 'GBA'), ('Pokemon Heart and Soul (v1.2.1).gba', 'GBA'),
    ('Yazzie.bin', 'MD'), ('Rocket Knight Adventures.md', 'MD'), ('NHL 95 All-Time (Widescreen).bin', 'MD'),
    ('Mega Man X.sfc', 'SFC'), ('Legend of Zelda - 18 Hours Past (v1.12).sfc', 'SFC'), ('Illusion of Gaia Retranslated (v1.1).sfc', 'SFC'),
    ('Super Baseball Simulator 1000.sfc', 'SFC'), ('Sanrio World Smash Ball (English).sfc', 'SFC'),
    ('Dragon\'s Lair.gbc', 'GBC'), ('Black Castle.gb', 'GBC'), ('Tobu Tobu Girl Deluxe.gb', 'GBC'), ('Traumatarium Penitent.gb', 'GBC'),
    ('Super Mario Land 2 DX - 6 Golden Coins.gb', 'GBC'), ('Mega Man World 5 DX.gb', 'GBC'), ('Pocket Bomberman.gbc', 'GBC'),
    ('Castlevania - Symphony of the Night.chd', 'PS'), ('Mega Man (USA).nes', 'FC'), ('Wendy - Every Witch Way.gbc', 'GBC'),
    ('GB Wordyl.gb', 'GBC'), ('Donkey Kong.gb', 'GBC'), ('Chromatic Tetris.gbc', 'GBC'), ('Pandoras Blocks.gbc', 'GBC'),
]

def sheet(out, theme='stock', cols=5, tile=240):
    bg = THEMES[theme][0]
    rows = (len(SAMPLES) + cols - 1) // cols
    im = Image.new('RGB', (cols * tile, rows * tile), bg)
    for i, (n, t) in enumerate(SAMPLES):
        g, s = make_grid(n, t)
        a = render(g, theme).resize((tile, tile), Image.LANCZOS)
        im.paste(a, ((i % cols) * tile, (i // cols) * tile), a)
    im.save(out)

def main(argv):
    cmd = argv[1]
    if cmd == 'sheet':
        theme = argv[3] if len(argv) > 3 else 'stock'
        sheet(argv[2], theme)
    elif cmd == 'one':
        name, tag, out = argv[2], argv[3], argv[4]
        style = int(argv[5]) if len(argv) > 5 else None
        g, s = make_grid(name, tag, style)
        render(g, argv[6] if len(argv) > 6 else 'stock').save(out)
        print(title_of(name), '->', STYLE_NAMES[s], hex(fnv1a(title_of(name))))
    elif cmd == 'image':
        g = grid_from_image(argv[2], argv[3], argv[4])
        render(g, 'stock', cw=6, ch=12, size=10).save(argv[5])
    elif cmd == 'mock':
        base = Image.open('/Users/ducko/powkiddy-rgb20sx/evidence/padding-compare/launcher-padding-20/list.png').convert('RGBA')
        g, s = make_grid('Rocket Knight Adventures.md', 'MD')
        a = render(g, 'stock')
        base.paste(a, (max(0, base.width - a.width), (base.height - a.height) // 2), a)
        base.save(argv[2])

if __name__ == '__main__':
    main(sys.argv)
