"""Anomaly detectors on per-effect contribution maps (docs/QA.md has the rules).

Every detector returns {"score", "threshold", "pass", "metrics", "regions"};
a region is {"bbox" (y0, y1, x0, x1) in the buffer's pixels, "area", the
measurements, "qualified"} and the worst ones get crop sheets.
"""
import numpy as np

from . import imgops as im
from . import thresholds as th

# Thresholds. Every gate value is derived (scripts/qa/thresholds.py, docs/QA.md "Thresholds and
# their derivation"): physics, the measured noise of the run's own repeated traces, the display,
# or a cited standard. The per-run ones (k sigma, minimum region, edge strength, flicker dof) are
# computed by th from the traces. Values that are not derived only select crops for review
# ("gate": False), or are marked "signature" in th.REGISTRY. Areas are per mille (‰) of px.
T = {
    # hard_dark artifact definition ("signature": not yet derived; QA.md has the proposals).
    "dark_removed": 0.60,      # an effect removes more than 60% of the luminance ...
    "dark_sharp": 0.50,        # ... with at least half of the drop inside 3 px of the edge ...
    "dark_unexplained": 0.50,  # ... along an edge the effect-off image does not have
    # Shadows may legitimately be sharp (small or distant lights): for them a region
    # also needs a near-black core, at least this fraction left with < 10% of the light
    # (ambient and other lights never vanish completely under a real shadow).
    "dark_black_shadows": 0.25,
    "dark_black_level": 0.10,
    # Physics: a qualified region is an artifact by definition, so none is tolerated. False
    # alarms are bounded by the k-sigma test (th.k_sigma) and the filters' support (th.min_area).
    "dark_fail_area": 0.0,
    # Review only (R0: the game's image is not a reference for effect outputs): these bounds
    # select the brightening regions whose crops are listed.
    "bright_ratio": {"rt_shadows": 1.5, "rt_local_shadows": 1.5, "gi": 2.0, "rt_reflections": 2.0, "all_rt": 2.0,
                     "sharc": 2.0, "gi_sharc": 2.0},
    # SHaRC leak gate (roofed_brightening): GI brightening on/gi_off above this ratio inside an
    # asset-DB roofed room while the sun is up is light through geometry (DESIGN_SOTA_NEXT 2.8) ...
    "roof_ratio": 1.3,
    "roof_fail_area": 0.5,       # ... a fail when the cache adds more than this (‰ of the frame) to the
                                 # roofed area GI without the cache already brightens that much
    "roof_min_px_frac": 0.01,    # roofed px needed for the gate to apply ...
    "roof_sun_lit_frac": 0.01,   # ... and px the game's sun mask lights (the exterior is sunlit)
    # Sun mask against the traced visibility (review only, R0): classifier levels that select
    # the crops, no area threshold.
    "sun_rt_dark": 0.15, "sun_rt_lit": 0.85, "sun_game_lit": 0.9, "sun_game_dark": 0.1,
    "sun_impact_ratio": 0.7,      # the sun term really darkened there (lighting on/sun_off)
    "sun_min_core_px": 3,         # erosion radius: ignores thin edges and penumbrae
    # rt_sun_replace in-frame A/B (rt_debug=7). Darker than min(game, traced) is impossible with
    # rt_sun_roof_mode=0 (physics): any such pixel fails. The far class is for review.
    "sun_check_darker_fail": 0.0,
    # The flicker statistics compare stds on top of the previews' 8-bit PQ quantisation (half a
    # code step): below it a std is rounding (S14 gi: 0.00164 against 0.00098, both under one
    # code step of 0.0039).
    "flicker_quant": 0.5 * th.PREVIEW_STEP,
    # Visible flicker: a temporal std of one 10-bit PQ code step (ST 2084: about the Barten
    # threshold). The mean excess over the static frame must stay below it.
    "flicker_jnd": th.PQ10_STEP,
    "flicker_box": 2,             # std pooled over a 5x5 box; its dof follow from the measured n_eff
    # Excess-flicker regions are significant at ALPHA_FW (measured n_eff) and visible
    # (> flicker_jnd): none is tolerated.
    "flicker_fail_area": 0.0,
}


