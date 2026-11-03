"""Hair qualification on decoded, same-run, registered arrays; never controls a game.

Masks must distinguish visible cards from visible strands (material class alone
cannot distinguish the two). Character IDs/ROIs must come from the cards frame,
including characters missing entirely from the candidate. Depth is linear metres;
motion is backward displacement in render pixels: previous(x + mv.x, y + mv.y).
"""
from __future__ import annotations

import numpy as np
from tools.live_batches.edge_flicker.analyze import energy as edge_energy


def verdict(name, value=None, limit=None, why="", invalid=False):
    status = "INVALID" if invalid else ("PASS" if value <= limit else "FAIL")
    return dict(name=name, status=status, value=value, threshold=limit,
                verdict=f"{status}: {name}: {why}")


def envelope(repeats, quantum, tests=1):
    """Prediction interval for one future sample, with Bonferroni family alpha .05.

    Student t accounts for estimating noise from few independent OFF repeats.
    Noise repeats must be independent stationary samples. Quantization is a
    hard floor. scipy is required: missing statistics is never a quality pass.
    """
    a = np.asarray(repeats, float)
    if a.ndim != 1 or len(a) < 3 or not np.isfinite(a).all() or quantum < 0:
        raise ValueError("need >= 3 finite independent repeat metrics and nonnegative precision")
    from scipy.stats import t
    k = float(t.ppf(1 - .05 / (2 * tests), len(a) - 1))
    return float(a.mean()), float(k * a.std(ddof=1) * np.sqrt(1 + 1 / len(a)) + quantum)


def bounded(name, value, repeats, quantum, tests=1, absolute=False):
    centre, tolerance = envelope(repeats, quantum, tests)
    delta = abs(value - centre) if absolute else value - centre
    return verdict(name, float(delta), tolerance,
                   f"observed {value:.6g}; OFF {centre:.6g}; repeat/precision bound {tolerance:.6g}")


def visual(cards, strands, visible_cards, character, strand_depth, scalp_depth,
           depth_precision, pixel_footprint, repeats, family=1):
    """One independently tracked character; repeats are OFF/OFF metric samples.

    Silhouette tolerance = measured OFF/OFF disagreement + one-pixel boundary
    footprint; no style-tuned percentage. Coverage uses the same bound. Samples
    outside support/behind an occluder must be removed identically beforehand.
    """
    from scripts.qa.imgops import erode
    c, s, v, roi = [np.asarray(x, bool) for x in (cards, strands, visible_cards, character)]
    if len({x.shape for x in (c, s, v, roi, strand_depth, scalp_depth)}) != 1:
        raise ValueError("mask/depth dimensions differ")
    c, s, v = c & roi, s & roi, v & roi
    if not roi.any() or not c.any():
        raise ValueError("character baseline has no hair support")
    boundary = (c & ~erode(c, 1)).sum() / c.sum()
    coverage_loss = max(0., float((c.sum() - s.sum()) / c.sum()))
    union = (c | s).sum()
    disagreement = 1 - (c & s).sum() / union if union else 1.
    results = [bounded("missing_hair", coverage_loss, repeats["coverage_loss"], boundary, family),
               bounded("silhouette_iou_loss", disagreement, repeats["iou_loss"], boundary, family)]
    if not s.any():
        results[0] = verdict("missing_hair", 1, 0, "baseline hair present, no visible strands")
    elif boundary >= 1:
        results[1] = verdict("silhouette_iou_loss", why="baseline thinner than one-pixel footprint", invalid=True)
    valid = np.isfinite(strand_depth) & np.isfinite(scalp_depth)
    tolerance = np.asarray(depth_precision) + np.asarray(pixel_footprint)
    if np.any(np.asarray(tolerance) < 0) or not np.isfinite(tolerance).all():
        raise ValueError("invalid depth precision/footprint")
    behind = s & ((strand_depth > scalp_depth + tolerance) | ~valid | (strand_depth <= 0))
    results.append(verdict("depth_sanity", int(behind.sum()), 0,
                           "linear strand depth must be finite, positive and in front of scalp within precision/footprint"))
    # Coplanar pixels are only a z-fighting risk if both representations are visible.
    zfight = s & v & (np.abs(strand_depth - scalp_depth) <= tolerance)
    results.append(verdict("z_fighting", int(zfight.sum()), 0, "simultaneous coplanar card/strand pixels"))
    results.append(verdict("duplicates", int((s & v).sum()), 0, "cards and strands simultaneously visible"))
    return results


