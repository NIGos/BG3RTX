"""Generate the Nexus Mods page graphics for BG3RTX.

Run from anywhere:  python docs/release/nexus/make_graphics.py
Screenshot pairs:   python docs/release/nexus/make_graphics.py --pairs <shots dir> [out dir]
  joins <name>_off.png + <name>_on.png into <name>_pair.jpg (default out:
  docs/release/nexus/screenshots/, which is not committed).
Needs Python 3 + Pillow only (no cairosvg, no network). Output goes to
docs/release/nexus/img/. Inputs are the project's own branding assets:
  assets/branding/bg3rtx-512.png          (emblem)
  assets/branding/surface-charcoal.png    (charcoal surface tile)
  assets/branding/ornaments/hero-lantern.png, corner.svg, divider.svg
  assets/fonts/LatoWeb-*.ttf              (UI font, OFL)
Display headings use Palatino Linotype (as the launcher mockup does) and fall
back to Georgia, then Lato, when it is not installed.
Palette: docs/release/VISUAL_STYLE.md and tools/launcher/ui/app.css.
"""
from __future__ import annotations

import math
import os
import re
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(HERE, "img")
BRAND = os.path.join(ROOT, "assets", "branding")
ORN = os.path.join(BRAND, "ornaments")
FONTS = os.path.join(ROOT, "assets", "fonts")

# Palette (VISUAL_STYLE.md / app.css)
BG = (23, 25, 29)          # #17191D
BG_DEEP = (18, 20, 23)     # #121417
PANEL = (34, 38, 44)       # #22262C
TEXT = (241, 231, 210)     # #F1E7D2
TEXT_2 = (190, 181, 164)   # #BEB5A4
BORDER = (69, 75, 84)      # #454B54
GOLD = (214, 170, 98)      # #D6AA62
GOLD_HI = (240, 201, 131)  # #F0C983
GOLD_STOPS = [(0.0, (241, 213, 160)), (0.35, (191, 148, 84)),
              (0.65, (111, 81, 46)), (1.0, (218, 180, 123))]

SS = 4  # supersampling factor for vector drawing


# ---------------------------------------------------------------- fonts
def _first_font(names, size):
    for n in names:
        for d in (FONTS, r"C:\Windows\Fonts"):
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default(size)


def lato(size, semibold=False):
    return _first_font(["LatoWeb-Semibold.ttf" if semibold else "LatoWeb-Regular.ttf"], size)


def display(size, bold=False, italic=False):
    pal = {(False, False): "pala.ttf", (True, False): "palab.ttf",
           (False, True): "palai.ttf", (True, True): "palabi.ttf"}[(bold, italic)]
    geo = {(False, False): "georgia.ttf", (True, False): "georgiab.ttf",
           (False, True): "georgiai.ttf", (True, True): "georgiaz.ttf"}[(bold, italic)]
    return _first_font([pal, geo, "LatoWeb-Semibold.ttf"], size)


def text_spaced(draw, xy, text, font, fill, tracking=0.0, anchor_y="ls"):
    """Draw text with extra letter spacing (tracking in px). Returns end x."""
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill, anchor=anchor_y)
        x += font.getlength(ch) + tracking
    return x - tracking


def spaced_width(text, font, tracking):
    return sum(font.getlength(c) for c in text) + tracking * (len(text) - 1)


# ---------------------------------------------------------------- tiny SVG path renderer
_TOK = re.compile(r"[MmLlHhVvCcSsQqTtZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def _bez(p0, p1, p2, p3=None, n=24):
    pts = []
    for i in range(1, n + 1):
        t = i / n
        if p3 is None:
            a = (1 - t) ** 2; b = 2 * (1 - t) * t; c = t * t
            pts.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))
        else:
            a = (1 - t) ** 3; b = 3 * (1 - t) ** 2 * t; c = 3 * (1 - t) * t * t; d = t ** 3
            pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                        a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return pts


