"""Contact sheet of the final frame of each scene in captures/scene-sweep.json.

  python scripts/sweep_sheet.py [sweep.json] [out.jpg]
"""
import json, os, re, sys
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
caps = os.path.join(root, "captures")
sweep = sys.argv[1] if len(sys.argv) > 1 else os.path.join(caps, "scene-sweep.json")
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(caps, "scene-sweep.jpg")

tiles = []
for r in json.load(open(sweep, encoding="utf-8-sig")):
    if not r.get("Capture"):
        continue
    img = os.path.join(caps, r["Capture"], "img")
    # The presented frame: the last full-size RGB10A2 dump (UI composite target).
    cands = sorted((f for f in os.listdir(img) if f.endswith("_RGB10A2.png")),
                   key=lambda f: int(re.match(r"d(\d+)_", f).group(1)))
    if cands:
        tiles.append((f'{r["Id"]} {r["Save"]}', Image.open(os.path.join(img, cands[-1])).convert("RGB")))

W, H, cols = 640, 267, 3
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * W, rows * (H + 18)), "black")
d = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(tiles):
    x, y = (i % cols) * W, (i // cols) * (H + 18)
    sheet.paste(im.resize((W, H)), (x, y + 18))
    d.text((x + 4, y + 3), name, fill="white")
sheet.save(out, quality=88)
print(out, len(tiles))
