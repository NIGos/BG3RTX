"""Anomaly detectors on per-effect contribution maps (docs/QA.md has the rules).

Every detector returns {"score", "threshold", "pass", "metrics", "regions"};
a region is {"bbox" (y0, y1, x0, x1) in the buffer's pixels, "area", the
measurements, "qualified"} and the worst ones get crop sheets.
"""
import numpy as np

from . import imgops as im

# Pass/fail thresholds. Areas are per mille (‰) of the buffer's pixels.
T = {
    "dark_removed": 0.60,      # an effect removes more than 60% of the luminance ...
    "dark_sharp": 0.50,        # ... with at least half of the drop inside 3 px of the edge ...
    "dark_unexplained": 0.50,  # ... along an edge the effect-off image does not have
    "dark_fail_area": 0.02,    # fail when qualified regions cover more than this (‰)
    # Shadows may legitimately be sharp (small or distant lights): for them a region
    # also needs a near-black core, at least this fraction left with < 10% of the light
    # (ambient and other lights never vanish completely under a real shadow).
    "dark_black_shadows": 0.25,
    # rt_sun_replace brightens by design (R0): its regions are listed for review.
    "bright_ratio": {"rt_shadows": 1.5, "rt_sun_replace": 1.15, "rt_local_shadows": 1.5, "gi": 2.0,
                     "rt_reflections": 2.0, "all_rt": 2.0},
    "bright_fail_area": 0.05,
    "sun_extra_fail_area": 0.5,   # RT dark, game lit, and the sun term really darkened there
    "sun_missing_warn_area": 2.0, # RT lit, game dark (casters missing from the TLAS): a warning
    "sun_replaced_warn_area": 2.0, # ... and rt_sun_replace wrote it lit: brightened, for review
    "sun_min_core_px": 3,         # erosion radius: ignores thin edges and penumbrae
    "flicker_ratio": 1.5,         # mean temporal std on / off
    "flicker_fail_area": 1.0,     # regions with std_on > 2 std_off + 0.01 (PQ code values)
    "flip_excess": 0.03,         # mean FLIP against the reference, above the on0/on1 noise floor
    "view_changed_frac": 0.05,    # G-buffer albedo differs (> 0.1) on more px than this: another view
    "min_area_frac": 2e-5,       # smallest region considered (of the buffer's pixels)
}


def _min_area(shape):
    return max(24, int(T["min_area_frac"] * shape[0] * shape[1]))


def _permille(px, shape):
    return 1000.0 * px / (shape[0] * shape[1])


def ratio_map(y_on, y_off, blur=1):
    """Luminance ratio on/off after a small box filter (sub-pixel jitter between traces)."""
    a, b = im.box(y_on, blur), im.box(y_off, blur)
    eps = 1e-4 * max(float(np.median(b)), 1e-6)
    return (a + eps) / (b + eps), a, b


SHADOW_EFFECTS = ("rt_shadows", "rt_local_shadows", "all_rt")