def parse_path(d):
    """Return a list of (points, closed) subpaths. Supports M L H V C S Q T Z (abs/rel)."""
    toks = _TOK.findall(d)
    i = 0
    subs = []
    cur = None
    pos = (0.0, 0.0)
    start = (0.0, 0.0)
    cmd = None
    last_ctrl = None
    last_cmd = None

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]; i += 1
            if cmd in "Zz":
                if cur:
                    subs.append((cur, True))
                cur = None
                pos = start
                last_cmd = cmd
                continue
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if c == "M":
            if cur:
                subs.append((cur, False))
            pos = (ox + num(), oy + num()); start = pos; cur = [pos]
            cmd = "l" if rel else "L"
            last_ctrl = None
        elif c == "L":
            pos = (ox + num(), oy + num()); cur.append(pos); last_ctrl = None
        elif c == "H":
            pos = ((pos[0] if rel else 0) + num(), pos[1]); cur.append(pos); last_ctrl = None
        elif c == "V":
            pos = (pos[0], (pos[1] if rel else 0) + num()); cur.append(pos); last_ctrl = None
        elif c == "C":
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cur.extend(_bez(pos, p1, p2, p3)); pos = p3; last_ctrl = ("C", p2)
        elif c == "S":
            if last_ctrl and last_ctrl[0] == "C":
                p1 = (2 * pos[0] - last_ctrl[1][0], 2 * pos[1] - last_ctrl[1][1])
            else:
                p1 = pos
            p2 = (ox + num(), oy + num()); p3 = (ox + num(), oy + num())
            cur.extend(_bez(pos, p1, p2, p3)); pos = p3; last_ctrl = ("C", p2)
        elif c == "Q":
            p1 = (ox + num(), oy + num()); p2 = (ox + num(), oy + num())
            cur.extend(_bez(pos, p1, p2)); pos = p2; last_ctrl = ("Q", p1)
        elif c == "T":
            if last_ctrl and last_ctrl[0] == "Q":
                p1 = (2 * pos[0] - last_ctrl[1][0], 2 * pos[1] - last_ctrl[1][1])
            else:
                p1 = pos
            p2 = (ox + num(), oy + num())
            cur.extend(_bez(pos, p1, p2)); pos = p2; last_ctrl = ("Q", p1)
        else:
            raise ValueError("unsupported path command " + cmd)
        last_cmd = cmd
    if cur:
        subs.append((cur, False))
    return subs


