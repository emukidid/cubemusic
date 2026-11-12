#!/usr/bin/env python3
"""Makes opening.bnr: the 96x32 banner a loader (Swiss etc.) shows for
CubeMusic, plus its name and description. The image is the CubeMusic wordmark
("Cube" in the text colour, "Music" in the accent colour, M PLUS Rounded 1c
Bold) over a semi-transparent disc on the app's night-sky gradient.

    python3 tools/make_banner.py [--preview-only]

Writes opening.bnr and tools/preview_banner.png (plus an 8x enlargement,
tools/preview_banner_8x.png, for looking at). Needs Python 3 with Pillow and
NumPy."""
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, 'data', 'mplus.ttf')

SHORT_NAME = 'CubeMusic'
SHORT_MAKER = 'emu_kidid'
LONG_NAME = 'CubeMusic'
LONG_MAKER = 'emu_kidid'
DESCRIPTION = 'Play the music on your GameCube discs, and back it up to an SD card.'

# the app's colours (source/ui.c, source/sky.c)
C_TEXT = (238, 238, 252)
C_ACCENT_HI = (170, 155, 255)
SKY = [(6, 6, 18), (14, 12, 40), (30, 22, 78)]

S = 8                      # supersampling
W, H = 96 * S, 32 * S


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def sky():
    """the vertical gradient, with the faint violet glow along the bottom"""
    img = Image.new('RGBA', (W, H))
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        c = lerp(SKY[0], SKY[1], t / 0.55) if t < 0.55 else lerp(SKY[1], SKY[2], (t - 0.55) / 0.45)
        if t > 0.6:
            g = (t - 0.6) / 0.4 * 0.25
            c = lerp(c, (70, 50, 190), g)
        for x in range(W):
            px[x, y] = c + (255,)
    return img