def warp(previous, backward_mv):
    """Bilinear gather, clipped support returned separately; no toroidal wrap."""
    h, w = previous.shape[:2]
    if backward_mv.shape != (h, w, 2):
        raise ValueError("motion must be H x W x 2 render-pixel backward displacement")
    y, x = np.mgrid[:h, :w]
    xx, yy = x + backward_mv[..., 0], y + backward_mv[..., 1]
    finite = np.isfinite(xx) & np.isfinite(yy)
    support = finite & (xx >= 0) & (xx <= w-1) & (yy >= 0) & (yy <= h-1)
    xx, yy = np.clip(np.where(finite, xx, 0), 0, w-1), np.clip(np.where(finite, yy, 0), 0, h-1)
    x0, y0 = np.floor(xx).astype(int), np.floor(yy).astype(int)
    dx, dy = xx-x0, yy-y0
    if previous.ndim == 3:
        dx, dy = dx[..., None], dy[..., None]
    out = ((1-dx)*(1-dy)*previous[y0, x0] + dx*(1-dy)*previous[y0, np.minimum(x0+1,w-1)] +
           (1-dx)*dy*previous[np.minimum(y0+1,h-1), x0] + dx*dy*previous[np.minimum(y0+1,h-1), np.minimum(x0+1,w-1)])
    return out, support


def temporal(frames, masks, motion, disoccluded, repeats, quantum, family=1,
             fresh_frames=None, registered_frames=None):
    if len(frames) < 3 or len(masks) != len(frames) or len(motion) != len(frames)-1 or len(disoccluded) != len(motion):
        raise ValueError("need >=3 consecutive frames, masks, N-1 MVs and disocclusion masks")
    errors, trails = [], []
    if not np.isfinite(frames).all() or not np.isfinite(motion).all():
        raise ValueError("nonfinite colour or motion vectors")
    if fresh_frames is None or registered_frames is None:
        raise ValueError("trailing needs fresh-current references; flicker needs registered frames")
    if np.shape(fresh_frames) != np.shape(frames) or np.shape(registered_frames) != np.shape(frames):
        raise ValueError("fresh/registered reference dimensions differ")
    for i, mv in enumerate(motion, 1):
        p, support = warp(frames[i-1], mv)
        pm, _ = warp(np.asarray(masks[i-1], float), mv)
        mask = masks[i] & support & ~disoccluded[i-1] & (pm > 0)
        trail = (pm > 0) & ~masks[i] & support & ~disoccluded[i-1]
        if not mask.any():
            raise ValueError("no persistent hair pixels after reprojection/disocclusion")
        d = np.abs(frames[i] - p).mean(-1)
        errors.append(float(d[mask].mean()))
        # Residual bright colour outside the current silhouette, compared with
        # the current background from a matched no-history reference.
        ghost = np.abs(frames[i] - fresh_frames[i]).mean(-1)
        trails.append(float(ghost[trail].mean()) if trail.any() else 0.)
    stable = np.logical_and.reduce(masks)
    if not stable.any():
        raise ValueError("no common hair support for registered flicker residual")
    residual = edge_energy(list(registered_frames), stable)["residual"]
    return [bounded("motion_reprojection", np.mean(errors), repeats["reprojection"], quantum, family),
            bounded("trailing_residual", np.mean(trails), repeats["trailing"], quantum, family),
            bounded("flicker_residual", residual, repeats["flicker"], quantum, family)]


def lighting(cards_linear, strands_linear, character, off_repeats, quantum, pt=None, pt_repeats=None, family=1):
    """Scene-linear pre-post luminance only. PT is optional but never a pass if absent."""
    if not character.any():
        raise ValueError("empty character lighting mask")
    delta = float((strands_linear - cards_linear)[character].mean())
    result = [bounded("character_energy", delta, off_repeats, quantum, family, absolute=True)]
    if pt is None or pt_repeats is None:
        result.append(verdict("pt_shadow_backlight", why="no matched converged PT reference with noise repeats", invalid=True))
    else:
        error = float(np.abs(strands_linear - pt)[character].mean())
        result.append(bounded("pt_shadow_backlight", error, pt_repeats, quantum, family))
    return result