def gradient_image(w, h, stops=GOLD_STOPS):
    """Diagonal (0,0)->(1,1) linear gradient over the whole canvas, like the SVG defs."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    # project onto the diagonal of the normalised box
    for y in range(h):
        for x in range(w):
            t = (x / max(w - 1, 1) + y / max(h - 1, 1)) / 2
            for k in range(len(stops) - 1):
                t0, c0 = stops[k]; t1, c1 = stops[k + 1]
                if t <= t1:
                    f = (t - t0) / (t1 - t0) if t1 > t0 else 0
                    px[x, y] = tuple(int(c0[j] + (c1[j] - c0[j]) * f) for j in range(3))
                    break
    return img


def _gradient_fast(w, h):
    small = gradient_image(64, 64)
    return small.resize((w, h), Image.BILINEAR)


def _hex(c):
    c = c.lstrip("#")
    if len(c) == 3:
        c = "".join(ch * 2 for ch in c)
    return tuple(int(c[k:k + 2], 16) for k in (0, 2, 4))


def render_svg(path, scale=1.0, tint=None):
    """Rasterise an ornament SVG (paths only) to RGBA at `scale`."""
    tree = ET.parse(path)
    root = tree.getroot()
    ns = "{http://www.w3.org/2000/svg}"
    vw, vh = float(root.get("width")), float(root.get("height"))
    W, H = int(round(vw * scale)), int(round(vh * scale))
    S = scale * SS
    canvas = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    grad = _gradient_fast(W * SS, H * SS) if tint is None else Image.new("RGB", (W * SS, H * SS), tint)

    def walk(el, inh):
        attrs = dict(inh)
        for k in ("fill", "stroke", "stroke-width", "opacity"):
            if el.get(k) is not None:
                attrs[k] = el.get(k)
        tag = el.tag.replace(ns, "")
        if tag == "path":
            draw_path(el.get("d"), attrs)
        for ch in el:
            if ch.tag.replace(ns, "") in ("defs", "title"):
                continue
            walk(ch, attrs)

    def paint_src(spec):
        if spec is None or spec == "none":
            return None
        if spec.startswith("url("):
            return grad
        col = _hex(spec) if tint is None else tint
        return Image.new("RGB", canvas.size, col)

    def draw_path(d, attrs):
        nonlocal canvas
        subs = parse_path(d)
        fill = paint_src(attrs.get("fill", "#000"))
        stroke = paint_src(attrs.get("stroke"))
        sw = float(attrs.get("stroke-width", 1)) * S
        if fill is not None:
            m = Image.new("L", canvas.size, 0)
            dm = ImageDraw.Draw(m)
            for pts, _closed in subs:
                if len(pts) > 2:
                    dm.polygon([(x * S, y * S) for x, y in pts], fill=255)
            layer = fill.convert("RGBA"); layer.putalpha(m)
            canvas = Image.alpha_composite(canvas, layer)
        if stroke is not None:
            m = Image.new("L", canvas.size, 0)
            dm = ImageDraw.Draw(m)
            r = sw / 2
            for pts, closed in subs:
                p = [(x * S, y * S) for x, y in pts]
                if closed:
                    p = p + [p[0]]
                if len(p) > 1:
                    dm.line(p, fill=255, width=max(1, int(round(sw))), joint="curve")
                for (x, y) in (p[0], p[-1]):
                    dm.ellipse((x - r, y - r, x + r, y + r), fill=255)
            layer = stroke.convert("RGBA"); layer.putalpha(m)
            canvas = Image.alpha_composite(canvas, layer)

    walk(root, {"fill": "#000"})
    return canvas.resize((W, H), Image.LANCZOS)


# ---------------------------------------------------------------- helpers
def charcoal(w, h, base=None):
    tile = Image.open(os.path.join(BRAND, "surface-charcoal.png")).convert("RGB")
    img = Image.new("RGB", (w, h))
    for y in range(0, h, tile.height):
        for x in range(0, w, tile.width):
            img.paste(tile, (x, y))
    return img


def vignette(img, strength=0.45):
    w, h = img.size
    m = Image.new("L", (w // 4 + 1, h // 4 + 1), 0)
    px = m.load()
    for y in range(m.height):
        for x in range(m.width):
            dx = (x / (m.width - 1) - 0.5) * 2
            dy = (y / (m.height - 1) - 0.5) * 2
            v = min(1.0, math.sqrt(dx * dx * 0.6 + dy * dy * 0.9))
            px[x, y] = int(255 * strength * v ** 2)
    m = m.resize((w, h), Image.BILINEAR)
    dark = Image.new("RGB", (w, h), (5, 7, 9))
    return Image.composite(dark, img, m)


def emblem(size):
    return Image.open(os.path.join(BRAND, "bg3rtx-512.png")).convert("RGBA").resize((size, size), Image.LANCZOS)


def gem(draw, cx, cy, r, s=1):
    """Faceted amber diamond like divider.svg (drawn at supersample s)."""
    draw.polygon([(cx, cy - r), (cx + r * 0.9, cy), (cx, cy + r), (cx - r * 0.9, cy)],
                 fill=(181, 129, 61), outline=(232, 197, 141), width=max(1, s))
    draw.polygon([(cx, cy - r * 0.72), (cx, cy + r * 0.72), (cx - r * 0.63, cy)], fill=(241, 212, 155))
    draw.polygon([(cx, cy - r * 0.72), (cx + r * 0.63, cy), (cx, cy)], fill=(216, 173, 103))


def corner_frame(img, inset, size, scale):
    """Gold corner ornaments (corner.svg rotated) and a thin double rule."""
    c = render_svg(os.path.join(ORN, "corner.svg"), scale)
    w, h = img.size
    base = img.convert("RGBA")
    pos = [(inset, inset, 0), (w - inset - c.width, inset, 270),
           (w - inset - c.width, h - inset - c.height, 180), (inset, h - inset - c.height, 90)]
    for x, y, rot in pos:
        base.alpha_composite(c.rotate(rot), (x, y))
    return base


def save(img, name, colors=None):
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name)
    if colors:
        img = img.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    img.save(p, optimize=True)
    print(f"{name:28s} {img.size[0]}x{img.size[1]}  {os.path.getsize(p) / 1024:7.1f} KiB")
    return p


def hero_crop(w, h, focus_y=0.42, zoom=1.0, right=1.0):
    """Crop the hero painting; `right` = right edge of the crop as a fraction of its width."""
    hero = Image.open(os.path.join(ORN, "hero-lantern.png")).convert("RGB")
    sw = int(w * zoom)
    sh = int(hero.height * sw / hero.width)
    hero = hero.resize((sw, sh), Image.LANCZOS)
    x0 = max(0, int(sw * right) - w)
    y0 = int(max(0, min(sh - h, focus_y * sh - h / 2)))
    return hero.crop((x0, y0, x0 + w, y0 + h))


def left_shade(img, reach=0.62, max_alpha=0.88, color=BG_DEEP):
    w, h = img.size
    m = Image.new("L", (w, 1))
    for x in range(w):
        t = x / (w * reach)
        a = max_alpha * max(0.0, 1 - t) ** 1.6 if t < 1 else 0
        m.putpixel((x, 0), int(255 * a))
    m = m.resize((w, h))
    return Image.composite(Image.new("RGB", (w, h), color), img, m)


def edge_rule(img, inset=6, color=GOLD, alpha=150):
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    w, h = img.size
    d.rectangle((inset, inset, w - 1 - inset, h - 1 - inset), outline=color + (alpha,), width=1)
    d.rectangle((inset + 4, inset + 4, w - 5 - inset, h - 5 - inset), outline=color + (alpha // 3,), width=1)
    return Image.alpha_composite(img.convert("RGBA"), over)


# ---------------------------------------------------------------- graphics
def make_banner():
    W, H = 1200, 400
    img = hero_crop(W, H, focus_y=0.455, zoom=1.06)
    img = left_shade(img, reach=0.70, max_alpha=0.93)
    img = vignette(img, 0.35)
    img = edge_rule(img, inset=8)
    img = corner_frame(img, 8, 0, 0.8)
    d = ImageDraw.Draw(img)

    x0 = 76
    em = emblem(104)
    img.alpha_composite(em, (x0, 92))
    word = lato(88, semibold=True)
    end = text_spaced(d, (x0 + 128, 178), "BG3RTX", word, TEXT, tracking=3)
    # beta tag
    tag_f = lato(15, semibold=True)
    tw = spaced_width("BETA", tag_f, 2.5)
    tx, ty = end + 18, 112
    d.rounded_rectangle((tx, ty, tx + tw + 20, ty + 26), radius=4, outline=GOLD, width=1)
    text_spaced(d, (tx + 10, ty + 19), "BETA", tag_f, GOLD_HI, tracking=2.5)

    d.text((x0 + 2, 252), "Ray-traced lighting for Baldur's Gate 3", font=display(36), fill=TEXT, anchor="ls")
    # divider under the tagline
    div = render_svg(os.path.join(ORN, "divider.svg"), 0.9)
    img.alpha_composite(div, (x0 - 4, 272))
    small = lato(16, semibold=True)
    text_spaced(d, (x0 + 2, 338), "VULKAN LAYER   \u00b7   DLSS RAY RECONSTRUCTION   \u00b7   NVIDIA RTX",
                small, GOLD_HI, tracking=2.2)
    save(img.convert("RGB"), "banner.png")


def make_sticky_banner():
    W, H = 1000, 190
    img = hero_crop(W, H, focus_y=0.33, zoom=1.45, right=0.71)
    img = left_shade(img, reach=0.8, max_alpha=0.94)
    img = vignette(img, 0.35)
    img = edge_rule(img, inset=6)
    img = corner_frame(img, 6, 0, 0.55)
    d = ImageDraw.Draw(img)
    img.alpha_composite(emblem(72), (60, 58))
    d.text((152, 98), "Welcome to the BG3RTX beta", font=display(38), fill=TEXT, anchor="ls")
    text_spaced(d, (154, 132), "PLEASE READ THIS BEFORE POSTING", lato(16, semibold=True), GOLD_HI, tracking=2.4)
    save(img.convert("RGB"), "sticky_banner.png")


def make_header(name, title):
    W, H = 1000, 64
    img = charcoal(W, H)
    img = vignette(img, 0.25).convert("RGBA")
    big = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    s = SS
    # thin border with angular notched corners
    n = 10 * s
    x1, y1, x2, y2 = 0.5 * s, 0.5 * s, W * s - 0.5 * s, H * s - 0.5 * s
    poly = [(x1 + n, y1), (x2 - n, y1), (x2, y1 + n), (x2, y2 - n), (x2 - n, y2), (x1 + n, y2), (x1, y2 - n), (x1, y1 + n), (x1 + n, y1)]
    d.line(poly, fill=BORDER + (255,), width=s)
    for (cx, cy, dx, dy) in ((x1, y1, 1, 1), (x2, y1, -1, 1), (x2, y2, -1, -1), (x1, y2, 1, -1)):
        d.line([(cx, cy + dy * n), (cx + dx * n, cy)], fill=GOLD + (255,), width=s * 2)
    # gold rule to the right of the title, ending in a gem
    tf = display(30)
    title_x = 70
    tw = tf.getlength(title)
    lx0 = (title_x + tw + 26) * s
    lx1 = (W - 52) * s
    cy = H * s / 2
    if lx1 - lx0 > 60 * s:
        d.line([(lx0, cy), (lx1 - 14 * s, cy)], fill=GOLD + (200,), width=s)
        d.line([(lx0 + 20 * s, cy + 5 * s), (lx1 - 40 * s, cy + 5 * s)], fill=GOLD + (90,), width=s)
        gem(d, lx1, cy, 9 * s, s)
    small = big.resize((W, H), Image.LANCZOS)
    img = Image.alpha_composite(img, small)
    img.alpha_composite(emblem(30), (24, 17))
    dd = ImageDraw.Draw(img)
    dd.text((title_x, 43), title, font=tf, fill=TEXT, anchor="ls")
    save(img.convert("RGB"), f"h_{name}.png", colors=None)


def make_divider():
    div = render_svg(os.path.join(ORN, "divider.svg"), 1.5)
    save(div, "divider.png")


def make_badges():
    """One row of fact badges for the top of the page."""
    items = ["PUBLIC BETA", "VULKAN", "NVIDIA RTX", "DLSS RR + FG", "NO GAME FILES CHANGED", "GPL-3.0"]
    f = lato(15, semibold=True)
    tr = 1.6
    pad, gap, h = 16, 12, 34
    widths = [spaced_width(t, f, tr) + 2 * pad for t in items]
    W = int(sum(widths) + gap * (len(items) - 1)) + 4
    big = Image.new("RGBA", (W * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    x = 2
    boxes = []
    for t, w in zip(items, widths):
        first = t == "PUBLIC BETA"
        d.rounded_rectangle((x * SS, 1 * SS, (x + w) * SS, (h - 1) * SS), radius=5 * SS,
                            fill=PANEL + (255,), outline=(GOLD if first else BORDER) + (255,), width=SS)
        boxes.append((x, t, w, first))
        x += w + gap
    img = big.resize((W, h), Image.LANCZOS)
    dd = ImageDraw.Draw(img)
    for x, t, w, first in boxes:
        text_spaced(dd, (x + pad, 23), t, f, GOLD_HI if first else TEXT, tracking=tr)
    save(img, "badges.png")


HEADERS = [
    ("features", "What it does"),
    ("screenshots", "Screenshots"),
    ("requirements", "Requirements"),
    ("install", "Installation"),
    ("presets", "Presets"),
    ("controls", "Controls and hotkeys"),
    ("limits", "Compatibility and known limits"),
    ("faq", "FAQ"),
    ("uninstall", "Uninstall"),
    ("bugs", "Reporting bugs"),
    ("licence", "Licence and credits"),
    ("read_first", "Please read first"),
    ("known_issues", "Known issues"),
    ("help", "Getting help"),
]


def make_pairs(src_dir, out_dir, width=1200):
    """Join <name>_off.<ext> and <name>_on.<ext> screenshots into <name>_pair.jpg.

    Both halves are scaled to the same height, framed in the page style and labelled
    OFF / ON. JPEG keeps a 1200 px pair well under 1 MB.
    """
    os.makedirs(out_dir, exist_ok=True)
    exts = (".png", ".jpg", ".jpeg", ".bmp", ".webp")
    names = sorted({f[:-len("_off") - len(os.path.splitext(f)[1])]
                    for f in os.listdir(src_dir)
                    if os.path.splitext(f)[0].endswith("_off") and f.lower().endswith(exts)})
    for name in names:
        def find(suffix):
            for e in exts:
                p = os.path.join(src_dir, name + suffix + e)
                if os.path.exists(p):
                    return Image.open(p).convert("RGB")
            return None
        off, on = find("_off"), find("_on")
        if off is None or on is None:
            print(f"skip {name}: needs both _off and _on")
            continue
        gap, pad = 6, 10
        half = (width - 2 * pad - gap) // 2
        hh = int(off.height * half / off.width)
        off = off.resize((half, hh), Image.LANCZOS)
        on = on.resize((half, int(on.height * half / on.width)), Image.LANCZOS).crop((0, 0, half, hh))
        img = Image.new("RGB", (width, hh + 2 * pad), BG)
        img.paste(off, (pad, pad)); img.paste(on, (pad + half + gap, pad))
        img = edge_rule(img, inset=2, alpha=120)
        d = ImageDraw.Draw(img)
        f = lato(16, semibold=True)
        for x, label, col in ((pad, "BG3RTX OFF", TEXT_2), (pad + half + gap, "BG3RTX ON", GOLD_HI)):
            tw = spaced_width(label, f, 2)
            d.rounded_rectangle((x + 12, pad + 12, x + 32 + tw, pad + 42), radius=4,
                                fill=(18, 20, 23, 215), outline=(GOLD if col == GOLD_HI else BORDER) + (255,))
            text_spaced(d, (x + 22, pad + 33), label, f, col, tracking=2)
        p = os.path.join(out_dir, name + "_pair.jpg")
        img.convert("RGB").save(p, quality=88, optimize=True, progressive=True)
        print(f"{name}_pair.jpg  {img.size[0]}x{img.size[1]}  {os.path.getsize(p) / 1024:.1f} KiB")


def main():
    import sys
    if len(sys.argv) >= 3 and sys.argv[1] == "--pairs":
        out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "screenshots")
        make_pairs(sys.argv[2], out)
        return
    make_banner()
    make_sticky_banner()
    make_divider()
    make_badges()
    for name, title in HEADERS:
        make_header(name, title)
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print(f"total {total / 1024:.1f} KiB in {OUT}")


if __name__ == "__main__":
    main()
