"""Generates every icon TailDrop ships: the app icon, theme-aware tray glyphs,
Explorer menu device glyphs and the sparse-package logos.

    python tools/make_icons.py

Everything is drawn procedurally at 8x and downsampled, so the output is
reproducible and there are no binary design sources to keep in sync.

The app icon follows Fluent's colorful icon style: a few layered shapes in one
hue family (teal/cyan), lit from the top, with a soft contact shadow.
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
PKG = os.path.join(ROOT, "package", "Assets")
SS = 8  # supersampling factor
FLUENT = r"C:\Windows\Fonts\SegoeIcons.ttf"

# Fluent teal/cyan ramp.
DROP_TOP = (0x7A, 0xF0, 0xE6)
DROP_MID = (0x1C, 0xC6, 0xC8)
DROP_BOTTOM = (0x0A, 0x8E, 0xA8)
TRAY_TOP = (0x2C, 0x93, 0xA8)
TRAY_BOTTOM = (0x14, 0x62, 0x78)
TRAY_RIM = (0x4F, 0xC9, 0xD6)
WELL = (0x0D, 0x4B, 0x5D)
BADGE = (0x0F, 0xA8, 0xB8)
AMBER = (0xF7, 0xA6, 0x00)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgradient(S, stops, y0=0.0, y1=1.0):
    """Vertical gradient image; stops are (position, rgb) in 0..1 of [y0, y1]."""
    g = Image.new("RGBA", (1, S))
    for y in range(S):
        t = min(1.0, max(0.0, (y / (S - 1) - y0) / max(1e-6, y1 - y0)))
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                g.putpixel((0, y), lerp(c0, c1, (t - p0) / max(1e-6, p1 - p0)) + (255,))
                break
    return g.resize((S, S))


def droplet_mask(size, r_frac, cy_frac, arrow=True, cx_frac=0.5):
    """Teardrop pointing up, optionally with a down-arrow cut out of the bulb.

    r_frac/cy_frac give the bulb radius and centre as fractions of the icon;
    the tip sits 2.05 radii above the centre.
    """
    S = size * SS
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    r = r_frac * S
    cx, cy = S * cx_frac, S * cy_frac
    tip = (cx, cy - 2.05 * r)
    theta = math.acos(r / (cy - tip[1]))
    a1, a2 = -math.pi / 2 + theta, -math.pi / 2 - theta
    p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
    p2 = (cx + r * math.cos(a2), cy + r * math.sin(a2))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.polygon([tip, p1, p2], fill=255)
    if arrow:
        d.bitmap((0, 0), arrow_mask(size, r_frac, cy_frac, cx_frac), fill=0)
    return m


def arrow_mask(size, r_frac, cy_frac, cx_frac=0.5, weight=0.17):
    S = size * SS
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    r = r_frac * S
    cx, cy = S * cx_frac, S * cy_frac
    w = max(r * weight, SS * 1.15)
    top, bot = cy - r * 0.55, cy + r * 0.50
    d.rounded_rectangle([cx - w / 2, top, cx + w / 2, bot - w * 0.3], radius=w / 2, fill=255)
    hw = r * 0.46
    head_y = bot - hw * 0.95
    d.line([(cx - hw, head_y), (cx, bot)], fill=255, width=int(w))
    d.line([(cx + hw, head_y), (cx, bot)], fill=255, width=int(w))
    for px, py in ((cx - hw, head_y), (cx + hw, head_y), (cx, bot)):
        d.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=255)
    return m


def layer(canvas, fill, mask):
    """Composites `fill` (an RGBA image or rgb tuple) through `mask` onto canvas."""
    if isinstance(fill, tuple):
        fill = Image.new("RGBA", canvas.size, fill + (255,))
    piece = fill.copy()
    piece.putalpha(ImageChops.multiply(fill.getchannel("A"), mask))
    canvas.alpha_composite(piece)


def shadow(canvas, mask, offset, blur, alpha):
    S = canvas.size[0]
    sh = Image.new("L", (S, S), 0)
    sh.paste(mask, (0, int(offset)))
    sh = sh.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * alpha))
    layer(canvas, (0, 0, 0), sh)


def app_icon(size):
    S = size * SS
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    small = size <= 24
    if small:
        # Just the droplet: the tray turns to mush below 32 px.
        r, cy = 0.315, 0.655
        drop = droplet_mask(size, r, cy, arrow=False)
        shadow(out, drop, S * 0.02, S * 0.02, 0.25)
        layer(out, vgradient(S, [(0, DROP_TOP), (0.5, DROP_MID), (1, DROP_BOTTOM)], 0.05, 0.95), drop)
        layer(out, (255, 255, 255), arrow_mask(size, r, cy, weight=0.2))
        return out.resize((size, size), Image.LANCZOS)

    # Tray: rounded base with a dark well the droplet falls into.
    tray = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tray).rounded_rectangle([S * 0.12, S * 0.62, S * 0.88, S * 0.94], radius=S * 0.10, fill=255)
    shadow(out, tray, S * 0.025, S * 0.03, 0.28)
    layer(out, vgradient(S, [(0, TRAY_TOP), (1, TRAY_BOTTOM)], 0.62, 0.94), tray)
    rim = Image.new("L", (S, S), 0)
    ImageDraw.Draw(rim).rounded_rectangle([S * 0.12, S * 0.62, S * 0.88, S * 0.94], radius=S * 0.10,
                                          outline=255, width=max(1, int(S * 0.012)))
    rim = ImageChops.multiply(rim, vgradient(S, [(0, (255,) * 3), (1, (0,) * 3)], 0.62, 0.77).convert("L"))
    layer(out, TRAY_RIM, rim)
    well = Image.new("L", (S, S), 0)
    ImageDraw.Draw(well).rounded_rectangle([S * 0.25, S * 0.675, S * 0.75, S * 0.80], radius=S * 0.062, fill=255)
    layer(out, WELL, well)

    # Droplet, with a contact shadow on the tray and a soft top-left highlight.
    r, cy = 0.235, 0.52
    drop = droplet_mask(size, r, cy, arrow=False)
    shadow(out, drop, S * 0.03, S * 0.025, 0.35)
    layer(out, vgradient(S, [(0, DROP_TOP), (0.55, DROP_MID), (1, DROP_BOTTOM)], 0.04, 0.76), drop)
    hl = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hl).ellipse([S * 0.33, S * 0.34, S * 0.49, S * 0.56], fill=255)
    hl = ImageChops.multiply(hl.filter(ImageFilter.GaussianBlur(S * 0.03)), drop).point(lambda v: int(v * 0.45))
    layer(out, (255, 255, 255), hl)
    layer(out, (255, 255, 255), arrow_mask(size, r, cy))
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
    for theme, col in (("light", LIGHT_GLYPH), ("dark", DARK_GLYPH)):
        write_ico(os.path.join(ASSETS, f"tray-{theme}.ico"), [tray_icon(s, col) for s in tray_sizes])
        write_ico(os.path.join(ASSETS, f"tray-{theme}-badge.ico"), [tray_icon(s, col, BADGE) for s in tray_sizes])
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