def stars(img, keep_out, count=8, seed=11):
    """the sky's specks (source/sky.c): small pale-white dots, and a few bigger
    violet-white ones with a faint halo, scattered well apart and never on or
    right beside a letter (keep_out: a 96x32 bool mask). A fixed seed keeps the
    banner the same every time it's made."""
    rng = np.random.default_rng(seed)
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    placed = []
    while len(placed) < count:
        x, y = rng.uniform(1.5, 94.5), rng.uniform(1.5, 30.5)
        if any((x - px) ** 2 + (y - py) ** 2 < 15 ** 2 for px, py in placed):
            continue   # keep them well spread out
        if keep_out[int(y), int(x)]:
            continue   # and off the lettering, so it stays easy to read
        placed.append((x, y))
        k = rng.random() ** 2
        size = 0.7 + k * 2.6                # sky.c's speck radius range
        bright = rng.uniform(0.55, 1.0)
        a = int(255 * bright)
        big = size > 2
        c = (200, 190, 255) if big else (225, 228, 255)
        cx, cy = x * S, y * S
        r = (0.55 + size * 0.25) * S        # at 96x32 a speck is about a pixel or two
        if size > 2.2:
            hr = r * 2.6
            d.ellipse([cx - hr, cy - hr, cx + hr, cy + hr], fill=(120, 100, 255, a // 5))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c + (a,))
    img.alpha_composite(layer)


def disc(diameter, alpha):
    """a disc: a silvery, faintly iridescent ring with a clear hub and hole"""
    d = diameter
    yy, xx = np.mgrid[0:d, 0:d].astype(np.float32)
    cx = cy = (d - 1) / 2
    dx, dy = xx - cx, yy - cy
    r = np.hypot(dx, dy) / (d / 2)
    ang = np.arctan2(dy, dx)
    # base: cool silver with an iridescent sweep (two bands, like light on a disc)
    sheen = 0.5 + 0.5 * np.cos(2 * ang - 0.6)
    hue = (ang / (2 * math.pi) * 2 + r * 0.35) % 1.0
    rgb = np.stack([
        0.5 + 0.5 * np.cos(2 * math.pi * (hue + 0.00)),
        0.5 + 0.5 * np.cos(2 * math.pi * (hue + 0.33)),
        0.5 + 0.5 * np.cos(2 * math.pi * (hue + 0.66)),
    ], -1)
    silver = np.array([200, 200, 225], np.float32) / 255
    col = silver * (0.55 + 0.45 * sheen[..., None]) * 0.8 + rgb * 0.2 * sheen[..., None]
    a = np.zeros((d, d), np.float32)
    a[(r <= 0.92) & (r >= 0.30)] = 1.0                # the data area
    a[(r < 0.30) & (r >= 0.15)] = 0.55                # the clear hub
    # the clear band round the edge, where the two layers meet: mostly
    # see-through, lightly tinted, with a bright line at each side of it
    rim = (r <= 1.0) & (r > 0.92)
    a[rim] = 0.35
    col[rim] = np.array([215, 220, 245], np.float32) / 255
    for lo, hi, k in ((0.905, 0.935, 1.0), (0.965, 1.0, 1.0)):
        line = (r >= lo) & (r <= hi)
        a[line] = k
        col[line] = np.array([235, 238, 255], np.float32) / 255
    hubline = (r >= 0.29) & (r <= 0.32)
    col[hubline] = np.minimum(col[hubline] * 1.25, 1)
    out = np.dstack([col * 255, a[..., None] * 255 * alpha]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def wordmark(height_px):
    """'Cube' + 'Music' at a cap height that fits height_px; returns RGBA (tight)"""
    font = ImageFont.truetype(FONT, height_px)
    parts = [('Cube', C_TEXT), ('Music', C_ACCENT_HI)]
    widths = [font.getlength(t) for t, _ in parts]
    asc, desc = font.getmetrics()
    img = Image.new('RGBA', (int(sum(widths)) + 4 * S, asc + desc), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x = 0
    for (t, c), w in zip(parts, widths):
        d.text((x, 0), t, font=font, fill=c + (255,))
        x += w
    return img.crop(img.getbbox())


def banner_image():
    img = sky()

    # where the letters will go (worked out first, so the stars can avoid them)
    wm = wordmark(H)
    room = W - 2 * 5 * S
    scale = room / wm.width
    wm = wm.resize((room, round(wm.height * scale)), Image.LANCZOS)
    x, y = (W - wm.width) // 2, (H - wm.height) // 2
    mask = Image.new('L', (W, H), 0)
    mask.paste(wm.getchannel('A'), (x, y))
    mask = mask.resize((96, 32), Image.BOX).filter(ImageFilter.MaxFilter(7))
    stars(img, np.array(mask) > 40)

    # the disc, centred, the full height with a pixel to spare, half see-through
    dd = H - 2 * S
    img.alpha_composite(disc(dd, 0.55), ((W - dd) // 2, (H - dd) // 2))


    # a soft dark halo so the letters stay readable over the disc
    halo = Image.new('RGBA', img.size, (0, 0, 0, 0))
    shadow = Image.new('RGBA', wm.size, (6, 4, 20, 255))
    shadow.putalpha(wm.getchannel('A'))
    halo.alpha_composite(shadow, (x, y))
    halo = halo.filter(ImageFilter.MaxFilter(2 * S - 1)).filter(ImageFilter.GaussianBlur(S * 0.9))
    a = np.array(halo.getchannel('A'), np.float32) * 0.6
    halo.putalpha(Image.fromarray(a.clip(0, 255).astype(np.uint8)))
    img.alpha_composite(halo)
    img.alpha_composite(wm, (x, y))
    return img.resize((96, 32), Image.LANCZOS)


def rgb5a3(img):
    """the GameCube's RGB5A3 texture format, in 4x4 tiles"""
    a = np.array(img.convert('RGBA'))
    out = bytearray()
    for ty in range(0, 32, 4):
        for tx in range(0, 96, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, al = (int(v) for v in a[y, x])
                    if al >= 224:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                    else:
                        v = (al >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
                    out += struct.pack('>H', v)
    return bytes(out)


def field(s, n):
    b = s.encode('ascii')[:n - 1]
    return b + b'\0' * (n - len(b))


def main():
    img = banner_image()
    img.save(os.path.join(HERE, 'preview_banner.png'))
    img.resize((96 * 8, 32 * 8), Image.NEAREST).save(os.path.join(HERE, 'preview_banner_8x.png'))
    print('wrote tools/preview_banner.png and tools/preview_banner_8x.png')
    if '--preview-only' in sys.argv:
        return
    data = b'BNR1' + b'\0' * 0x1C + rgb5a3(img)
    data += field(SHORT_NAME, 0x20) + field(SHORT_MAKER, 0x20) + field(LONG_NAME, 0x40) + field(LONG_MAKER, 0x40) + field(DESCRIPTION, 0x80)
    assert len(data) == 0x1960
    open(os.path.join(ROOT, 'opening.bnr'), 'wb').write(data)
    print('wrote opening.bnr')


if __name__ == '__main__':
    main()