def hard_dark(y_on, y_off, sigma, blur=1, effect=""):
    """Connected regions where the effect removes > 60% of the light with sharp,
    unexplained edges: geometry artifacts rather than soft shadows."""
    r, a, b = ratio_map(y_on, y_off, blur)
    floor = 0.05 * float(np.median(b))
    m = (r < 1.0 - T["dark_removed"]) & (b > floor) & ((b - a) > 3.0 * sigma)
    m = im.opening(m, 1)
    lab, n = im.label(m)
    lr = np.log(np.clip(r, 1e-3, 1e3))
    _, gx, gy = im.grad_mag(lr)
    eoff, _, _ = im.grad_mag(np.log(np.maximum(b, floor * 0.1)))
    edge_thr = 0.25   # |d log L| per px: a real edge in the effect-off image
    regs, flagged = [], 0
    for i, area, bb in im.regions(lab, n, _min_area(r.shape)):
        y0, y1, x0, x1 = im.window(bb, r.shape, 14)
        sub = lab[y0:y1, x0:x1] == i
        rr = r[y0:y1, x0:x1]
        in3 = sub & ~im.erode(sub, 3)
        core = im.erode(sub, 3)
        d3 = im.dilate(sub, 3)
        out3 = d3 & ~sub
        far = im.dilate(sub, 12) & ~im.dilate(sub, 6)
        if not out3.any() or not far.any():
            continue
        inside = float(np.median(rr[core])) if core.any() else float(np.median(rr[sub]))
        outside = float(np.median(rr[far]))
        full = outside - inside
        step = float(np.median(rr[out3])) - float(np.median(rr[in3]))
        sharp = float(np.clip(step / full, 0, 1)) if full > 1e-3 else 0.0
        boundary = d3 & ~im.erode(sub, 1) & ~(im.dilate(sub, 3) & ~im.dilate(sub, 1))   # a 2-px band on the edge
        unexpl = float((eoff[y0:y1, x0:x1][boundary] < edge_thr).mean()) if boundary.any() else 0.0
        poly = im.polygonality(gx[y0:y1, x0:x1], gy[y0:y1, x0:x1], boundary)
        removed = float(1.0 - np.median(rr[sub]))
        deep = sub & (rr < 0.1)   # near-black: the effect leaves < 10% of the light
        black = float(deep.sum() / max(1, sub.sum()))
        fy, fx = (np.argwhere(deep).mean(axis=0) if deep.any() else np.argwhere(sub).mean(axis=0))
        q = removed >= T["dark_removed"] and sharp >= T["dark_sharp"] and unexpl >= T["dark_unexplained"]
        if effect in SHADOW_EFFECTS:
            q = q and black >= T["dark_black_shadows"]
        if q:
            flagged += area
        regs.append({"bbox": bb, "area": area, "removed": round(removed, 3), "sharp": round(sharp, 3),
                     "unexplained": round(unexpl, 3), "polygonal": round(poly, 3), "black": round(black, 3),
                     "focus": (int(y0 + fy), int(x0 + fx)), "qualified": q,
                     "rank": area * removed * (0.25 + sharp) * (0.25 + unexpl) * (0.5 + black) * (1.0 if q else 0.1)})
    score = _permille(flagged, r.shape)
    regs.sort(key=lambda g: -g["rank"])
    return {"score": round(score, 4), "threshold": T["dark_fail_area"], "pass": score <= T["dark_fail_area"],
            "unit": "‰ of px", "metrics": {"candidates": len(regs), "qualified": sum(g["qualified"] for g in regs),
                                            "mean_ratio": round(float(np.mean(a) / max(np.mean(b), 1e-9)), 4)},
            "regions": regs, "map": ("ratio", r)}


def brightening(y_on, y_off, sigma, effect, blur=1):
    """Regions the effect brightens beyond its bound (shadows may only darken)."""
    thr = T["bright_ratio"].get(effect, 2.0)
    r, a, b = ratio_map(y_on, y_off, blur)
    floor = 0.02 * float(np.median(b))
    m = (r > thr) & (a > floor) & ((a - b) > 3.0 * sigma)
    m = im.opening(m, 1)
    lab, n = im.label(m)
    regs, flagged = [], 0
    for i, area, bb in im.regions(lab, n, _min_area(r.shape)):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        gain = float(np.median(r[y0:y1, x0:x1][sub]))
        flagged += area
        regs.append({"bbox": bb, "area": area, "gain": round(gain, 3), "qualified": True, "rank": area * (gain - 1)})
    score = _permille(flagged, r.shape)
    regs.sort(key=lambda g: -g["rank"])
    return {"score": round(score, 4), "threshold": T["bright_fail_area"], "pass": score <= T["bright_fail_area"],
            "unit": "‰ of px", "metrics": {"bound": thr, "regions": len(regs),
                                            "px_over_bound_raw": round(_permille(int(((r > thr) & (a > floor)).sum()), r.shape), 3)},
            "regions": regs, "map": ("ratio", r)}


