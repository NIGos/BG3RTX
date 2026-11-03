"""Consecutive rendered-frame reflection support/delta toggles.

Debug 25 is sampled BEFORE post processing: red/green encode signed relative
compose luminance delta; blue is unresolved. It distinguishes tier 1 from all
unresolved tiers, but cannot distinguish hidden/off-screen/sky. Never decode a
tonemapped or presented debug image as these exact codes.
All other water effects must be OFF: debug25's delta includes their changes too.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from .pan_artifacts import decode


def consecutive(ids, count):
    if len(ids) != count or count < 2 or np.any(np.diff(ids) != 1):
        raise ValueError('need at least two consecutive rendered record frames')


def debug25(rgb):
    """Return signed delta, tier (1 or -1 unresolved, 0 excluded), valid.

    Channel zeros are exact even in B10G11R11. Positive code magnitudes are not
    compared to a tuned tolerance: .3/.25 blue survive their native encoding.
    Black is a tier-1 replacement with zero luminance delta, NOT missing water.
    Callers must supply a same-frame record coverage mask to distinguish black
    non-water background if the capture does not restrict pixels to records.
    """
    a = np.asarray(rgb)
    if a.ndim != 4 or a.shape[-1] < 3 or not np.isfinite(a).all() or np.any(a < 0):
        raise ValueError('debug25 must be finite nonnegative T,H,W,RGB')
    r, g, b = (a[..., k] for k in range(3))
    resolved = (b == 0) & ((r == 0) | (g == 0))
    unresolved = (r == 0) & (g == 0) & (b > 0)
    return g-r, np.where(resolved, 1, np.where(unresolved, -1, 0)), resolved | unresolved


def derived_threshold(calibration, valid, quantum):
    """Static-repeat maximum adjacent change plus two storage error bounds.

    `quantum` is a per-pixel upper bound on one sample's native storage ULP.
    Calibration must already use the same screen samples as the measured turn.
    Uncalibrated pixels are excluded, never reported as a zero rate.
    """
    c, v = np.asarray(calibration), np.asarray(valid, bool)
    if c.ndim != 3 or c.shape != v.shape or len(c) < 2:
        raise ValueError('need at least two static calibration frames and matching validity')
    q = np.broadcast_to(np.asarray(quantum), c.shape[1:])
    if not np.isfinite(c).all() or not np.isfinite(q).all() or np.any(q <= 0):
        raise ValueError('invalid calibration or storage quantum')
    known = np.all(v, axis=0)
    noise = np.max(np.where(v[1:] & v[:-1], np.abs(np.diff(c, axis=0)), 0), axis=0)
    return noise + 2*q, known


def count_toggles(signal, tiers, valid, threshold, frame_ids):
    """Screen-pixel level crossings; denominators are adjacent valid samples.

    abs(delta)>threshold is ON, including darkening replacements. Normal view
    motion can cross this level too: this measures switching, not a proof of a
    renderer defect. No optical-flow correspondence is claimed.
    """
    s, t, v = np.asarray(signal), np.asarray(tiers), np.asarray(valid, bool)
    if s.ndim != 3 or t.shape != s.shape or v.shape != s.shape:
        raise ValueError('signal/tier/valid dimensions differ')
    consecutive(frame_ids, len(s))
    bound = np.broadcast_to(np.asarray(threshold), s.shape[1:])
    if not np.isfinite(s).all() or not np.isfinite(bound).all() or np.any(bound <= 0):
        raise ValueError('nonfinite signal or invalid threshold')
    active = np.abs(s) > bound
    usable = v[1:] & v[:-1]
    flips = usable & (active[1:] != active[:-1])
    exits = (t[:-1] == 1) & (t[1:] == -1)
    entries = (t[:-1] == -1) & (t[1:] == 1)
    rows = []
    for i in range(len(s)-1):
        rows.append(dict(before=int(frame_ids[i]), after=int(frame_ids[i+1]),
                         valid_pixels=int(usable[i].sum()), toggles=int(flips[i].sum()),
                         tier_1_to_x=int((flips[i] & exits[i]).sum()),
                         tier_x_to_1=int((flips[i] & entries[i]).sum())))
    denom = int(usable.sum())
    return dict(status='VALID' if denom else 'INCOMPLETE: no calibrated adjacent water samples',
                toggles=int(flips.sum()), valid_pixel_pairs=denom,
                rate=float(flips.sum()/denom) if denom else None,
                on_to_off=int((flips & active[:-1]).sum()),
                off_to_on=int((flips & active[1:]).sum()),
                tier_1_to_x=int((flips & exits).sum()),
                tier_x_to_1=int((flips & entries).sum()),
                same_tier=int((flips & (t[1:] == t[:-1])).sum()),
                tier_transition_counts=dict(tier_1_to_x=int((usable & exits).sum()),
                                            tier_x_to_1=int((usable & entries).sum())),
                per_pair=rows, correspondence='same screen pixel; adjacent common record coverage',
                threshold_method='static maximum adjacent change plus two native storage ULP bounds')


def analyze_debug25(rgb, ids, calibration, calibration_ids, quantum, coverage, calibration_coverage):
    consecutive(calibration_ids, len(calibration))
    signal, tiers, valid = debug25(rgb)
    c, _, cv = debug25(calibration)
    if np.shape(coverage) != signal.shape or np.shape(calibration_coverage) != c.shape:
        raise ValueError('same-frame water record coverage is mandatory')
    valid &= np.asarray(coverage, bool)
    cv &= np.asarray(calibration_coverage, bool)
    threshold, known = derived_threshold(c, cv, quantum)
    result = count_toggles(signal, tiers, valid & known, threshold, ids)
    result.update(signal='absolute signed relative compose luminance delta',
                  tier_x='unresolved aggregate (2/3/4/5/6); subtypes unavailable',
                  calibrated_pixels=int(known.sum()),
                  threshold_range=[float(threshold[known].min()), float(threshold[known].max())] if known.any() else None)
    return result


def native_quantum(rgb, fmt):
    """Per-pixel conservative ULP over all RGB samples (pre-post native data)."""
    maximum = np.max(np.asarray(rgb), axis=(0, 3))
    if fmt == 122:  # R11G11B10: five mantissa bits is the coarsest channel
        return np.exp2(np.maximum(np.floor(np.log2(np.maximum(maximum, 2.0**-14))), -14)-5)
    if fmt == 97:
        v = maximum.astype('f2')
        return np.nextafter(v, np.float16(np.inf)).astype('f4')-v.astype('f4')
    if fmt in (37, 43, 44, 50):
        return np.full(maximum.shape, 1/255)
    raise ValueError(f'unsupported signal storage precision {fmt}')


def analyze_game_output(rgb, ids, calibration, calibration_ids, quantum, coverage, calibration_coverage):
    """Native-water brightness level crossings, explicitly NOT isolated SSR.

    The zero reference is each screen sample's static lower luminance envelope.
    Glints, refraction, animated normals and camera motion remain confounders.
    This control cannot by itself support or exclude the game's SSR candidate.
    """
    consecutive(calibration_ids, len(calibration))
    weights = np.array([.2126, .7152, .0722])
    rgb, calibration = np.asarray(rgb), np.asarray(calibration)
    if rgb.ndim != 4 or calibration.ndim != 4 or rgb.shape[-1] != 3 or calibration.shape[-1] != 3:
        raise ValueError('game output must be T,H,W,RGB')
    s, c = rgb @ weights, calibration @ weights
    if np.shape(coverage) != s.shape or np.shape(calibration_coverage) != c.shape:
        raise ValueError('same-frame water record coverage is mandatory')
    threshold, known = derived_threshold(c, calibration_coverage, quantum)
    signal = np.maximum(s-np.min(c, axis=0), 0)
    result = count_toggles(signal, np.zeros(s.shape, int), np.asarray(coverage,bool) & known, threshold, ids)
    result.update(signal='game water output brightness above static lower envelope',
                  attribution='PROXY ONLY: native SSR not isolated; glints/refraction/motion remain',
                  supports_ssr_candidate=False)
    return result


def export_reflection_mask(rgb, coverage, destination):
    """Tier-1 union within all-frame common exact record coverage.

    Reuse this one fixed screen-space region on every beauty turn at the same
    pose/protocol. Registration is a proxy across turns; there is no cross-turn
    pixel subtraction or per-pixel motion correspondence. The PNG uses the pure
    green code accepted by fg_present_flicker's existing coverage_mask decoder.
    """
    _, tiers, valid = debug25(rgb)
    c = np.asarray(coverage, bool)
    if c.shape != tiers.shape:
        raise ValueError('reflection mask needs same-frame exact record coverage')
    mask = np.all(c, axis=0) & np.any((tiers == 1) & valid, axis=0)
    out = np.zeros((*mask.shape, 3), dtype='u1')
    out[...,1] = mask*255
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(out).save(destination)
    return dict(path=str(destination.resolve()), pixels=int(mask.sum()),
                region='tier-1 union inside all-frame common exact record coverage',
                registration='fixed screen-space proxy reused across beauty turns; no cross-turn pixel subtraction')


def validate_manifest_frames(meta, entries):
    ids = [e['record_frame'] for e in entries]
    consecutive(ids, len(entries))
    count = meta.get('K')
    if count is not None and len(ids) != count:
        raise ValueError('sample count does not match declared K')
    if 'raw_record_frames' in meta:
        if count is None or meta['raw_record_frames'] != count+1:
            raise ValueError('raw record frame count must be K plus one omitted warmup')
    if 'omitted_warmup_record_frame' in meta:
        warmup = meta['omitted_warmup_record_frame']
        if count is None or ids != list(range(warmup+1,warmup+1+count)):
            raise ValueError('retained record frames do not follow the declared warmup')
    for e in entries:
        if 'submit_frame' in e and (e['submit_frame'] < 0 or e['submit_frame'] >= 2**64-1):
            raise ValueError('sample has no valid submission')
    return ids


def load_stream(directory, name):
    """Load the existing burst sample manifest; never accept missed records."""
    directory = Path(directory)
    meta = json.loads((directory/'burst.json').read_text())
    if not meta.get('valid_transport'):
        raise ValueError('invalid transport')
    entries = sorted((e for e in meta['samples'] if e['name'] == name), key=lambda e:e['record_frame'])
    ids = validate_manifest_frames(meta, entries)
    formats = {e['format'] for e in entries}
    if len(formats) != 1:
        raise ValueError('signal storage format changed')
    return np.stack([decode(directory/e['file'], e)[...,:3] for e in entries]), ids, formats.pop()


def load_coverage(directory, expected_ids, shape):
    directory = Path(directory)
    meta = json.loads((directory/'burst.json').read_text())
    if not meta.get('valid_transport'):
        raise ValueError('invalid coverage transport')
    entries = sorted((e for e in meta['samples'] if e['name'] == 'water_coverage'), key=lambda e:e['record_frame'])
    if validate_manifest_frames(meta,entries) != list(expected_ids):
        raise ValueError('coverage frames do not exactly match signal frames')
    masks=[]
    for e in entries:
        if e['format'] != 13 or (e['height'],e['width']) != tuple(shape):
            raise ValueError('coverage must be same-size native R8_UINT')
        masks.append(np.frombuffer((directory/e['file']).read_bytes(),dtype='u1').reshape(shape) != 0)
    return np.stack(masks)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('turn', type=Path)
    ap.add_argument('--calibration', required=True, type=Path)
    ap.add_argument('--signal', choices=('compose','game'), default='compose')
    ap.add_argument('--coverage', type=Path, help='Optional NPZ override with turn/calibration boolean T,H,W record masks; defaults to exact water_coverage stream')
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--mask-out', type=Path, help='Export exact tier-1 union region for same-protocol beauty/RR comparisons (compose signal only)')
    a = ap.parse_args(argv)
    try:
        stream = 'water_compose_debug' if a.signal == 'compose' else 'water_game'
        rgb, ids, fmt = load_stream(a.turn, stream)
        cal, cids, cfmt = load_stream(a.calibration, stream)
        if fmt != cfmt:
            raise ValueError('calibration storage format differs')
        if a.coverage:
            with np.load(a.coverage, allow_pickle=False) as masks:
                coverage, cc = masks['turn'], masks['calibration']
        else:
            coverage=load_coverage(a.turn,ids,rgb.shape[1:3])
            cc=load_coverage(a.calibration,cids,cal.shape[1:3])
        signal_rgb = np.concatenate([rgb,cal])
        quantum = native_quantum(signal_rgb[...,:2] if a.signal == 'compose' else signal_rgb,fmt)
        analyzer = analyze_debug25 if a.signal == 'compose' else analyze_game_output
        result = analyzer(rgb, ids, cal, cids, quantum, coverage, cc)
        if a.mask_out:
            if a.signal != 'compose':
                raise ValueError('--mask-out requires compose debug signal')
            result['reflection_region'] = export_reflection_mask(rgb,coverage,a.mask_out)
    except (ValueError, KeyError, OSError) as error:
        result = dict(status='INVALID', error=str(error))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'VALID' else 1


if __name__ == '__main__':
    raise SystemExit(main())