def _min_area(shape, blur=1, r_open=1):
    """Smallest region: the filters' own support (th.min_area), independent of the size."""
    return th.min_area(blur, r_open)


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
    unexplained edges: geometry artifacts rather than soft shadows.

    Derived parts (docs/QA.md "Thresholds and their derivation"): the drop must exceed
    k sigma of the measured per-pixel noise (k: Bonferroni over the buffer's independent
    samples), regions are at least the filters' support, an off-image edge "explains" the
    boundary when it is as steep as the drop itself after the same box, and no qualified
    region is tolerated."""
    r, a, b = ratio_map(y_on, y_off, blur)
    k = th.k_sigma(r.shape, blur)
    sig = np.asarray(sigma, np.float32)
    # (b - a) > k sigma also makes the denominator significant (b > k sigma): no luminance floor.
    m = (r < 1.0 - T["dark_removed"]) & ((b - a) > k * sig)
    m = im.opening(m, 1)
    lab, n = im.label(m)
    lr = np.log(np.clip(r, 1e-3, 1e3))
    _, gx, gy = im.grad_mag(lr)
    # log of the effect-off image, clamped at its own noise (below it the log is noise).
    eoff, _, _ = im.grad_mag(np.log(np.maximum(b, np.maximum(sig, 1e-9))))
    edge_thr = th.edge_threshold(T["dark_removed"], blur)
    regs, flagged = [], 0
    for i, area, bb in im.regions(lab, n, _min_area(r.shape, blur)):
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
        deep = sub & (rr < T["dark_black_level"])   # near-black: the effect leaves < 10% of the light
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
                                            "mean_ratio": round(float(np.mean(a) / max(np.mean(b), 1e-9)), 4),
                                            "k_sigma": round(k, 3), "edge_thr": round(edge_thr, 4),
                                            "min_area_px": _min_area(r.shape, blur)},
            "regions": regs, "map": ("ratio", r)}


def brightening(y_on, y_off, sigma, effect, blur=1):
    """Regions the effect brightens beyond a review bound, significant at k sigma of the
    measured noise. Review only: the off image is the game's (R0), so there is no threshold;
    every region is listed for its crops."""
    thr = T["bright_ratio"].get(effect, 2.0)
    r, a, b = ratio_map(y_on, y_off, blur)
    k = th.k_sigma(r.shape, blur)
    sig = np.asarray(sigma, np.float32)
    m = (r > thr) & ((a - b) > k * sig) & (b > k * sig)
    m = im.opening(m, 1)
    lab, n = im.label(m)
    regs, flagged = [], 0
    for i, area, bb in im.regions(lab, n, _min_area(r.shape, blur)):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        gain = float(np.median(r[y0:y1, x0:x1][sub]))
        flagged += area
        regs.append({"bbox": bb, "area": area, "gain": round(gain, 3), "qualified": True, "rank": area * (gain - 1)})
    score = _permille(flagged, r.shape)
    regs.sort(key=lambda g: -g["rank"])
    # QUALITY.md R0: the game's image is not a reference for effect outputs, so brightening
    # against it is for review (the crops), not a gate, and has no threshold.
    return {"score": round(score, 4), "threshold": None, "gate": False,
            "pass": not regs, "unit": "‰ of px", "metrics": {"bound": thr, "regions": len(regs), "k_sigma": round(k, 3),
                                            "px_over_bound_raw": round(_permille(int(((r > thr) & (b > k * sig)).sum()), r.shape), 3)},
            "regions": regs, "map": ("ratio", r)}


def roof_mask(view):
    """sharc_debug=5 trace (debug diffuse, gi/sharc_roof.hlsl): green = inside an asset-DB roof
    volume, red = outside every volume, blue = not known (no volumes for the level), black =
    sky. Returns (roofed, outside, unknown) masks."""
    r, g, b = view[..., 0], view[..., 1], view[..., 2]
    lit = (r + g + b) > 0.05
    roofed = lit & (g > 2 * r) & (g > 2 * b)
    outside = lit & (r > 2 * g) & (r > 2 * b)
    unknown = lit & (b > 2 * r) & (b > 2 * g)
    return roofed, outside, unknown


def roofed_brightening(y_with, y_base, y_off, roofed, sigma, blur=2, sun_lit_frac=None):
    """SHaRC leak gate (docs/QA.md): GI brightening y/y_off > roof_ratio on roofed pixels
    (a room whose exterior is sunlit). Light through geometry that exists is physically
    implausible (QUALITY R0); the bounce of lit surfaces inside the room is not. GI without
    the cache (y_base, on-screen bounce only, cannot pass through walls) measures the second,
    so the score is the roofed area the cache adds on top of it."""
    def bright(y_on):
        r, a, b = ratio_map(y_on, y_off, blur)
        floor = 0.02 * float(np.median(b))
        m = roofed & (r > T["roof_ratio"]) & (a > floor) & ((a - b) > 3.0 * sigma)
        return im.opening(m, 1), r
    m_with, r_with = bright(y_with)
    m_base, _ = bright(y_base)
    added = m_with & ~im.dilate(m_base, 2)
    lab, n = im.label(added)
    regs = []
    for i, area, bb in im.regions(lab, n, _min_area(added.shape)):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        gain = float(np.median(r_with[y0:y1, x0:x1][sub]))
        regs.append({"bbox": bb, "area": area, "gain": round(gain, 3), "qualified": True, "rank": area * (gain - 1)})
    regs.sort(key=lambda g: -g["rank"])
    score = _permille(sum(g["area"] for g in regs), added.shape)
    roof_px = int(roofed.sum())
    # The exterior is sunlit: the game's own sun mask is lit somewhere in the view (sun_dbg).
    sunlit = sun_lit_frac is None or sun_lit_frac >= T["roof_sun_lit_frac"]
    applies = roof_px >= T["roof_min_px_frac"] * roofed.size and sunlit
    metrics = {"roofed_px_permille": round(_permille(roof_px, roofed.shape), 2),
               "sun_lit_frac": None if sun_lit_frac is None else round(float(sun_lit_frac), 4),
               "with_cache_permille": round(_permille(int(m_with.sum()), roofed.shape), 4),
               "without_cache_permille": round(_permille(int(m_base.sum()), roofed.shape), 4),
               "bound": T["roof_ratio"], "applies": applies}
    # Review only (user decision 2026-10-27): the 1.3 ratio and the 0.5 per mille area have no
    # physical basis. The physical checks are sharc_reference and sharc_leak_cells below.
    return {"score": round(score, 4), "threshold": T["roof_fail_area"], "gate": False,
            "pass": (not applies) or score <= T["roof_fail_area"], "unit": "‰ of px added by the cache (review map)",
            "metrics": metrics, "regions": regs if applies else [], "map": ("ratio", r_with)}


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
    extra = im.opening(im.erode((rt_b < T["sun_rt_dark"]) & (game_b > T["sun_game_lit"]) & valid, k), 1)
    extra = im.dilate(extra, k)
    missing = im.opening(im.erode((rt_b > T["sun_rt_lit"]) & (game_b < T["sun_game_dark"]) & valid, k), 1)
    missing = im.dilate(missing, k)
    masks = [("extra_casters", extra), ("missing_casters", missing)]
    if applied is not None:
        # rt_sun_replace (blue channel): written lit where the game's mask is dark.
        rep = im.opening(im.erode((im.box(applied, 2) > T["sun_rt_lit"]) & (game_b < T["sun_game_dark"]) & valid, k), 1)
        masks.append(("replaced", im.dilate(rep, k)))
    out = {}
    for name, m in masks:
        lab, n = im.label(m)
        regs, tot, impact_px = [], 0, 0
        for i, area, bb in im.regions(lab, n, _min_area(m.shape)):
            y0, y1, x0, x1 = bb
            sub = lab[y0:y1, x0:x1] == i
            imp = float((sun_ratio[y0:y1, x0:x1][sub] < T["sun_impact_ratio"]).mean()) if sun_ratio is not None else 0.0
            tot += area
            impact_px += int(area * imp)
            regs.append({"bbox": bb, "area": area, "impact": round(imp, 3), "qualified": True,
                         "rank": area * (0.2 + imp)})
        regs.sort(key=lambda g: -g["rank"])
        out[name] = (regs, tot, impact_px)
    ex_regs, ex_tot, ex_imp = out["extra_casters"]
    mi_regs, mi_tot, _ = out["missing_casters"]
    lit_game = (game_b > T["sun_game_lit"]) & valid
    s_extra = _permille(ex_imp, game.shape)
    s_missing = _permille(mi_tot, game.shape)
    metrics = {"extra_area": round(_permille(ex_tot, game.shape), 3), "extra_impact_area": round(s_extra, 3),
               "missing_area": round(s_missing, 3),
               "agree_lit": round(float(((rt_b > 0.5) & lit_game).sum() / max(1, lit_game.sum())), 4),
               "game_lit_frac": round(float(lit_game.mean()), 4)}
    res = {
        # R0: casters the game's mask lacks may be right (our BVH has geometry its cascades
        # miss), so this is for review too.
        "extra": {"score": round(s_extra, 4), "threshold": None, "gate": False,
                  "pass": not ex_regs, "unit": "‰ of px (darkened by the sun term)",
                  "metrics": metrics, "regions": ex_regs},
        # With min(game, traced) a missing caster cannot change the image. With
        # rt_sun_replace only trusted pixels can (the "replaced" entry below); off-screen
        # casters (roofs, cut-away walls) are expected here, so this is a warning, not a gate.
        "missing": {"score": round(s_missing, 4), "threshold": None, "gate": False,
                    "pass": not mi_regs, "unit": "‰ of px", "metrics": metrics,
                    "regions": mi_regs},
    }
    if "replaced" in out:
        rp_regs, rp_tot, _ = out["replaced"]
        s_rep = _permille(rp_tot, game.shape)
        metrics["replaced_area"] = round(s_rep, 3)
        metrics["replaced_of_missing"] = round(s_rep / max(s_missing, 1e-9), 3) if s_missing > 0 else 0.0
        # Brightened against the game: physically right (over-wide filtering, acne) or a
        # missing caster leaking. For review (the crops), not a gate.
        res["replaced"] = {"score": round(s_rep, 4), "threshold": None, "gate": False,
                           "pass": not rp_regs, "unit": "‰ of px", "metrics": metrics,
                           "regions": rp_regs}
    return res


def animated(albedos, masks, shape, normals=()):
    """Pixels whose scene content changed across the traces of one temporal set: the
    G-buffer albedo (geometry moved: idle animation, walking NPCs, wind in grass) or
    the game's own sun mask (a caster moved: the shadow of an animated character or
    frond on a static floor). Both are inputs of the lighting, independent of every
    RT setting, so a change there is the scene's, not ours. albedos/masks: per trace,
    (H, W, 3) / (H, W) or None; resampled (nearest) to `shape`."""
    def fit(a):
        if a.shape[:2] == tuple(shape):
            return a
        ys = (np.arange(shape[0]) * a.shape[0]) // shape[0]
        xs = (np.arange(shape[1]) * a.shape[1]) // shape[1]
        return a[ys][:, xs]
    out = np.zeros(shape, bool)

    def changed(fields, step):
        # A static input still moves by the sub-pixel jitter (< 1 px) between frames, which
        # changes a boxed field by at most |grad| x 1 px, plus one code step of its encoding
        # (th.jitter_change). A change beyond that is the scene's motion.
        m = np.zeros(shape, bool)
        if len(fields) < 2:
            return m
        bound = np.maximum.reduce([th.jitter_change(f, step) for f in fields])
        for f in fields[1:]:
            m |= np.abs(f - fields[0]) > bound
        return m
    raw = [fit(a).mean(-1) for a in albedos if a is not None]
    if raw:
        out |= changed([im.box(a, 2) for a in raw], th.code_step(raw[0]))   # RGBA8: 1/255
    # Normals: wind in grass moves blades in front of grass of the same colour, which the
    # albedo misses (S3: the blade-streak std pattern of the all-off set, 39% masked). The
    # code step is measured from the decoded data, so a preview scaled by the sky sentinel
    # (one step larger than any real change) cannot mark anything.
    nr = [fit(n) for n in normals if n is not None]
    if nr:
        st = max(th.code_step(nr[0][..., c]) for c in range(2))
        for c in range(2):
            out |= changed([im.box(n[..., c], 2) for n in nr], st)
    mk = [fit(m) for m in masks if m is not None]
    if mk:
        out |= changed([im.box(m, 1) for m in mk], th.code_step(mk[0]))
    # The flicker test pools the std over a (2 flicker_box + 1)^2 box, so a moving pixel's std
    # reaches flicker_box px into its static neighbours: dilate by the same radius.
    # (The animated geometry's own contact lighting farther out is not covered: QA.md.)
    return im.dilate(out, T["flicker_box"])


def temporal(codes_on, codes_off, moving=None):
    """Per-pixel temporal std of the final frame's PQ code value (perceptually
    uniform), static camera, all-on against all-off.

    The two sets are captured about a minute apart, 240 presents between traces, so
    their std holds the scene's own motion in each window (an NPC that walks or turns
    in one window and idles in the other), not only our temporal noise. `moving`
    (animated() of both sets) excludes the pixels whose geometry or game sun mask
    changed; the gate is scored on the rest. The unmasked numbers stay in the metrics.

    Every limit is derived (docs/QA.md "Thresholds and their derivation"):
      - candidate regions are visible: std_on - std_off (5x5 box) above one 10-bit PQ step
        (flicker_jnd), static, at least the filters' support (th.min_area);
      - a region counts when it is significant: ln(std_on + q) - ln(std_off + q) over the
        region exceeds z sqrt(1 / (2 nu_on) + 1 / (2 nu_off)), nu = (n - 1) x min area x n_eff /
        box area (a scan statistic at the min-area scale), n_eff measured from the residuals' spatial correlation (th.n_eff_box), z
        Bonferroni at ALPHA_FW over every region the frame could hold, q half a preview step;
      - no such region is tolerated, and the mean excess over the static frame stays below
        one PQ step (frame-wide flicker too faint to form a region)."""
    def lum(stack):
        return np.stack([c[..., 0] * 0.2627 + c[..., 1] * 0.678 + c[..., 2] * 0.0593 for c in stack]).astype(np.float32)
    y_on, y_off = lum(codes_on), lum(codes_off)
    n_on, n_off = len(codes_on), len(codes_off)
    s_on, s_off = y_on.std(axis=0, ddof=1), y_off.std(axis=0, ddof=1)
    static = ~moving if moving is not None and moving.shape == s_on.shape else np.ones(s_on.shape, bool)
    q = T["flicker_quant"]
    R = T["flicker_box"]
    raw_ratio = float((s_on.mean() + q) / (s_off.mean() + q))
    ratio = float((s_on[static].mean() + q) / (s_off[static].mean() + q)) if static.any() else raw_ratio
    ne_on = th.n_eff_box(list(y_on - y_on.mean(0)), static, R)
    ne_off = th.n_eff_box(list(y_off - y_off.mean(0)), static, R)
    box_a = float((2 * R + 1) ** 2)
    min_a = _min_area(s_on.shape, R, R)
    # Bonferroni over every region the frame could hold (a priori: independent px / min area).
    z = th.z_one_sided(s_on.size / min_a)
    b_on, b_off = im.box(s_on, R), im.box(s_off, R)
    # Candidates: visible excess (one 10-bit PQ step of std); each is then tested as a region,
    # its std pooled over its area: nu = (n - 1) x area x n_eff / box area independent samples.
    visible = b_on - b_off > T["flicker_jnd"]
    raw_area = _permille(im.opening(visible, R).sum(), visible.shape)
    cand = im.opening(visible & static, R)
    lab, n = im.label(cand)
    regs, tot = [], 0
    for i, area, bb in im.regions(lab, n, min_a):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        so = float(s_on[y0:y1, x0:x1][sub].mean())
        sf = float(s_off[y0:y1, x0:x1][sub].mean())
        # Pooled over the minimum area only: a larger region is a union of noisy candidates
        # chosen by the same data, so its own area would credit the selection (scan statistic
        # at the min-area scale; frame-wide faint flicker is the mean-excess gate's job).
        nu_r_on = max(1.0, (n_on - 1) * min_a * ne_on / box_a)
        nu_r_off = max(1.0, (n_off - 1) * min_a * ne_off / box_a)
        lim = z * float(np.sqrt(1.0 / (2 * nu_r_on) + 1.0 / (2 * nu_r_off)))
        stat = float(np.log(so + q) - np.log(sf + q))
        sig_ok = stat > lim
        if sig_ok:
            tot += area
        regs.append({"bbox": bb, "area": area, "std_on": round(so, 4), "std_off": round(sf, 4),
                     "ln_ratio": round(stat, 3), "ln_limit": round(lim, 3), "qualified": sig_ok,
                     "rank": area * (so - sf) * (1.0 if sig_ok else 0.1)})
    regs.sort(key=lambda g: -g["rank"])
    regs = [g for g in regs if g["qualified"]] + [g for g in regs if not g["qualified"]]
    area = _permille(tot, cand.shape)
    lim_ln = z * np.sqrt(1.0 / (2 * (n_on - 1) * ne_on) + 1.0 / (2 * (n_off - 1) * ne_off))   # one box
    # Without room for one region of the minimum size the gate measures nothing: never a pass
    # (gi_flicker2_*: a broken normal decode masked 100% of S3 and "passed").
    inconclusive = int(im.erode(static, R).sum()) < min_a
    mean_excess = float(s_on[static].mean() - s_off[static].mean()) if static.any() else 0.0
    ok = mean_excess <= T["flicker_jnd"] and area <= T["flicker_fail_area"] and not inconclusive
    return {"score": round(mean_excess, 6), "threshold": round(T["flicker_jnd"], 6), "pass": ok,
            "unit": "mean std on - std off (PQ code, static px)",
            "metrics": {"std_on": round(float(s_on[static].mean()), 5) if static.any() else 0.0,
                        "std_off": round(float(s_off[static].mean()), 5) if static.any() else 0.0,
                        "ratio": round(ratio, 3),
                        "excess_area": round(area, 3), "excess_threshold": T["flicker_fail_area"],
                        "n_eff": [round(ne_on, 2), round(ne_off, 2)], "z": round(z, 3),
                        "ln_limit": round(float(lim_ln), 4),
                        "animated_frac": round(1.0 - float(static.mean()), 4), "inconclusive": inconclusive,
                        "unmasked": {"ratio": round(raw_ratio, 3), "excess_area": round(raw_area, 3)},
                        "frames": [n_on, n_off]},
            "regions": regs, "map": ("std", s_on)}


def view_shift(a, b):
    """Global displacement (dy, dx) in px (sub-pixel refined) of view b against view a: the
    peak of the phase correlation of their G-buffer albedo (mean channel). The albedo does not
    depend on any lighting setting; a camera move shifts all of it, scene motion (wind, NPCs)
    only parts. The sub-pixel jitter moves it by less than 1 px per axis, so a shift of 1 px or
    more on an axis is a camera move. A rotation or zoom about the centre has no single shift:
    the changed-pixel share (view_changed()) is reported next to it for review."""
    a = np.asarray(a, np.float64)
    b = np.asarray(b, np.float64)
    if a.ndim == 3:
        a, b = a.mean(-1), b.mean(-1)
    h, w = a.shape
    win = np.outer(np.hanning(h), np.hanning(w))
    fa = np.fft.rfft2((a - a.mean()) * win)
    fb = np.fft.rfft2((b - b.mean()) * win)
    x = fa * np.conj(fb)
    c = np.fft.irfft2(x / (np.abs(x) + 1e-12), s=(h, w))
    py, px = np.unravel_index(int(np.argmax(c)), c.shape)

    def sub(cm, c0, cp):
        # Parabolic peak refinement: a shift of a fraction of a pixel (the jitter) moves the
        # integer peak to the neighbour; the refined peak keeps it below 1 px.
        den = cm - 2 * c0 + cp
        return 0.5 * (cm - cp) / den if den < 0 else 0.0
    fy = py + sub(c[(py - 1) % h, px], c[py, px], c[(py + 1) % h, px])
    fx = px + sub(c[py, (px - 1) % w], c[py, px], c[py, (px + 1) % w])
    fy = fy - h if fy > h / 2 else fy
    fx = fx - w if fx > w / 2 else fx
    return round(-float(fy), 2), round(-float(fx), 2), float(c.max()), float(c[0, 0])


def view_changed(a, b):
    """Share of px whose boxed albedo changed beyond the jitter bound (th.jitter_change)."""
    if a.ndim == 3:
        a, b = a.mean(-1), b.mean(-1)
    fa, fb = im.box(a, 2), im.box(b, 2)
    bound = np.maximum(th.jitter_change(fa, th.code_step(a)), th.jitter_change(fb, th.code_step(a)))
    return float((np.abs(fa - fb) > bound).mean())


def sun_check(view):
    """rt_debug=7 trace (debug diffuse x 2): yellow = brightened by > 0.1 against
    min(game, traced) at the game's shadow edge, red = brightened farther from it, blue =
    darker than min. Everything else is dim grey (R = G = B <= 0.2). The 0.8 / 0.2 levels
    decode that colour code (saturated 1 or 0 against grey <= 0.2), they are not tolerances;
    the 0.1 of the classes is the shader's (rt_sun_resolve.hlsl, a runtime constant). Darker
    is impossible by construction with rt_sun_roof_mode=0, so one such pixel fails; far is
    for review (no threshold)."""
    r, g, b = view[..., 0], view[..., 1], view[..., 2]
    edge = (r > 0.8) & (g > 0.8) & (b < 0.2)
    far = (r > 0.8) & (g < 0.2) & (b < 0.2)
    darker = (b > 0.8) & (r < 0.2) & (g < 0.2)

    def regions(m):
        lab, n = im.label(im.dilate(m, 2))
        out = []
        for i, area, bb in im.regions(lab, n, 8):
            y0, y1, x0, x1 = bb
            out.append({"bbox": bb, "area": area, "px": int(m[y0:y1, x0:x1].sum()), "qualified": True,
                        "rank": area})
        out.sort(key=lambda g_: -g_["rank"])
        return out

    def pm(m):
        return round(_permille(int(m.sum()), view.shape), 4)

    metrics = {"brightened_edge": pm(edge), "brightened_far": pm(far), "darker": pm(darker),
               "brightened_total": pm(edge | far),
               "far_share": round(float(far.sum()) / max(1, int((edge | far).sum())), 4)}
    return {
        "far": {"score": metrics["brightened_far"], "threshold": None, "gate": False,
                "pass": not far.any(), "unit": "‰ of px",
                "metrics": metrics, "regions": regions(far)},
        "edge": {"score": metrics["brightened_edge"], "threshold": None, "gate": False, "pass": True,
                 "unit": "‰ of px", "metrics": metrics, "regions": regions(edge)},
        "darker": {"score": metrics["darker"], "threshold": T["sun_check_darker_fail"],
                   "pass": metrics["darker"] <= T["sun_check_darker_fail"], "unit": "‰ of px",
                   "metrics": metrics, "regions": regions(darker)},
    }

def _corr_length(res, mask, max_lag=32):
    """Spatial correlation length (px) of zero-mean residuals: the first lag at which the
    autocorrelation along x and along y (within `mask`) falls below 1/e. Used to thin a pixel
    test to independent samples: RR and the box filters correlate neighbours."""
    m = mask.astype(bool)
    r = np.where(m, res - float(res[m].mean()) if m.any() else 0.0, 0.0).astype(np.float64)
    v = float((r[m] ** 2).mean()) if m.any() else 0.0
    if v <= 0:
        return 1
    for lag in range(1, max_lag + 1):
        cx = (r[:, lag:] * r[:, :-lag])[m[:, lag:] & m[:, :-lag]]
        cy = (r[lag:, :] * r[:-lag, :])[m[lag:, :] & m[:-lag, :]]
        c = np.concatenate([cx, cy])
        if c.size == 0 or float(c.mean()) / v < np.exp(-1.0):
            return lag
    return max_lag


def sharc_reference(ys_cache, ys_ref, mask, z=None):
    """The cache's result against its physical reference (docs/QA.md "sharc_reference"):
    `ys_cache` repeated traces with the cache, `ys_ref` repeated traces of the reference (no
    cache, rays as long as the cache's, 8 per pixel). Every tolerance is measured:
      - per pixel, the noise of the difference of the two means, sigma_d = sqrt(var_c / N_c +
        var_r / N_r), from the traces' own per-pixel variance (unbiased, N - 1);
      - a pixel is in excess where mean_c - mean_r > sigma_d, in deficit where mean_r -
        mean_c > sigma_d. Under "no difference" both tails are equally likely, so the deficit
        tail is the measured control for the excess tail;
      - the counts are taken on a grid as wide as the measured correlation length, so the
        samples are independent, and the excess fails only if it exceeds the deficit by more
        than z times the binomial noise of that difference, sqrt(N_excess + N_deficit), with z
        the one-sided quantile at ALPHA_FW (thresholds.z_one_sided; the caller passes the
        Bonferroni share of its gate family). The sigma_d cut only defines the two symmetric tails.
    It fails only where the cache's result is brighter than the reference beyond the measured
    noise of both estimates."""
    c, r = np.stack(ys_cache).astype(np.float64), np.stack(ys_ref).astype(np.float64)
    nc, nr = c.shape[0], r.shape[0]
    mc, mr = c.mean(0), r.mean(0)
    sd = np.sqrt(c.var(0, ddof=1) / nc + r.var(0, ddof=1) / nr)
    d = mc - mr
    m = mask & (sd > 0) & np.isfinite(d)
    L = max(_corr_length(c[0] - mc, m), _corr_length(r[0] - mr, m))
    grid = np.zeros_like(m)
    grid[::L, ::L] = True
    excess, deficit = m & (d > sd), m & (-d > sd)
    ne, nd = int((excess & grid).sum()), int((deficit & grid).sum())
    zq = th.z_one_sided(1) if z is None else z
    noise = zq * float(np.sqrt(ne + nd))
    score = ne - nd
    tot = float(mr[m].sum()) if m.any() else 0.0
    excess_energy = float((d - sd)[excess].sum()) / tot if tot > 0 else 0.0
    lab, n = im.label(im.opening(excess, 1))
    regs = []
    for i, area, bb in im.regions(lab, n, _min_area(excess.shape)):
        y0, y1, x0, x1 = bb
        sub = lab[y0:y1, x0:x1] == i
        z = float(np.median((d / np.maximum(sd, 1e-12))[y0:y1, x0:x1][sub]))
        regs.append({"bbox": bb, "area": area, "excess_sigmas": round(z, 2), "qualified": True, "rank": area * z})
    regs.sort(key=lambda g: -g["rank"])
    return {"score": score, "threshold": round(noise, 2), "pass": score <= noise,
            "unit": "independent px in excess minus in deficit (> sigma_d), against z x their binomial noise",
            "metrics": {"traces": [nc, nr], "z": round(zq, 3), "alpha_fw": th.ALPHA_FW, "corr_length_px": L, "samples": int((m & grid).sum()),
                        "excess_samples": ne, "deficit_samples": nd,
                        "excess_permille": round(1000.0 * float(excess.sum()) / max(1, int(m.sum())), 3),
                        "deficit_permille": round(1000.0 * float(deficit.sum()) / max(1, int(m.sum())), 3),
                        "excess_energy_share": round(excess_energy, 5),
                        "mean_change_pct": round(100.0 * (float(mc[m].sum()) / tot - 1.0), 3) if tot > 0 else None},
            "regions": regs, "map": ("sigmas", np.where(m, d / np.maximum(sd, 1e-12), 0.0))}


def sharc_leak_cells(view, mask=None):
    """sharc_debug=6 trace (gi_trace_rt_sharc.hlsl): the cache's GI contribution per pixel split
    by the geometric leak test, red from a cell occluded from the query point, green clean,
    blue from a cell larger than the surface's thickness. Returns the shares of the
    contribution (frame, and inside `mask`). No threshold: a non-zero occluded share means the
    leak mechanism is active, and sharc_reject_occluded=1 (the default) removes exactly it."""
    def shares(m):
        r, g, b = (float(view[..., k][m].sum()) for k in range(3))
        t = r + g + b
        return {"occluded": round(r / t, 5) if t > 0 else 0.0, "thin": round(b / t, 5) if t > 0 else 0.0,
                "contribution": round(t, 3), "px": int(m.sum())}
    lit = view.sum(-1) > 0
    out = {"frame": shares(lit)}
    if mask is not None and mask.shape == lit.shape:
        out["roofed"] = shares(lit & mask)
    occ = (view[..., 0] > view[..., 1]) & (view[..., 0] > view[..., 2]) & lit
    lab, n = im.label(im.opening(occ, 1))
    regs = [{"bbox": bb, "area": area, "qualified": True, "rank": area} for _, area, bb in im.regions(lab, n, _min_area(occ.shape))]
    regs.sort(key=lambda g: -g["rank"])
    return {"score": out["frame"]["occluded"], "threshold": 0.0, "gate": False,
            "pass": out["frame"]["occluded"] == 0.0,
            "unit": "share of the cache's GI contribution from occluded cells (with the rejection off)",
            "metrics": out, "regions": regs}
