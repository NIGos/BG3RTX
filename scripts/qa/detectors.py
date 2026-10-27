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
    # hard_dark review signature (not derivable: they rank crops, never gate; the gate is the
    # light-off floor below). An effect removes more than 60% of the luminance ...
    "dark_removed": 0.60,
    "dark_sharp": 0.50,        # ... sharp (only without a measured PSF; else step_sharpness) ...
    "dark_unexplained": 0.50,  # ... along an edge the effect-off image does not have ...
    "dark_black_shadows": 0.25,  # ... and, for shadows, this share at the floor (umbra)
    "dark_black_level": 0.10,  # near-black without a floor trace: < 10% of the light left
    # Physics: no pixel may go below the effect's light-off floor beyond k sigma (energy). False
    # alarms are bounded by the k-sigma test (th.k_sigma) and the filters' support (th.min_area).
    "dark_fail_area": 0.0,
    # The energy gate is review only until a run with the exposure held (qa_exposure_hold,
    # capture.plan) validates it: without the hold, the eye adaptation re-exposed the darker
    # light-off traces and the floors came out brighter than the images they bound (QA
    # 20261027-173531: S3 GI 320 permille, S14 GI 428 permille "below the floor").
    "dark_energy_gate": False,
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


def _edge_profile(rr, sub):
    """The `sharp` measurement of one region: the share of its drop (far ring 6-12 px out
    against the core 3 px in) that happens across its edge (3-px bands either side).
    Returns (sharp, step, full, n_in, n_out), or None without a ring."""
    in3 = sub & ~im.erode(sub, 3)
    core = im.erode(sub, 3)
    out3 = im.dilate(sub, 3) & ~sub
    far = im.dilate(sub, 12) & ~im.dilate(sub, 6)
    if not out3.any() or not far.any() or not in3.any():
        return None
    inside = float(np.median(rr[core])) if core.any() else float(np.median(rr[sub]))
    full = float(np.median(rr[far])) - inside
    step = float(np.median(rr[out3])) - float(np.median(rr[in3]))
    sharp = float(np.clip(step / full, 0, 1)) if full > 1e-3 else 0.0
    return sharp, step, full, int(in3.sum()), int(out3.sum())


_STEP_CACHE = {}


def step_sharpness(psf_sigma, blur):
    """`sharp` of an ideal geometric step as this pipeline renders it: a dark disk whose edge
    is blurred by the measured point spread (th.psf_sigma: RR, upscaling and the preview
    resampling) and then by the detector's own box. It is the largest `sharp` a real edge
    can reach in this run, so the reference for "as sharp as a geometric edge"."""
    key = (round(float(psf_sigma), 2), int(blur))
    if key not in _STEP_CACHE:
        H = 96
        yy, xx = np.mgrid[0:H, 0:H].astype(np.float32)
        t = (np.hypot(yy - H / 2, xx - H / 2) < 20).astype(np.float32)
        t = im.gauss(t, float(psf_sigma)) if psf_sigma > 0.3 else t
        r, _, _ = ratio_map(1.0 - 0.95 * t, np.ones_like(t), blur)
        lab, n = im.label(im.opening(r < 0.5, 1))
        best = 0.0
        for i, area, bb in im.regions(lab, n, 1):
            y0, y1, x0, x1 = im.window(bb, r.shape, 14)
            p = _edge_profile(r[y0:y1, x0:x1], lab[y0:y1, x0:x1] == i)
            if p:
                best = max(best, p[0])
        _STEP_CACHE[key] = best
    return _STEP_CACHE[key]