def sun_disagreement(game, rt, sun_ratio, cls=None, applied=None):
    """rt_debug=2 trace: game mask (set 1 b7 before our fold) against the traced
    visibility (1 spp soft sun, box-filtered). rt_backlit=0 in that trace, so traced
    lit is always front-facing. Penumbrae and thin edges are removed by thresholds
    and erosion. `sun_ratio` (compose, on/sun_off) says where the sun term really
    darkened."""
    rt_b = im.box(rt, 2)
    game_b = im.box(game, 1)
    valid = np.ones_like(game, bool)
    if cls is not None:
        valid &= ~np.isin(cls, (1, 3, 4, 5))   # unlit, and transmitting classes (back-lit, untraced here)
    k = T["sun_min_core_px"]
    extra = im.opening(im.erode((rt_b < 0.15) & (game_b > 0.9) & valid, k), 1)
    extra = im.dilate(extra, k)
    missing = im.opening(im.erode((rt_b > 0.85) & (game_b < 0.1) & valid, k), 1)
    missing = im.dilate(missing, k)
    masks = [("extra_casters", extra), ("missing_casters", missing)]
    if applied is not None:
        # rt_sun_replace (blue channel): written lit where the game's mask is dark.
        rep = im.opening(im.erode((im.box(applied, 2) > 0.85) & (game_b < 0.1) & valid, k), 1)
        masks.append(("replaced", im.dilate(rep, k)))
    out = {}
    for name, m in masks:
        lab, n = im.label(m)
        regs, tot, impact_px = [], 0, 0
        for i, area, bb in im.regions(lab, n, _min_area(m.shape)):
            y0, y1, x0, x1 = bb
            sub = lab[y0:y1, x0:x1] == i
            imp = float((sun_ratio[y0:y1, x0:x1][sub] < 0.7).mean()) if sun_ratio is not None else 0.0
            tot += area
            impact_px += int(area * imp)
            regs.append({"bbox": bb, "area": area, "impact": round(imp, 3), "qualified": True,
                         "rank": area * (0.2 + imp)})
        regs.sort(key=lambda g: -g["rank"])
        out[name] = (regs, tot, impact_px)
    ex_regs, ex_tot, ex_imp = out["extra_casters"]
    mi_regs, mi_tot, _ = out["missing_casters"]
    lit_game = (game_b > 0.9) & valid
    s_extra = _permille(ex_imp, game.shape)
    s_missing = _permille(mi_tot, game.shape)
    metrics = {"extra_area": round(_permille(ex_tot, game.shape), 3), "extra_impact_area": round(s_extra, 3),
               "missing_area": round(s_missing, 3),
               "agree_lit": round(float(((rt_b > 0.5) & lit_game).sum() / max(1, lit_game.sum())), 4),
               "game_lit_frac": round(float(lit_game.mean()), 4)}
    res = {
        "extra": {"score": round(s_extra, 4), "threshold": T["sun_extra_fail_area"],
                  "pass": s_extra <= T["sun_extra_fail_area"], "unit": "‰ of px (darkened by the sun term)",
                  "metrics": metrics, "regions": ex_regs},
        # With min(game, traced) a missing caster cannot change the image. With
        # rt_sun_replace only trusted pixels can (the "replaced" entry below); off-screen
        # casters (roofs, cut-away walls) are expected here, so this is a warning, not a gate.
        "missing": {"score": round(s_missing, 4), "threshold": T["sun_missing_warn_area"], "gate": False,
                    "pass": s_missing <= T["sun_missing_warn_area"], "unit": "‰ of px", "metrics": metrics,
                    "regions": mi_regs},
    }
    if "replaced" in out:
        rp_regs, rp_tot, _ = out["replaced"]
        s_rep = _permille(rp_tot, game.shape)
        metrics["replaced_area"] = round(s_rep, 3)
        metrics["replaced_of_missing"] = round(s_rep / max(s_missing, 1e-9), 3) if s_missing > 0 else 0.0
        # Brightened against the game: physically right (over-wide filtering, acne) or a
        # missing caster leaking. For review (the crops), not a gate.
        res["replaced"] = {"score": round(s_rep, 4), "threshold": T["sun_replaced_warn_area"], "gate": False,
                           "pass": s_rep <= T["sun_replaced_warn_area"], "unit": "‰ of px", "metrics": metrics,
                           "regions": rp_regs}
    return res


def temporal(codes_on, codes_off):
    """Per-pixel temporal std of the final frame's PQ code value (perceptually
    uniform), static camera, all-on against all-off (animation is in both)."""
    def std(stack):
        y = np.stack([c[..., 0] * 0.2627 + c[..., 1] * 0.678 + c[..., 2] * 0.0593 for c in stack])
        return y.std(axis=0)
    s_on, s_off = std(codes_on), std(codes_off)
    ratio = float(s_on.mean() / max(s_off.mean(), 1e-6))
    excess = im.box(s_on, 2) > 2.0 * im.box(s_off, 2) + 0.01
    excess = im.opening(excess, 2)
    lab, n = im.label(excess)
    regs, tot = [], 0
    for i, area, bb in im.regions(lab, n, _min_area(excess.shape) * 4):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        tot += area
        so = float(s_on[y0:y1, x0:x1][sub].mean())
        sf = float(s_off[y0:y1, x0:x1][sub].mean())
        regs.append({"bbox": bb, "area": area, "std_on": round(so, 4), "std_off": round(sf, 4), "qualified": True,
                     "rank": area * (so - sf)})
    regs.sort(key=lambda g: -g["rank"])
    area = _permille(tot, excess.shape)
    ok = ratio <= T["flicker_ratio"] and area <= T["flicker_fail_area"]
    return {"score": round(ratio, 3), "threshold": T["flicker_ratio"], "pass": ok, "unit": "std on / std off",
            "metrics": {"std_on": round(float(s_on.mean()), 5), "std_off": round(float(s_off.mean()), 5),
                        "excess_area": round(area, 3), "excess_threshold": T["flicker_fail_area"],
                        "frames": [len(codes_on), len(codes_off)]},
            "regions": regs, "map": ("std", s_on)}
