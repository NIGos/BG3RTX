"""Hair BSDF comparison in the offline renderer: the same groom shaded three ways, with the CHAR_Hair defaults.

python -m tools.hair_groom.bsdf_compare --groom <groom.npz from python -m tools.hair_groom> --out <new folder>

  hit:     the current GI/reflection hit shading on hair (Lambert with Hair_Color, on the round fibre)
  game:    the game's camera-pixel hair shading (bg3_hair_spec + bg3_hair_diff, UE4-style Marschner)
  chiang:  tools/hair_groom/bsdf.py (Chiang 2016, sigma_a from Hair_Color)
All three see the same light (irradiance E), the same opacity shadow map and an ambient irradiance whose
response is the model's own single-scattering albedo (bsdf.furnace), so the brightness differences are the
models'. Writes <view>_hit_game_chiang.png and report.json (mean RGB and luminance on hair pixels, albedos).
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from . import bsdf, render
from .__main__ import HEAD, views

E = 2.5          # light irradiance, as render.shade's direct * 2.5
AMBIENT = 0.12   # ambient irradiance (uniform sphere)


def shader(model):
    p = bsdf.CHAR_HAIR
    albedo = bsdf.furnace(p["roughness"], p["color"], n=100000, model=model)

    def fn(tangent, view_dir, light_dir, shadow, root):
        n = len(tangent)
        out = np.empty((n, 3))
        h = np.random.default_rng(7).uniform(-1, 1, n)  # stochastic offset across the fibre, per fragment
        for c0 in range(0, n, 500000):
            s = slice(c0, c0 + 500000)
            t = tangent[s]
            wo = np.tile(view_dir, (len(t), 1))
            wi = np.tile(light_dir, (len(t), 1))
            if model == "chiang":
                S = bsdf.evaluate(t, wo, wi, h[s], p["color"], p["roughness"])
            elif model == "game":
                S = bsdf.game_hair(t, wo, wi, p["color"], p["roughness"], p["scatter"], p["backlit"])
            else:
                S = bsdf.lambert_fibre(t, wo, wi, h[s], p["color"])
            out[s] = S * E * shadow[s, None]
        out += AMBIENT * albedo[None, :]
        return out * (0.75 + 0.25 * root)[:, None]
    return fn, albedo


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--groom", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--pixel", type=float, default=0.0005)
    ap.add_argument("--views", default="front,backlit")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    g = np.load(args.groom)

    def build(v):
        return render.strand_fragments(g["points"], g["radii"], g["offsets"], g["root"], v)

    models = ("hit", "game", "chiang")
    shaders = {m: shader(m) for m in models}
    report = {"albedo_white": {m: float(bsdf.furnace(0.41, n=100000, model=m).mean()) for m in models},
              "albedo_char_hair": {m: shaders[m][1].tolist() for m in models},
              "slab_albedo_chiang": bsdf.slab_albedo(bsdf.CHAR_HAIR["color"], 0.41, n=4000, bounces=4000).tolist(),
              "views": {}}
    all_views = views(args.pixel)
    for name in args.views.split(","):
        view, light = all_views[name]
        imgs, stats = [], {}
        for m in models:
            img, cov = render.render(build, view, light, HEAD, shade_fn=shaders[m][0])
            hair = cov > 0.5
            rgb = img[hair].mean(0)
            stats[m] = {"mean_rgb": rgb.tolist(), "luminance": float(rgb @ [0.2126, 0.7152, 0.0722]),
                        "r_over_g": float(rgb[0] / rgb[1]), "g_over_b": float(rgb[1] / rgb[2])}
            imgs.append(render.to_png(img))
            print(f"[bsdf] {name} {m} {stats[m]}", flush=True)
        gap = np.full((imgs[0].shape[0], 6, 3), 255, np.uint8)
        Image.fromarray(np.concatenate([imgs[0], gap, imgs[1], gap, imgs[2]], 1)).save(
            args.out / f"{name}_hit_game_chiang.png")
        report["views"][name] = stats
    (args.out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("albedo_white", "albedo_char_hair", "slab_albedo_chiang")}), flush=True)


if __name__ == "__main__":
    main()
