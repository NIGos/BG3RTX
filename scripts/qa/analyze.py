"""Per-scene analysis: contribution maps, detectors, crops, report; then pruning."""
import html
import json
import shutil
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from . import capture, detectors as det, flipmetric, imgops as im
from .decode import Frame, Trace, lum709, lum2020

CROPS_PER_DETECTOR = 3   # per detector, effect and buffer (2 on the lighting buffer)
EFFECT_LABEL = {"gi": "GI (hybrid RT GI + RT AO fold)", "rt_reflections": "RT reflections (D-RF)",
                "rt_shadows": "RT sun shadows", "rt_sun_replace": "RT sun replacing the game's mask (in-frame vs min)",
                "rt_local_shadows": "RT local-light shadows (D-LS)",
                "all_rt": "all RT effects"}


# ---------------------------------------------------------------- display helpers
def display(lin, exposure):
    x = np.maximum(lin, 0) * exposure
    return np.clip((x / (1 + x)) ** (1 / 2.2), 0, 1)


def heat_ratio(r):
    v = np.clip(np.log2(np.maximum(r, 1e-3)) / 2.0, -1, 1)   # +-2 stops
    out = np.ones(r.shape + (3,), np.float32)
    neg, pos = np.minimum(v, 0), np.maximum(v, 0)
    out[..., 1] += neg + -pos * 0.6
    out[..., 2] += neg
    out[..., 0] -= pos
    out[..., 1] = np.clip(out[..., 1], 0, 1)
    return np.clip(out, 0, 1)


def heat_scalar(s, vmax):
    v = np.clip(s / max(vmax, 1e-9), 0, 1)
    return np.stack([v, v ** 2, 0.2 * (1 - v)], -1)


def to_img(a):
    a = np.asarray(a)
    if a.ndim == 2:
        a = np.repeat(a[..., None], 3, -1)
    return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8))


