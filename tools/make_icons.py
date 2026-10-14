"""Generates every icon TailDrop ships: the app icon, theme-aware tray glyphs,
Explorer menu device glyphs and the sparse-package logos.

    python tools/make_icons.py

Everything is drawn procedurally at 8x and downsampled, so the output is
reproducible and there are no binary design sources to keep in sync.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
PKG = os.path.join(ROOT, "package", "Assets")
SS = 8  # supersampling factor

TOP_LEFT = (0x3D, 0x8B, 0xFF)
BOTTOM_RIGHT = (0x6A, 0x3D, 0xF5)
FLUENT = r"C:\Windows\Fonts\SegoeIcons.ttf"


def droplet_mask(size, r_frac, cy_frac, arrow=True):
    """Teardrop pointing up with a down-arrow cut out of the bulb.

    r_frac/cy_frac give the bulb radius and centre as fractions of the icon;
    the tip sits 2.05 radii above the centre.
    """
    S = size * SS
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    r = r_frac * S
    cx, cy = S / 2, S * cy_frac
    tip = (cx, cy - 2.05 * r)
    theta = math.acos(r / (cy - tip[1]))
    a1, a2 = -math.pi / 2 + theta, -math.pi / 2 - theta
    p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
    p2 = (cx + r * math.cos(a2), cy + r * math.sin(a2))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.polygon([tip, p1, p2], fill=255)
    if arrow:
        w = max(r * 0.17, SS * 1.15)
        top, bot = cy - r * 0.55, cy + r * 0.50
        d.rounded_rectangle([cx - w / 2, top, cx + w / 2, bot - w * 0.3], radius=w / 2, fill=0)
        hw = r * 0.46
        head_y = bot - hw * 0.95
        d.line([(cx - hw, head_y), (cx, bot)], fill=0, width=int(w))
        d.line([(cx + hw, head_y), (cx, bot)], fill=0, width=int(w))
        for px, py in ((cx - hw, head_y), (cx + hw, head_y), (cx, bot)):
            d.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=0)
    return m


def gradient(S):
    g = Image.new("RGBA", (S, S))
    px = g.load()
    for y in range(S):
        for x in range(S):
            t = (x + y) / (2 * (S - 1))
            px[x, y] = tuple(int(TOP_LEFT[i] + (BOTTOM_RIGHT[i] - TOP_LEFT[i]) * t) for i in range(3)) + (255,)
    return g


_grad_cache = {}


def app_icon(size):
    S = size * SS
    if S not in _grad_cache:
        _grad_cache[S] = gradient(S)
    bg = _grad_cache[S].copy()
    shape = Image.new("L", (S, S), 0)
    inset = S * (0.04 if size > 24 else 0.0)
    ImageDraw.Draw(shape).rounded_rectangle([inset, inset, S - inset, S - inset], radius=S * 0.24, fill=255)
    # soft top highlight
    hl = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hl).ellipse([-S * 0.3, -S * 0.75, S * 1.3, S * 0.45], fill=40)
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    bg = Image.composite(white, bg, hl)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(bg, (0, 0), shape)
    drop = droplet_mask(size, 0.255, 0.60) if size > 24 else droplet_mask(size, 0.28, 0.61)
    out.paste((255, 255, 255, 255), (0, 0), drop)
    return out.resize((size, size), Image.LANCZOS)


def tray_icon(size, color, badge=None, dim=False):
    S = size * SS
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    drop = droplet_mask(size, 0.305, 0.665)
    if dim:
        drop = drop.point(lambda v: v * 0.55)
    out.paste(color + (255,), (0, 0), drop)
    if badge:
        br = S * 0.21
        bx, by = S - br - S * 0.02, S - br - S * 0.02
        ring = Image.new("L", (S, S), 0)
        ImageDraw.Draw(ring).ellipse([bx - br * 1.35, by - br * 1.35, bx + br * 1.35, by + br * 1.35], fill=255)
        out.paste((0, 0, 0, 0), (0, 0), ring)  # transparent gap around the badge
        dot = Image.new("L", (S, S), 0)
        ImageDraw.Draw(dot).ellipse([bx - br, by - br, bx + br, by + br], fill=255)
        out.paste(badge + (255,), (0, 0), dot)
    return out.resize((size, size), Image.LANCZOS)


def glyph_icon(size, ch, color):
    S = size * SS
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    f = ImageFont.truetype(FLUENT, int(S * 0.94))
    d = ImageDraw.Draw(out)
    l, t, r, b = d.textbbox((0, 0), ch, font=f)
    d.text(((S - (r - l)) / 2 - l, (S - (b - t)) / 2 - t), ch, font=f, fill=color + (255,))
    return out.resize((size, size), Image.LANCZOS)


def write_ico(path, imgs):
    import io
    import struct
    blobs = []
    for im in imgs:
        b = io.BytesIO()
        im.save(b, format="PNG")
        blobs.append(b.getvalue())
    hdr = struct.pack("<HHH", 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    entries = b""
    for im, blob in zip(imgs, blobs):
        w, h = im.size
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    with open(path, "wb") as f:
        f.write(hdr + entries + b"".join(blobs))


def main():
    os.makedirs(ASSETS, exist_ok=True)
    os.makedirs(PKG, exist_ok=True)
    app_sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    write_ico(os.path.join(ASSETS, "app.ico"), [app_icon(s) for s in app_sizes])
    app_icon(256).save(os.path.join(ASSETS, "app-256.png"))

    tray_sizes = [16, 20, 24, 32, 40, 48, 64]
    LIGHT_GLYPH, DARK_GLYPH = (0x1B, 0x1B, 0x1B), (0xFF, 0xFF, 0xFF)
    BLUE, AMBER = (0x3D, 0x8B, 0xFF), (0xF7, 0xA6, 0x00)
    for theme, col in (("light", LIGHT_GLYPH), ("dark", DARK_GLYPH)):
        write_ico(os.path.join(ASSETS, f"tray-{theme}.ico"), [tray_icon(s, col) for s in tray_sizes])
        write_ico(os.path.join(ASSETS, f"tray-{theme}-badge.ico"), [tray_icon(s, col, BLUE) for s in tray_sizes])
        write_ico(os.path.join(ASSETS, f"tray-{theme}-off.ico"), [tray_icon(s, col, AMBER, dim=True) for s in tray_sizes])

    glyphs = {"pc": "\ue7f4", "laptop": "\ue7f8", "phone": "\ue8ea", "tablet": "\ue70a",
              "server": "\ue968", "other": "\ue8cc", "more": "\ue712"}
    menu_sizes = [16, 20, 24, 32, 40, 48]
    for name, ch in glyphs.items():
        for theme, col in (("light", (0x1B, 0x1B, 0x1B)), ("dark", (0xF3, 0xF3, 0xF3))):
            write_ico(os.path.join(ASSETS, f"dev-{name}-{theme}.ico"),
                      [glyph_icon(s, ch, col) for s in menu_sizes])

    for name, size in (("Square44x44Logo.png", 44), ("Square150x150Logo.png", 150), ("StoreLogo.png", 50)):
        if size >= 100:
            canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            inner = app_icon(int(size * 0.66))
            canvas.paste(inner, ((size - inner.width) // 2, (size - inner.height) // 2), inner)
            canvas.save(os.path.join(PKG, name))
        else:
            app_icon(size).save(os.path.join(PKG, name))
    print("icons written to", ASSETS)


if __name__ == "__main__":
    main()