def hard_dark(y_on, y_off, sigma, blur=1, effect="", floor=None, moving=None, psf_sigma=None):
    """Light an effect removes that was not its own (gate), and dark regions with sharp,
    unexplained edges (review).

    Gate, physics (docs/QA.md "Thresholds and their derivation"): `floor` is the trace with
    the effect's own light removed (capture.FLOORS: sun mask, D-LS visibility or AO written
    0), everything else as in `y_on`. An effect that only removes its own light can never
    take a pixel below it, so pixels where floor - on > k sigma (k: Bonferroni over the
    independent samples) are an energy violation; any region of them fails. Pixels whose
    scene inputs moved between the traces (`moving`) are excluded. Without a floor the
    detector is review only.

    Review: regions where the effect removes > 60% of the light (beyond k sigma), ranked by
    the old signature: `sharp`, compared with the measured step response (`step_like`:
    sharp >= step_sharpness(psf) - z sigma_sharp, z Bonferroni over the regions tested),
    `unexplained` (no effect-off edge as steep as the drop, th.edge_threshold), `polygonal`,
    and `black`: with a floor, the share of the region the effect left at its floor (it
    removed all of its own light: an umbra); without, the share left under 10%."""
    r, a, b = ratio_map(y_on, y_off, blur)
    k = th.k_sigma(r.shape, blur)
    sig = np.asarray(sigma, np.float32)
    sig_px = np.broadcast_to(sig, r.shape)
    min_a = _min_area(r.shape, blur)
    static = ~moving if moving is not None and moving.shape == r.shape else np.ones(r.shape, bool)
    fl = im.box(floor, blur) if floor is not None and floor.shape == r.shape else None

    # ---- the gate: below the floor
    eregs, e_px, emask = [], 0, None
    if fl is not None:
        emask = im.opening(((fl - a) > k * sig) & static, 1)
        lab_e, n_e = im.label(emask)
        for i, area, bb in im.regions(lab_e, n_e, min_a):
            y0, y1, x0, x1 = bb
            sub = lab_e[y0:y1, x0:x1] == i
            deficit = float(np.median(((fl - a) / np.maximum(fl, 1e-9))[y0:y1, x0:x1][sub]))
            e_px += area
            eregs.append({"bbox": bb, "area": area, "below_floor": round(deficit, 3), "kind": "energy",
                          "qualified": True, "rank": 1e9 + area * deficit})

    # ---- review: the dark-region signature
    m = (r < 1.0 - T["dark_removed"]) & ((b - a) > k * sig)
    m = im.opening(m, 1)
    lab, n = im.label(m)
    lr = np.log(np.clip(r, 1e-3, 1e3))
    _, gx, gy = im.grad_mag(lr)
    # log of the effect-off image, clamped at its own noise (below it the log is noise).
    eoff, _, _ = im.grad_mag(np.log(np.maximum(b, np.maximum(sig, 1e-9))))
    edge_thr = th.edge_threshold(T["dark_removed"], blur)
    cands = im.regions(lab, n, min_a)
    s0 = step_sharpness(psf_sigma, blur) if psf_sigma is not None else None
    z_r = th.z_one_sided(max(1, len(cands)))
    regs = []
    for i, area, bb in cands:
        y0, y1, x0, x1 = im.window(bb, r.shape, 14)
        sub = lab[y0:y1, x0:x1] == i
        rr = r[y0:y1, x0:x1]
        prof = _edge_profile(rr, sub)
        if prof is None:
            continue
        sharp, step, full, n_in, n_out = prof
        step_like = None
        if s0 is not None and full > 1e-3:
            # Noise of the band medians (1.2533 sigma / sqrt(n) for a median) in ratio units.
            s_r = float(np.median((sig_px[y0:y1, x0:x1] / np.maximum(b[y0:y1, x0:x1], 1e-9))[sub]))
            s_sharp = 1.2533 * s_r * np.sqrt(1.0 / n_in + 1.0 / n_out) / full
            step_like = bool(sharp >= s0 - z_r * s_sharp)
        boundary = im.dilate(sub, 3) & ~im.erode(sub, 1) & ~(im.dilate(sub, 3) & ~im.dilate(sub, 1))   # 2-px band
        unexpl = float((eoff[y0:y1, x0:x1][boundary] < edge_thr).mean()) if boundary.any() else 0.0
        poly = im.polygonality(gx[y0:y1, x0:x1], gy[y0:y1, x0:x1], boundary)
        removed = float(1.0 - np.median(rr[sub]))
        if fl is not None:   # umbra: the effect left the pixel at its floor (all of its own light gone)
            deep = sub & ((a - fl)[y0:y1, x0:x1] < k * sig_px[y0:y1, x0:x1])
        else:
            deep = sub & (rr < T["dark_black_level"])
        black = float(deep.sum() / max(1, sub.sum()))
        fy, fx = (np.argwhere(deep).mean(axis=0) if deep.any() else np.argwhere(sub).mean(axis=0))
        sig_ok = removed >= T["dark_removed"] and (step_like if step_like is not None else sharp >= T["dark_sharp"]) \
            and unexpl >= T["dark_unexplained"] and (effect not in SHADOW_EFFECTS or black >= T["dark_black_shadows"])
        regs.append({"bbox": bb, "area": area, "removed": round(removed, 3), "sharp": round(sharp, 3),
                     "step_like": step_like, "unexplained": round(unexpl, 3), "polygonal": round(poly, 3),
                     "black": round(black, 3), "kind": "signature", "signature": bool(sig_ok),
                     "focus": (int(y0 + fy), int(x0 + fx)), "qualified": False,
                     "rank": area * removed * (0.25 + sharp) * (0.25 + unexpl) * (0.5 + black) * (1.0 if sig_ok else 0.1)})
    regs.sort(key=lambda g: -g["rank"])
    eregs.sort(key=lambda g: -g["rank"])
    gate = fl is not None and T["dark_energy_gate"]
    score = _permille(e_px, r.shape)
    sig_area = _permille(sum(g["area"] for g in regs if g["signature"]), r.shape)
    has_floor = fl is not None
    return {"score": round(score if has_floor else sig_area, 4), "threshold": T["dark_fail_area"] if gate else None,
            "gate": gate, "pass": (score <= T["dark_fail_area"]) if gate else (not eregs and not any(g["signature"] for g in regs)),
            "unit": ("‰ of px below the light-off floor" + ("" if gate else " (review until the exposure-held run validates it)"))
            if has_floor else "‰ of px with the dark signature (review)",
            "metrics": {"candidates": len(regs), "signature": sum(g["signature"] for g in regs),
                        "signature_permille": round(sig_area, 4), "energy_regions": len(eregs),
                        "mean_ratio": round(float(np.mean(a) / max(np.mean(b), 1e-9)), 4),
                        "k_sigma": round(k, 3), "edge_thr": round(edge_thr, 4), "min_area_px": min_a,
                        "psf_sigma_px": None if psf_sigma is None else round(float(psf_sigma), 3),
                        "step_sharp": None if s0 is None else round(s0, 3), "floor": has_floor},
            "regions": eregs + regs, "map": ("ratio", r), "energy_mask": emask}


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
        lo = min(float(f.min()) for f in fields)
        hi = max(float(f.max()) for f in fields)
        for f in fields[1:]:
            d = np.abs(f - fields[0])
            # Plus the input's own frame-to-frame noise, measured: the game's sun mask is a
            # dithered PCF that differs on static pixels between any two frames (S3
            # 20261027-174810: without this term 30% of on* and 99.6% of all sets marked).
            # Most pixels are static, so the median |difference| is that noise
            # (MAD x 1.4826 = sigma of the difference), tested at the buffer's k sigma.
            # Measured on the pixels that can carry it: a dither cannot move a value clipped
            # at its code range (a fully lit or fully shadowed mask), so pixels at the range
            # end in both frames are left out. Over all pixels the median was 0 in S14 and
            # S3 (2/3 of the mask saturated), and the shadow regions' dither still marked
            # 27-31% of the frame; measured on the unsaturated third (sigma 0.06-0.09): 0.1-0.6%.
            act = ((fields[0] > lo + step) & (fields[0] < hi - step)) | ((f > lo + step) & (f < hi - step))
            sig = 1.4826 * float(np.median(d[act])) if act.any() else 0.0
            m |= d > bound + th.k_sigma(shape, 1) * sig
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
    # Candidates: boxes whose own excess is visible (one 10-bit PQ step of std) AND significant
    # on its own (one-sided at ALPHA_FW, nu = (n - 1) x n_eff per box, no Bonferroni: a screen;
    # the region test below controls the family). Without the per-box test, every box with a
    # visible excess joined, and in S3 (20261027-173531) one 104,000-px region spanned the
    # whole ground, its mean set by a grass patch. Each region is then tested with its std
    # pooled over the minimum area (scan statistic, Bonferroni over the regions).
    visible = b_on - b_off > T["flicker_jnd"]
    z_box = th.z_one_sided(1)
    box_lim = z_box * np.sqrt(1.0 / (2 * (n_on - 1) * ne_on) + 1.0 / (2 * (n_off - 1) * ne_off))
    box_sig = (np.log(b_on + q) - np.log(b_off + q)) > box_lim
    raw_area = _permille(im.opening(visible & box_sig, R).sum(), visible.shape)
    cand = im.opening(visible & box_sig & static, R)
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


