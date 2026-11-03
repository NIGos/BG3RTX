"""Offline B1 export. Python 3.11 + Pillow 12.3.0; no SVG engine or network.

The deliberately small SVG vocabulary is polygons in a square viewBox. Fail closed
on unsupported drawing elements rather than silently producing an incomplete icon.
"""
import argparse
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET

import PIL
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "branding"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
PILLOW_VERSION = "12.3.0"
NS = "{http://www.w3.org/2000/svg}"


def render(source, size, ink=None):
    root = ET.parse(source).getroot()
    box = [float(v) for v in root.attrib["viewBox"].split()]
    if box[:2] != [0, 0] or box[2] != box[3]:
        raise ValueError("Expected square zero-origin viewBox")
    scale = size * 8 / box[2]
    image = Image.new("RGBA", (size * 8, size * 8))
    draw = ImageDraw.Draw(image)
    for node in root:
        if node.tag in (NS + "title", NS + "desc"):
            continue
        if node.tag != NS + "polygon" or set(node.attrib) != {"fill", "points"}:
            raise ValueError("Unsupported SVG drawing element or attributes")
        points = [tuple(float(c) * scale for c in p.split(","))
                  for p in node.attrib["points"].split()]
        # Pillow polygons include the boundary pixel. Sampling at subpixel centres
        # keeps integer SVG edges on the pixel grid before the 8x box integration.
        points = [(x - 0.5, y - 0.5) for x, y in points]
        draw.polygon(points, fill=ink or node.attrib["fill"])
    return image.resize((size, size), Image.Resampling.BOX)


def png(image):
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=False, compress_level=9)
    return stream.getvalue()


def surface():
    # Integer hash field, seed 0xB13A, periodic at 256 pixels. No source imagery.
    image = Image.new("RGB", (256, 256))
    pixels = []
    for y in range(256):
        for x in range(256):
            n = ((x * 374761393 + y * 668265263) ^ 0xB13A) & 0xFFFFFFFF
            n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
            noise = ((n ^ (n >> 16)) % 5) - 2
            pixels.append(tuple(c + noise for c in (23, 25, 29)))
    image.putdata(pixels)
    return image


def outputs():
    result = {}
    icons = {}
    for size in sorted(set(SIZES + (512,))):
        source = ASSETS / ("bg3rtx-small.svg" if size <= 48 else "bg3rtx.svg")
        icons[size] = render(source, size)
        result[f"bg3rtx-{size}.png"] = png(icons[size])
    # Explicit directory preserves every requested frame (including 20/40/64).
    frames = [result[f"bg3rtx-{size}.png"] for size in SIZES]
    offset = 6 + 16 * len(SIZES)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(SIZES)))
    for size, data in zip(SIZES, frames):
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256,
                                     0, 0, 1, 32, len(data), offset))
        offset += len(data)
    result["bg3rtx.ico"] = bytes(directory) + b"".join(frames)
    result["surface-charcoal.png"] = png(surface())
    # Rows: colour, monochrome black, high-contrast white. Actual-size samples.
    sheet = Image.new("RGB", (800, 760), "#17191D")
    draw = ImageDraw.Draw(sheet)
    for column, background in enumerate(("#17191D", "#F1E7D2")):
        draw.rectangle((column * 400, 0, column * 400 + 399, 759), fill=background)
        for row, ink in enumerate((None, "#000000", "#FFFFFF")):
            # White on black / black on white for system high-contrast examples.
            for size, x, y in ((16, 16, 24), (32, 64, 24), (48, 120, 24)):
                source = ASSETS / "bg3rtx-small.svg"
                image = render(source, size, ink)
                sheet.paste(image, (column * 400 + x, row * 80 + y), image)
        image = icons[256]
        sheet.paste(image, (column * 400 + 32, 320), image)
        enlarged = icons[16].resize((128, 128), Image.Resampling.NEAREST)
        sheet.paste(enlarged, (column * 400 + 32, 608), enlarged)
    result["review-light-dark.png"] = png(sheet)
    # Editable monochrome masters for runtime system-colour substitution.
    for suffix, colour in (("mono", "#000000"), ("contrast", "#FFFFFF")):
        for stem in ("bg3rtx", "bg3rtx-small"):
            text = (ASSETS / f"{stem}.svg").read_text(encoding="utf-8")
            result[f"{stem}-{suffix}.svg"] = text.replace("#BEB5A4", colour).replace(
                "#D6AA62", colour).encode("utf-8")
    manifest = {"python": "3.11", "pillow": PILLOW_VERSION, "ico_sizes": SIZES,
                "sha256": {name: hashlib.sha256(data).hexdigest()
                           for name, data in sorted(result.items())}}
    for name in ("bg3rtx.svg", "bg3rtx-small.svg"):
        manifest["sha256"][name] = hashlib.sha256((ASSETS / name).read_bytes()).hexdigest()
    result["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Compare without writing")
    args = parser.parse_args()
    if os.name == "nt":
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    if sys.version_info[:2] != (3, 11) or PIL.__version__ != PILLOW_VERSION:
        parser.error(f"Use Python 3.11 and Pillow {PILLOW_VERSION}; no automatic installs")
    font_root = ROOT / "assets" / "fonts"
    font_manifest = json.loads((font_root / "manifest.json").read_text(encoding="utf-8"))
    for name, expected in font_manifest["sha256"].items():
        if hashlib.sha256((font_root / name).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"FAIL: font/licence identity {name}")
    generated = outputs()
    for name, data in generated.items():
        path = ASSETS / name
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"FAIL: stale or missing {path}")
        else:
            path.write_bytes(data)
    ico = Image.open(io.BytesIO(generated["bg3rtx.ico"]))
    if ico.ico.sizes() != {(s, s) for s in SIZES}:
        raise SystemExit("FAIL: ICO directory sizes")
    for size in SIZES:
        frame = ico.ico.getimage((size, size)).convert("RGBA")
        expected = Image.open(io.BytesIO(generated[f"bg3rtx-{size}.png"])).convert("RGBA")
        if frame.tobytes() != expected.tobytes() or frame.getpixel((0, 0))[3] != 0:
            raise SystemExit(f"FAIL: ICO RGBA round trip at {size}")
    print(f"PASS: {len(generated)} outputs; 9 ICO RGBA frames; "
          + ("byte-identical regeneration" if args.check else "exported"))


if __name__ == "__main__":
    main()