def crop_window(bb, shape, pad=24, lo=160, hi=400, focus=None):
    y0, y1, x0, x1 = bb
    cy, cx = focus if focus else ((y0 + y1) // 2, (x0 + x1) // 2)
    h = int(np.clip(y1 - y0 + 2 * pad, lo, hi))
    w = int(np.clip(x1 - x0 + 2 * pad, lo, hi))
    ya = int(np.clip(cy - h // 2, 0, max(0, shape[0] - h)))
    xa = int(np.clip(cx - w // 2, 0, max(0, shape[1] - w)))
    return ya, min(shape[0], ya + h), xa, min(shape[1], xa + w)


def sheet(tiles, labels, title, path):
    """Tiles side by side at 1:1 with a caption line."""
    imgs = [to_img(t) for t in tiles]
    w = sum(i.width for i in imgs) + 4 * (len(imgs) - 1)
    h = max(i.height for i in imgs)
    out = Image.new("RGB", (w, h + 32), (20, 20, 20))
    d = ImageDraw.Draw(out)
    d.text((4, 2), title[:200], fill=(255, 255, 255))
    x = 0
    for img, lab in zip(imgs, labels):
        out.paste(img, (x, 32))
        d.text((x + 4, 18), lab, fill=(200, 200, 120))
        x += img.width + 4
    out.save(path, optimize=True)


def outline(img, mask):
    o = img.copy()
    edge = mask & ~im.erode(mask, 1)
    o[edge] = (1.0, 0.0, 1.0)
    return o


# ---------------------------------------------------------------- scene analysis
def analyze_scene(d: Path, pool):
    t0 = time.time()
    meta = json.loads((d / "run.json").read_text())
    raw = d / "raw"
    traces = {name: Trace(raw / sub) for name, sub in meta["traces"].items() if (raw / sub / "dumps.json").exists()}
    fr = {n: Frame(t) for n, t in traces.items()}
    crops = d / "crops"
    if crops.exists():
        shutil.rmtree(crops)
    crops.mkdir()
    res = {"scene": meta["scene"], "save": meta["save"], "run_ok": meta["ok"], "run_s": meta.get("run_s"),
           "exclusive": meta.get("exclusive", True),
           "traces": list(traces), "checks": [], "crops": [], "notes": [],
           # Layer counters that explain RT geometry artifacts (alpha-test fallbacks, dropped draws).
           "log": [l for l in meta.get("log_tail", []) if "[alpha]" in l or "RT capture" in l][-2:]}
    if "on0" not in fr:
        res["notes"].append("no all-on trace: nothing to analyse")
        return res

    # Load every image the detectors need, in parallel (PNG decode releases the GIL).
    def load(n):
        f = fr[n]
        return n, {"final": f.final_linear(), "code": f.final_code(), "compose": f.compose()}
    data = dict(pool.map(load, list(fr)))
    for n, v in data.items():
        v["Yf"] = lum2020(v["final"])
        v["Yc"] = lum709(v["compose"]) if v["compose"] is not None else None

    # ---- same view? The G-buffer albedo does not depend on any lighting setting:
    # a trace whose albedo differs from on0's beyond jitter and foliage motion shows
    # another view (the camera moved), and every comparison with it is void.
    def view_change(n):
        a, b = fr[n].albedo(), fr["on0"].albedo()
        if a is None or b is None or a.shape != b.shape:
            return n, 0.0
        da = np.abs(im.box(a.mean(-1), 2) - im.box(b.mean(-1), 2))
        return n, float((da > 0.1).mean())
    view = dict(pool.map(view_change, [n for n in fr if n != "on0"]))
    moved = sorted(n for n, v in view.items() if v > det.T["view_changed_frac"])
    # The sun debug traces are evaluated each on its own (in-frame), so another view than
    # on0's does not void them. (Their crops show on0's lighting only when the view agrees.)
    self_contained = ("sun_dbg", "sun_check", "sun_trust")
    moved_self = [n for n in moved if n in self_contained]
    moved = [n for n in moved if n not in self_contained]
    res["view_change"] = {n: round(v, 4) for n, v in view.items()}
    if moved:
        res["notes"].append(f"camera moved: {', '.join(moved)} show another view than on0 (albedo differs on "
                            f"{', '.join(f'{100 * view[n]:.0f}%' for n in moved)} of px); their comparisons are void")
        res["checks"].append({"detector": "same_view", "effect": "all", "buffer": "G-buffer albedo", "on": "on0",
                              "off": ",".join(moved), "score": round(max(view[n] for n in moved), 4),
                              "threshold": det.T["view_changed_frac"], "pass": False, "unit": "fraction of px changed",
                              "metrics": res["view_change"]})
        for n in moved:
            data.pop(n)
    on_set = [n for n in data if n.startswith("on")]
    off_set = [n for n in data if n.startswith("off")]

    def noise(names, key, blur):
        ys = [im.box(data[n][key], blur) for n in names if data[n][key] is not None]
        if len(ys) < 2:
            return None
        return np.stack(ys).std(axis=0)

    def sigma_of(key, blur):
        # Temporal std of the static view (RR noise, wind in foliage, idle animation),
        # the larger of the all-on and all-off sets, spread over 2 px so a moving edge
        # covers both of its positions; x sqrt(2) for the difference of two frames.
        s = [x for x in (noise(on_set, key, blur), noise(off_set, key, blur)) if x is not None]
        if not s:
            return 0.0
        return im.maxf(np.maximum.reduce(s), 2) * np.sqrt(2.0)
    sig = {k: sigma_of(k, b) for k, b in (("Yf", 1), ("Yc", 2))}

    def sigma(key):
        return sig[key]

    expo_f = 0.18 / max(float(np.median(data["on0"]["Yf"])), 1e-6)
    expo_c = 0.18 / max(float(np.median(data["on0"]["Yc"])), 1e-6) if data["on0"]["Yc"] is not None else 1.0
    disp = {}

    def dsp(n, key):
        if (n, key) not in disp:
            src, ex = (data[n]["final"], expo_f) if key == "final" else (data[n]["compose"], expo_c)
            disp[(n, key)] = display(src, ex)
        return disp[(n, key)]

    # ---- per-effect contribution detectors (final frame and lighting buffer)
    jobs = []
    for eff, (a, b) in capture.EFFECTS.items():
        if a not in data or b not in data:
            res["notes"].append(f"{eff}: traces {a}/{b} missing")
            continue
        for buf, key, blur in (("final", "Yf", 1), ("lighting", "Yc", 2)):
            if data[a][key] is None or data[b][key] is None:
                continue
            s = sigma(key)
            jobs.append(("hard_dark", eff, buf, a, b, pool.submit(det.hard_dark, data[a][key], data[b][key], s, blur, eff)))
            jobs.append(("brightening", eff, buf, a, b,
                         pool.submit(det.brightening, data[a][key], data[b][key], s, eff, blur)))
    contrib = {}
    for kind, eff, buf, a, b, fut in jobs:
        r = fut.result()
        ratio = r.pop("map")[1]
        if buf == "final" and kind == "hard_dark":
            Ya, Yb = data[a]["Yf"], data[b]["Yf"]
            contrib[eff] = {"mean_change_pct": round(100 * (float(Ya.mean()) / max(float(Yb.mean()), 1e-9) - 1), 2),
                            "px_darker_50pct": round(100 * float((ratio < 0.5).mean()), 3),
                            "px_brighter_2x": round(100 * float((ratio > 2).mean()), 3)}
        chk = {"detector": kind, "effect": eff, "buffer": buf, "on": a, "off": b, **{k: v for k, v in r.items() if k != "regions"},
               "top": r["regions"][:6]}
        res["checks"].append(chk)
        if not r["pass"]:
            key = "final" if buf == "final" else "compose"
            for j, g in enumerate([g for g in r["regions"] if g["qualified"]][:CROPS_PER_DETECTOR - (buf != "final")]):
                y0, y1, x0, x1 = crop_window(g["bbox"], ratio.shape, focus=g.get("focus"))
                m = np.zeros(ratio.shape, bool)
                gy0, gy1, gx0, gx1 = g["bbox"]
                m[gy0:gy1, gx0:gx1] = True
                reg = (ratio < 1.0 - det.T["dark_removed"]) if kind == "hard_dark" else (ratio > det.T["bright_ratio"].get(eff, 2))
                reg &= m
                name = f"{kind}_{eff}_{buf}_{j}.png"
                info = {k: v for k, v in g.items() if k not in ("bbox", "rank", "qualified", "focus")}
                sheet([dsp(b, key)[y0:y1, x0:x1], dsp(a, key)[y0:y1, x0:x1], heat_ratio(ratio[y0:y1, x0:x1]),
                       outline(dsp(a, key)[y0:y1, x0:x1], reg[y0:y1, x0:x1])],
                      [f"{EFFECT_LABEL[eff]} off ({b})", f"on ({a})", "on/off (red darker, blue brighter)", "flagged"],
                      f"{meta['scene']} {kind} {eff} [{buf}] at x{x0}-{x1} y{y0}-{y1}: {info}", crops / name)
                res["crops"].append({"file": f"crops/{name}", "detector": kind, "effect": eff, "buffer": buf,
                                     "bbox": g["bbox"], "shape": list(ratio.shape), **info})
    res["contribution"] = contrib

    # ---- sun-shadow disagreement (rt_debug=2 trace)
    if "sun_dbg" in data:
        dif, src = fr["sun_dbg"].diffuse()
        if dif is not None:
            game, rt = np.clip(2 * dif[..., 0], 0, 1), np.clip(2 * dif[..., 1], 0, 1)
            # Blue: the visibility written into the mask (rt_sun_replace; older builds write 0).
            applied = np.clip(2 * dif[..., 2], 0, 1)
            if float(applied.max()) <= 0.0:
                applied = None
            sun_ratio = None
            if data["on0"]["Yc"] is not None and "sun_off" in data:
                sun_ratio = det.ratio_map(data["on0"]["Yc"], data["sun_off"]["Yc"], 2)[0]
            cls = fr["sun_dbg"].material_class()
            if cls is not None and cls.shape != game.shape:
                cls = None
            sd = det.sun_disagreement(game, rt, sun_ratio, cls, applied)
            gm = fr["off0"].sun_mask() if "off0" in data else None
            if gm is not None and gm.shape == game.shape:
                sd["extra"]["metrics"]["game_mask_vs_alloff_mad"] = round(float(np.abs(gm - game).mean()), 4)
            same = "sun_dbg" not in moved_self   # on0's lighting and ratios match this view
            if not same:
                sun_ratio = None
            for sub, r in sd.items():
                dname = "sun_replaced" if sub == "replaced" else f"sun_{sub}_casters"
                res["checks"].append({"detector": dname, "effect": "rt_sun_replace" if sub == "replaced" else "rt_shadows",
                                      "buffer": f"sun mask ({src})",
                                      "on": "sun_dbg", "off": "sun_dbg", **{k: v for k, v in r.items() if k != "regions"},
                                      "top": r["regions"][:6]})
                regs = r["regions"] if not r["pass"] else r["regions"][:1]
                for j, g in enumerate(regs[:CROPS_PER_DETECTOR]):
                    y0, y1, x0, x1 = crop_window(g["bbox"], game.shape)
                    blue = applied if applied is not None else np.zeros_like(game)
                    ov = np.stack([game, im.box(rt, 2), blue], -1)
                    name = f"sun_{sub}_{j}.png"
                    tiles = [ov[y0:y1, x0:x1]]
                    labs = ["red game mask, green RT, blue written (white all lit)"]
                    if same:
                        tiles.insert(0, dsp("on0", "compose")[y0:y1, x0:x1])
                        labs.insert(0, "all on (lighting)")
                        if sub != "replaced":
                            if sun_ratio is not None:
                                tiles.append(heat_ratio(sun_ratio[y0:y1, x0:x1]))
                                labs.append("sun term on/off")
                            if "sun_off" in data:
                                tiles.insert(0, dsp("sun_off", "compose")[y0:y1, x0:x1])
                                labs.insert(0, "RT sun off (lighting)")
                    info = {k: v for k, v in g.items() if k not in ("bbox", "rank", "qualified", "focus")}
                    sheet(tiles, labs, f"{meta['scene']} {dname} at x{x0}-{x1} y{y0}-{y1}: {info}"
                          + ("" if not r["pass"] else " (largest, below threshold)"), crops / name)
                    res["crops"].append({"file": f"crops/{name}", "detector": dname, "effect": "rt_shadows",
                                         "buffer": "sun mask", "bbox": g["bbox"], "shape": list(game.shape), **info})

    # ---- rt_sun_replace in-frame A/B (rt_debug=7 trace): the written mask against
    # min(game, traced) of the same pixel and frame; animation cannot fake it.
    if "sun_check" in data:
        dif, src = fr["sun_check"].diffuse()
        if dif is not None:
            view = np.clip(2 * dif, 0, 1)
            sc = det.sun_check(view)
            over = None
            if "sun_dbg" in data:
                dd, _ = fr["sun_dbg"].diffuse()
                if dd is not None and dd.shape == dif.shape:
                    over = np.clip(2 * dd, 0, 1)
            for sub, r in sc.items():
                res["checks"].append({"detector": f"sun_check_{sub}", "effect": "rt_sun_replace",
                                      "buffer": f"debug diffuse ({src})", "on": "sun_check", "off": "sun_check",
                                      **{k: v for k, v in r.items() if k != "regions"}, "top": r["regions"][:6]})
                # Every far region is for review; the edge ones are expected (largest two).
                regs = r["regions"][:CROPS_PER_DETECTOR] if sub != "edge" else r["regions"][:2]
                for j, g in enumerate(regs):
                    y0, y1, x0, x1 = crop_window(g["bbox"], view.shape[:2])
                    tiles = [view[y0:y1, x0:x1]]
                    labs = ["changed by the replace (yellow edge, red far, blue darker)"]
                    if over is not None:
                        tiles.append(over[y0:y1, x0:x1])
                        labs.append("next trace: red game, green RT, blue written")
                    if "sun_check" not in moved_self:
                        tiles.insert(0, dsp("on0", "compose")[y0:y1, x0:x1])
                        labs.insert(0, "all on (lighting)")
                    name = f"sun_check_{sub}_{j}.png"
                    info = {k: v for k, v in g.items() if k not in ("bbox", "rank", "qualified", "focus")}
                    sheet(tiles, labs, f"{meta['scene']} sun_check {sub} at x{x0}-{x1} y{y0}-{y1}: {info}", crops / name)
                    res["crops"].append({"file": f"crops/{name}", "detector": f"sun_check_{sub}",
                                         "effect": "rt_sun_replace", "buffer": "debug diffuse", "bbox": g["bbox"],
                                         "shape": list(view.shape[:2]), **info})

    # ---- non-finite values
    nf = {n: t.nonfinite() for n, t in traces.items()}
    worst = max(nf.values()) if nf else 0
    res["checks"].append({"detector": "nonfinite", "effect": "all", "buffer": "every dump", "on": "*", "off": "*",
                          "score": worst, "threshold": 0, "pass": worst == 0, "unit": "values (max over traces)",
                          "metrics": {"per_trace": nf, "dumps": {n: t.nonfinite_by_dump() for n, t in traces.items() if nf[n]}}})

    # ---- temporal stability
    # Sets: on* (all on), off* (all RT off), nogi* (all on but the GI neutral: --temporal-gi).
    nogi_set = [n for n in data if n.startswith("nogi")]
    if len(on_set) >= 2 and len(off_set) >= 2:
        # Scene motion in any set (geometry or the game's own shadows moved) is excluded.
        shape = data["on0"]["code"].shape[:2]
        sets = [s for s in (on_set, off_set, nogi_set) if len(s) >= 2]
        moving = np.zeros(shape, bool)
        for s in sets:
            moving |= det.animated([fr[n].albedo() for n in s], [fr[n].sun_mask() for n in s], shape,
                                   [fr[n].normal() for n in s])
        pairs = [("all_rt", off_set, "all RT off")]
        if len(nogi_set) >= 2:
            pairs.append(("gi", nogi_set, "GI neutral (gi_strength=0 gi_ao=0)"))
        for eff, base, base_label in pairs:
            r = det.temporal([data[n]["code"] for n in on_set], [data[n]["code"] for n in base], moving)
            s_on = r.pop("map")[1]
            res["checks"].append({"detector": "flicker", "effect": eff, "buffer": "final (PQ code), static px",
                                  "on": "+".join(on_set), "off": "+".join(base),
                                  **{k: v for k, v in r.items() if k != "regions"}, "top": r["regions"][:6]})
            if r["pass"]:
                continue
            vmax = float(np.percentile(s_on, 99.5)) + 1e-6
            s_base = np.stack([lum2020(data[n]["code"]) for n in base]).std(axis=0)
            for j, g in enumerate(r["regions"][:CROPS_PER_DETECTOR]):
                y0, y1, x0, x1 = crop_window(g["bbox"], s_on.shape)
                name = f"flicker_{j}.png" if eff == "all_rt" else f"flicker_{eff}_{j}.png"
                info = {k: v for k, v in g.items() if k not in ("bbox", "rank", "qualified", "focus")}
                sheet([dsp("on0", "final")[y0:y1, x0:x1], heat_scalar(s_on[y0:y1, x0:x1], vmax),
                       heat_scalar(s_base[y0:y1, x0:x1], vmax), moving[y0:y1, x0:x1].astype(np.float32) * 0.5],
                      ["all on", f"temporal std, all on ({len(on_set)} frames)", f"temporal std, {base_label}",
                       "scene motion (excluded)"],
                      f"{meta['scene']} flicker ({eff}) at x{x0}-{x1} y{y0}-{y1}: {info}", crops / name)
                res["crops"].append({"file": f"crops/{name}", "detector": "flicker", "effect": eff, "buffer": "final",
                                     "bbox": g["bbox"], "shape": list(s_on.shape), **info})

    # ---- reference (optional)
    if "ref" in data:
        a, ref = dsp("on0", "final"), dsp("ref", "final")
        fmap = flipmetric.flip(ref, a)
        floor = flipmetric.flip(dsp("on0", "final"), dsp("on1", "final")) if "on1" in data else None
        mean = float(fmap.mean())
        # Scored above the noise floor (two all-on frames of the same view differ by RR
        # noise and animation as well).
        excess = mean - (float(floor.mean()) if floor is not None else 0.0)
        r = {"detector": "reference", "effect": "all_rt", "buffer": "final", "on": "on0", "off": "ref",
             "score": round(excess, 4), "threshold": det.T["flip_excess"], "pass": excess <= det.T["flip_excess"],
             "unit": flipmetric.NAME + ", mean above the on0/on1 floor",
             "metrics": {"mean": round(mean, 4), "p99": round(float(np.percentile(fmap, 99)), 4),
                         "noise_floor_on0_vs_on1": round(float(floor.mean()), 4) if floor is not None else None,
                         "psnr_db": round(float(-10 * np.log10(max(np.mean((a - ref) ** 2), 1e-12))), 2),
                         "reference": "gi_rr_rays=8, ls_samples=2, RR on, same view"}}
        m = im.opening(im.box(fmap, 2) > 0.35, 2)
        lab, n = im.label(m)
        regs = sorted(im.regions(lab, n, 400), key=lambda t: -t[1])[:2]
        for j, (_, area, bb) in enumerate(regs):
            y0, y1, x0, x1 = crop_window(bb, fmap.shape)
            name = f"reference_{j}.png"
            sheet([ref[y0:y1, x0:x1], a[y0:y1, x0:x1], heat_scalar(fmap[y0:y1, x0:x1], 1.0)],
                  ["reference (8 GI rays)", "all on (1 ray)", "FLIP error"],
                  f"{meta['scene']} reference difference at x{x0}-{x1} y{y0}-{y1}, {area} px", crops / name)
            res["crops"].append({"file": f"crops/{name}", "detector": "reference", "effect": "all_rt", "buffer": "final",
                                 "bbox": bb, "shape": list(fmap.shape), "area": area})
        res["checks"].append(r)

    # ---- overview: the all-on frame with every cropped region boxed
    ov = to_img(dsp("on0", "final"))
    W = 1280
    s = W / ov.width
    ov = ov.resize((W, int(ov.height * s)))
    dr = ImageDraw.Draw(ov)
    for i, c in enumerate(res["crops"]):
        y0, y1, x0, x1 = c["bbox"]
        k = W / c["shape"][1]
        col = (255, 0, 255) if c["detector"] == "hard_dark" else (255, 200, 0) if c["detector"].startswith("sun") else (0, 200, 255)
        dr.rectangle([x0 * k - 2, y0 * k - 2, x1 * k + 2, y1 * k + 2], outline=col, width=2)
        dr.text((x0 * k, max(0, y0 * k - 12)), str(i + 1), fill=col)
    ov.save(d / "overview.jpg", quality=85)
    res["analysis_s"] = round(time.time() - t0, 1)
    res["summary"] = summarize(res)
    (d / "result.json").write_text(json.dumps(res, indent=1, default=_json))
    return res


def _json(o):
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    return str(o)


def summarize(res):
    fails = [c for c in res["checks"] if not c["pass"] and c.get("gate", True)]
    warns = [c for c in res["checks"] if not c["pass"] and not c.get("gate", True)]
    if not res["run_ok"]:
        head = "RUN INCOMPLETE"
    else:
        head = "FAIL" if fails else "PASS"
    parts = []
    by = {}
    for c in fails:
        by.setdefault(c["detector"], []).append(c)
    for kind, cs in by.items():
        if kind in ("hard_dark", "brightening"):
            effs = {}
            for c in cs:
                effs.setdefault(c["effect"], []).append(f"{c['buffer']} {c['score']}‰")
            parts.append(f"{kind.replace('_', '-')}: " + "; ".join(f"{e} ({', '.join(v)})" for e, v in effs.items()))
        elif kind == "flicker":
            for c in cs:
                m = c["metrics"]
                parts.append(f"flicker {c['effect']}: std on/off {c['score']} (limit {c['threshold']}), excess area "
                             f"{m['excess_area']}‰ (limit {m['excess_threshold']}; scene motion "
                             f"{100 * m.get('animated_frac', 0):.1f}% excluded, unmasked "
                             f"{m.get('unmasked', {}).get('excess_area', '?')}‰)")
        else:
            c = cs[0]
            parts.append(f"{kind} {c['score']} (> {c['threshold']})")
    for c in warns:
        eff = f" {c['effect']}" if c.get("effect") else ""
        parts.append(f"review {c['detector']}{eff} {c['score']} (> {c['threshold']})")
    passes = [c["detector"] for c in res["checks"] if c["pass"]]
    tail = f" | ok: {', '.join(sorted(set(passes) - set(by)))}" if passes else ""
    return f"{res['scene']} {res['save']}: {head}" + (" - " + " | ".join(parts) if parts else "") + tail


# ---------------------------------------------------------------- report
CSS = """body{font:14px system-ui,sans-serif;margin:16px;background:#111;color:#ddd}
table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid #444;padding:3px 6px;font-size:12px;vertical-align:top}
.p{color:#6c6}.w{color:#fc4}.f{color:#f55;font-weight:bold}img{max-width:100%;display:block;margin:6px 0}
code{color:#fc8}h2{border-top:1px solid #555;padding-top:12px}.sum{font-family:monospace;white-space:pre-wrap}"""


def write_report(results, out: Path, wall_s):
    rows = []
    for r in results:
        sc = r["scene"]
        rows.append(f"<h2 id='{sc}'>{sc} {html.escape(r['save'])}</h2><p class='sum'>{html.escape(r.get('summary', 'RUN INCOMPLETE'))}</p>")
        rows.append(f"<p>Traces: {', '.join(r['traces'])}. Game run {r.get('run_s')} s{'' if r.get('exclusive', True) else ' (shared run: not a timing)'}, analysis {r.get('analysis_s')} s.</p>")
        if r.get("notes"):
            rows.append("<p>Notes: " + html.escape("; ".join(r["notes"])) + "</p>")
        for l in r.get("log", []):
            rows.append(f"<p><code>{html.escape(l)}</code></p>")
        rows.append(f"<img src='{sc}/overview.jpg' alt='overview'>")
        if r.get("contribution"):
            rows.append("<table><tr><th>effect</th><th>mean luminance change</th><th>px darker &gt;50%</th><th>px brighter &gt;2x</th></tr>")
            for e, c in r["contribution"].items():
                rows.append(f"<tr><td>{e}</td><td>{c['mean_change_pct']:+.2f}%</td><td>{c['px_darker_50pct']}%</td><td>{c['px_brighter_2x']}%</td></tr>")
            rows.append("</table>")
        rows.append("<table><tr><th>detector</th><th>effect</th><th>buffer</th><th>score</th><th>threshold</th><th>result</th><th>metrics</th></tr>")
        for c in r["checks"]:
            ok = ("<span class='p'>pass</span>" if c["pass"] else "<span class='f'>FAIL</span>" if c.get("gate", True)
                  else "<span class='w'>warn</span>")
            met = html.escape(json.dumps(c.get("metrics", {}), default=_json))[:400]
            rows.append(f"<tr><td>{c['detector']}</td><td>{c['effect']}</td><td>{html.escape(c['buffer'])}</td><td>{c['score']} "
                        f"{html.escape(c.get('unit', ''))}</td><td>{c['threshold']}</td><td>{ok}</td><td>{met}</td></tr>")
        rows.append("</table>")
        for i, c in enumerate(r["crops"]):
            info = {k: v for k, v in c.items() if k not in ("file", "shape")}
            rows.append(f"<p>{i + 1}. <code>{html.escape(json.dumps(info, default=_json))}</code></p>"
                        f"<img src='{sc}/{c['file']}' alt='crop'>")
    sums = "\n".join(html.escape(r.get("summary", "RUN INCOMPLETE")) for r in results)
    doc = (f"<!doctype html><html><head><meta charset='utf-8'><title>BG3RTX QA</title><style>{CSS}</style></head><body>"
           f"<h1>BG3RTX image-quality QA</h1><p>{time.strftime('%Y-%m-%d %H:%M')}, {len(results)} scene(s), "
           f"{wall_s:.0f} s analysis. Thresholds: <code>{html.escape(json.dumps(det.T))}</code>. "
           f"Perceptual metric: {flipmetric.NAME}. Rules: docs/QA.md.</p><p class='sum'>{sums}</p>"
           + "".join(rows) + "</body></html>")
    (out / "report.html").write_text(doc, encoding="utf-8")


def prune(d: Path):
    """Disk hygiene: keep crops, overview, result.json, run metadata; drop raw dumps."""
    shutil.rmtree(d / "raw", ignore_errors=True)
    ar = d / "auto-run.out"
    if ar.exists():
        lines = ar.read_text(errors="replace").splitlines()
        ar.write_text("\n".join(lines[-150:]))


def analyze_all(dirs, out: Path, keep_raw=False):
    t0 = time.time()
    results = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        for d in dirs:
            if not (d / "raw").exists():
                if (d / "result.json").exists():
                    results.append(json.loads((d / "result.json").read_text()))
                    continue
                print(f"{d}: no raw captures")
                continue
            r = analyze_scene(d, pool)
            results.append(r)
            # A scene whose run stalled or moved has no summary, only an error.
            print(r.get("summary") or f"{d.name}: {r.get('error', r)}", flush=True)
            if not keep_raw:
                prune(d)
    wall = time.time() - t0
    write_report(results, out, wall)
    (out / "summary.json").write_text(json.dumps([{"scene": r["scene"], "summary": r.get("summary", "RUN INCOMPLETE")} for r in results], indent=1))
    print(f"report: {out / 'report.html'} ({wall:.0f} s analysis)")