def tlas_front_decode(v):
    """rt_debug=8 view (debug diffuse x 2, rt_shadow.hlsl / rt_debug.hlsl): R = log2(e)/8 with
    e the distance the TLAS hit lies in front of the depth buffer in units of the derived
    tolerance (depth_tol.hlsli + the measured 0.5% TLAS error), G = 1 alpha-tested hit, 0.5
    opaque hit, B = 0.25 where the pixel has depth. Returns (l = log2 e, kind, has_depth),
    kind 2 alpha-tested, 1 opaque, 0 not in front."""
    r, g, b = v[..., 0], v[..., 1], v[..., 2]
    has_depth = b > 0.125                      # half-way between 0 and the 0.25 code
    kind = np.where(g > 0.75, 2, np.where(g > 0.25, 1, 0))   # half-way between the 0/0.5/1 codes
    return 8.0 * np.clip(r, 0, 1), kind, has_depth


def tlas_code_sigma(l):
    """Rounding of the l = log2 e code through its chain, per pixel (in l units): the RGBA8
    rtVis (one code = 8/255 of l), the R11G11B10F diffuse (6-bit mantissa: half an ulp is 2^-7
    relative, uniform: 2^-6 / sqrt(12)), the 8-bit preview (th.preview_quant_sigma). Static
    per pixel, so it does not average out over the traces."""
    b = np.clip(l / 8.0, 0, 1)
    s_rgba8 = (8.0 / 255.0) / np.sqrt(12.0)
    s_r11 = 8.0 * b * (2.0 ** -6) / np.sqrt(12.0)
    s_prev = 8.0 * 2.0 * th.preview_quant_sigma((0.5 * b)[..., None], "hdr")[..., 0]
    return np.sqrt(s_rgba8 ** 2 + s_r11 ** 2 + s_prev ** 2)


def tlas_front(views, preview=True):
    """Geometry the ray-traced scene has in front of the frame's own surfaces (QA finding 1:
    grass cards whose alpha test returned "solid" occlude GI, sun and reflections as quads).

    `views`: the rt_debug=8 view (x 2) of N >= 2 traces of the same view. Per pixel:
      l = log2(e), e = (raster depth - TLAS hit) / tolerance, so l > 0 is in front beyond the
      derived same-surface tolerance (shaders/rt/depth_tol.hlsli);
      sigma of the mean = sqrt(s^2 / N + q^2): s the std over the traces (the jitter and the
      stochastic alpha test change it; max-filtered over the 1 px the jitter moves), q the
      code rounding (tlas_code_sigma; only with `preview`);
      significant where mean l > k sigma, k = th.k_sigma(shape, 0) (Bonferroni over the px).
    Gate: significant px whose hit is alpha-tested in most traces. The raster and the trace
    run the same alpha test on that geometry, so a TLAS cutout in front of the raster
    surface is the trace keeping texels the raster discarded: an artifact, none tolerated.
    Review: the same with an opaque hit (cut-away walls, dithered LOD fades: geometry the
    TLAS legitimately keeps; R0 says the real scene includes it)."""
    ls, kinds, dep = zip(*[tlas_front_decode(v) for v in views])
    L = np.stack(ls)
    K = np.stack(kinds)
    has_depth = np.logical_and.reduce(dep)
    n = L.shape[0]
    mean = L.mean(0)
    s = im.maxf(L.std(0, ddof=1), 1) if n >= 2 else np.zeros_like(mean)
    q = tlas_code_sigma(mean) if preview else 0.0
    sig_mean = np.sqrt(s ** 2 / max(n, 1) + np.square(q))
    k = th.k_sigma(mean.shape, 0)
    signif = has_depth & (mean > k * sig_mean) & (mean > 0)
    alpha = (K == 2).sum(0) * 2 > (K > 0).sum(0)    # alpha-tested in most of the traces in front
    opaque = (K > 0).any(0) & ~alpha
    min_a = _min_area(mean.shape, 0, 1)
    out = {}
    for name, m in (("alpha", signif & alpha), ("opaque", signif & opaque)):
        m = im.opening(m, 1)
        lab, nr = im.label(m)
        regs, tot = [], 0
        for i, area, bb in im.regions(lab, nr, min_a):
            y0, y1, x0, x1 = bb
            sub = lab[y0:y1, x0:x1] == i
            e_med = float(2.0 ** np.median(mean[y0:y1, x0:x1][sub]))
            tot += area
            regs.append({"bbox": bb, "area": area, "front_tol": round(e_med, 2), "qualified": name == "alpha",
                         "rank": area * np.log2(1 + e_med)})
        regs.sort(key=lambda g: -g["rank"])
        out[name] = (regs, _permille(tot, mean.shape))
    metrics = {"traces": n, "k_sigma": round(k, 3), "min_area_px": min_a,
               "alpha_front_permille": round(out["alpha"][1], 4), "opaque_front_permille": round(out["opaque"][1], 4),
               "in_front_any_permille": round(_permille(int(((K > 0).any(0) & has_depth).sum()), mean.shape), 4),
               "depth_px_frac": round(float(has_depth.mean()), 4)}
    return {
        "alpha": {"score": round(out["alpha"][1], 4), "threshold": 0.0, "pass": out["alpha"][1] <= 0.0,
                  "unit": "‰ of px: alpha-tested TLAS geometry in front of the raster, beyond tolerance and k sigma",
                  "metrics": metrics, "regions": out["alpha"][0]},
        "opaque": {"score": round(out["opaque"][1], 4), "threshold": None, "gate": False, "pass": not out["opaque"][0],
                   "unit": "‰ of px: opaque TLAS geometry in front (cut-aways, LOD fades: review)",
                   "metrics": metrics, "regions": out["opaque"][0]},
    }, mean


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


def sharc_reference(ys_cache, ys_ref, mask):
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
        than the binomial noise of that difference, sqrt(N_excess + N_deficit).
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
    noise = float(np.sqrt(ne + nd))
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
            "unit": "independent px in excess minus in deficit (> sigma_d), against their binomial noise",
            "metrics": {"traces": [nc, nr], "corr_length_px": L, "samples": int((m & grid).sum()),
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
