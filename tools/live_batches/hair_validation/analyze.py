"""Offline report: python -m tools.live_batches.hair_validation.analyze RUN.json --out DIR.

Input is a per-run manifest, see docs/HAIR_VALIDATION.md. No game launching.
Absent observables are INVALID; a clean partial log never qualifies the feature.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import numpy as np
from scripts.qa import hair


def complete_inventory(checks):
    """A partial run still lists every required quality observable explicitly."""
    names = ["missing_hair", "silhouette_iou_loss", "depth_sanity", "z_fighting", "duplicates",
             "character_energy", "pt_shadow_backlight"]
    names += [f"{stream}/{metric}" for stream in ("rendered", "presented")
              for metric in ("motion_reprojection", "trailing_residual", "flicker_residual")]
    for name in names:
        if not any(c["name"] == name or c["name"].endswith("/"+name) for c in checks):
            checks.append(hair.verdict(name, why="required capture evidence absent", invalid=True))
    return checks


def log_checks(text, validation=None, pipelines=()):
    checks = []
    for name, pattern in {
        "device_faults": r"VK_ERROR_DEVICE_LOST|device (?:was )?lost|\[fault\]\s+(?:read|write|execute) invalid",
        "exceptions": r"\[crash\] exception 0x",
        "pool_full": r"pool full",
        "hair_guards": r"(?:invalid/missing native state|retired bounds-invalid primitives) [1-9]\d*|set 1 [1-9]\d* out-of-range reads skipped|\[hair[^\]]*\].*(?:guard|invalid|out.of.range)[=: ]+[1-9]\d*",
    }.items():
        matches = [m.group(0) for m in re.finditer(pattern, text, re.I)]
        checks.append(hair.verdict(name, len(matches), 0, "; ".join(matches[:3]) or "zero forbidden events"))
    for name, pattern in {
        "guard_evidence": r"\[alpha hair\].*invalid/missing native state 0;.*retired bounds-invalid primitives 0;",
        "completion": r"\[motion-plan\] complete phases=\d+",
        "strand_participation": r"[1-9]\d* G-buffer and [1-9]\d* velocity strand draws, 0 pipelines refused",
    }.items():
        checks.append(hair.verdict(name, 0 if re.search(pattern, text) else 1, 0, "required log evidence"))
    if validation is None:
        checks.append(hair.verdict("gpuav", why="dedicated validation log absent", invalid=True))
    else:
        # Validation messages span multiple lines. Retain all errors, separating
        # attribution by debug pipeline name/handle rather than guessing from VUID.
        blocks = re.split(r"(?=Validation Error)", validation)
        errors = [b for b in blocks if re.search(r"Validation Error|VUID-|UNASSIGNED-.*(?:GPU|Error)", b, re.I)]
        ours = [b for b in errors if any(p.lower() in b.lower() for p in pipelines)]
        checks.append(hair.verdict("gpuav_our_pipelines", len(ours), 0, f"{len(ours)} attributed blocks; {len(errors)-len(ours)} unclassified/other blocks"))
        checks.append(hair.verdict("gpuav_all_errors", len(errors), 0, "unattributed errors also block qualification"))
        if not re.search(r"GPU.assisted.*(?:enabled|active)|gpuav_enable\s*[=:]\s*true", validation, re.I):
            checks.append(hair.verdict("gpuav_active", why="no GPU-AV activation evidence", invalid=True))
    return checks


def performance(samples, family):
    checks, metrics = [], []
    for n in (1, 5, 10):
        s = [s for s in samples if s.get("characters") == n]
        if not s:
            checks.append(hair.verdict(f"performance_{n}", why="no observed character-count cohort", invalid=True))
            continue
        for row in s:
            if not row.get("exclusive") or row.get("diagnostics") or row.get("contention"):
                raise ValueError("performance requires exclusive, diagnostic-free, uncontended windows")
            frames = np.asarray(row["frame_ms"], float)
            if len(frames) < 100 or not np.isfinite(frames).all() or (frames <= 0).any():
                raise ValueError("p99 needs >=100 positive finite rendered frame times")
            p99 = float(np.percentile(frames, 99))
            checks.append(hair.verdict(f"p99_{n}_{row['window']}", p99, row["frame_budget_ms"], "budget = 1000 / target rendered Hz, declared before run"))
            checks.append(hair.verdict(f"vram_{n}_{row['window']}", row["vram_mb"], row["device_budget_mb"], "device-local usage <= driver budget"))
            checks.append(hair.bounded(f"hair_cost_{n}_{row['window']}", row["hair_gpu_ms"], row["off_gpu_ms"], row["timer_quantum_ms"], family))
            metrics.append(dict(characters=n, window=row["window"], hair_gpu_ms=row["hair_gpu_ms"],
                                amortized_ms_per_character=row["hair_gpu_ms"]/n, p99_frame_ms=p99,
                                vram_mb=row["vram_mb"]))
    return checks, metrics


def analyze(manifest, root):
    checks, metrics = [], {}
    def read(p):
        return (root / p).read_text(encoding="utf-8", errors="replace")
    log = read(manifest["game_log"]) + "\n" + read(manifest["runner_log"])
    checks += log_checks(log, read(manifest["validation_log"]) if manifest.get("validation_log") else None,
                         manifest.get("pipeline_identifiers", []))
    metrics["logged_hair_gpu_ms"] = [float(x) for x in re.findall(
        r"\[hair primary\] strand G-buffer draws GPU ([0-9.]+) ms/frame", log)]
    metrics["logged_vram_mb"] = [int(x) for x in re.findall(
        r"device-local VRAM of this process: used (\d+) MB", log)]
    metrics["logged_rendered_p99_ms"] = [float(x) for x in re.findall(
        r"\[pace\] rendered frame time over \d+ frames: median [0-9.]+ ms .*?p99 ([0-9.]+) ms", log)]
    windows = manifest.get("windows", [])
    family = max(1, len(windows) * 20)
    required = manifest.get("required_windows", [])
    if not required or set(required) != {w["name"] for w in windows}:
        checks.append(hair.verdict("window_inventory", why="all predicted windows required exactly once", invalid=True))
    if len({w["name"] for w in windows}) != len(windows):
        raise ValueError("duplicate window labels")
    for observable in ("characters_enter_leave", "lod_transitions", "repeated_toggles", "smoke_ultra"):
        if not manifest.get("observed", {}).get(observable):
            checks.append(hair.verdict(observable, why="capture/log provenance not supplied", invalid=True))
    for window in windows:
        prefix = window["name"]
        if window.get("run_id") != manifest.get("run_id") or not window.get("registered"):
            checks.append(hair.verdict(prefix, why="same-run identity and registration required", invalid=True))
            continue
        with np.load(root / window["arrays"], allow_pickle=False) as data:
            # Iterate baseline inventory: a bald candidate cannot remove a character.
            ids = np.unique(data["character_ids"])
            for identity in ids[ids > 0]:
                roi = data["character_ids"] == identity
                try:
                    result = hair.visual(data["cards"], data["strands"], data["visible_cards"], roi,
                                         data["strand_depth"], data["scalp_depth"], window["depth_precision_m"],
                                         data["footprint_m"], window["repeats"], family)
                    checks += [dict(r, name=f"{prefix}/character_{identity}/{r['name']}") for r in result]
                    result = hair.lighting(data["cards_linear"], data["strands_linear"], roi,
                                            window["repeats"]["energy_delta"], window["linear_quantum"],
                                            data["pt"] if "pt" in data else None,
                                            window["repeats"].get("pt_error"), family)
                    checks += [dict(r, name=f"{prefix}/character_{identity}/{r['name']}") for r in result]
                    for stream in ("rendered", "presented"):
                        stream_roi = data[f"{stream}_character_ids"] == identity
                        t = hair.temporal(data[f"{stream}_frames"], data[f"{stream}_masks"] & stream_roi,
                                          data[f"{stream}_mv"], data[f"{stream}_disoccluded"],
                                          window["repeats"][stream], window["code_quantum"], family,
                                          data[f"{stream}_fresh"], data[f"{stream}_registered"])
                        checks += [dict(r, name=f"{prefix}/character_{identity}/{stream}/{r['name']}") for r in t]
                except (ValueError, KeyError) as e:
                    checks.append(hair.verdict(f"{prefix}/character_{identity}", why=str(e), invalid=True))
            if not np.any(ids > 0):
                checks.append(hair.verdict(prefix, why="empty baseline character inventory", invalid=True))
    p, metrics["performance"] = performance(manifest.get("performance", []), family)
    checks += p
    if not re.search(r"\[hair-reload\] selected save reload complete", log):
        checks.append(hair.verdict("save_reload", why="no post-reload scene-ready/strand evidence", invalid=True))
    checks = complete_inventory(checks)
    status = "FAIL" if any(c["status"] == "FAIL" for c in checks) else ("INVALID" if any(c["status"] == "INVALID" for c in checks) else "PASS")
    return dict(schema=1, run_id=manifest.get("run_id"), status=status, checks=checks, metrics=metrics)


def write_report(report, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / "summary.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    (out / "report.md").write_text(f"# Hair validation: {report['status']}\n\n" +
                                  "\n".join("- "+r["name"]+": "+r["verdict"] for r in report["checks"])+"\n", encoding="utf-8")


def diagnostic_flicker(windows, roi, quantum):
    """Same-run, stationary/registered consecutive RGB code arrays; cards ROI is fixed.

    Three independent cards bursts estimate the temporal metric's envelope; adjacent
    frames/pixels are not treated as independent noise repeats. This is a diagnostic,
    not a linear-energy, silhouette, PT or full MV/trailing qualification.
    """
    from tools.live_batches.edge_flicker.analyze import energy
    from tools.live_batches.hair_validation.make_job import CAPTURE_WINDOWS, DIAGNOSTIC_WINDOWS
    if set(windows) != set(CAPTURE_WINDOWS):
        raise ValueError("need every predicted capture window exactly once")
    roi = np.asarray(roi, bool)
    metrics = {}
    if roi.ndim != 2 or not roi.any() or quantum <= 0 or not np.isfinite(quantum):
        raise ValueError("nonempty baseline player-hair ROI and positive encoding quantum required")
    for name, frames in windows.items():
        frames = np.asarray(frames)
        if frames.ndim != 4 or frames.shape[0] < 3 or frames.shape[1:] != (*roi.shape, 3) or not np.isfinite(frames[:,roi]).all():
            raise ValueError("need finite consecutive RGB frames with a common ROI/resolution")
        metrics[name] = energy(list(frames), roi)
    repeats = [metrics['cards-'+c]['residual'] for c in 'abc']
    # FIR coefficients (1, -1/2, -1/2) amplify code precision by at most 2.
    centre, tolerance = hair.envelope(repeats, 2 * quantum, len(DIAGNOSTIC_WINDOWS))
    checks = [hair.verdict(name+'/edge_flicker', metrics[name]['residual']-centre, tolerance,
                          "same-run cards repeats + encoding precision; stationary player-head ROI")
              for name in DIAGNOSTIC_WINDOWS]
    return dict(schema=1, status='FAIL' if any(c['status']=='FAIL' for c in checks) else 'PASS',
                checks=checks, metrics=metrics, cards_residual=centre, envelope=tolerance,
                scope='Rendered edge-flicker diagnostic only; no energy or full temporal qualification')


def diagnose(manifest, root):
    from tools.live_batches.edge_flicker.analyze import burst, registered_run, aligned_frames
    from tools.live_batches.hair_validation.make_job import CAPTURE_WINDOWS
    if not manifest.get('run_id'):
        raise ValueError('same-run identity required')
    entries = manifest['captures']
    if len(entries) != len(CAPTURE_WINDOWS) or {e['window'] for e in entries} != set(CAPTURE_WINDOWS):
        raise ValueError('missing or duplicate predicted capture windows')
    if any(e.get('run_id') != manifest['run_id'] for e in entries):
        raise ValueError('cross-run comparison forbidden')
    directories = [(root/e['directory']).resolve() for e in entries]
    if len(set(directories)) != len(directories):
        raise ValueError('cards repeats/candidates must use distinct bursts')
    raw = {e['window']:burst(d)['codes'] for e,d in zip(entries,directories)}
    anchor = raw['cards-a'][0]
    x0,y0,x1,y1 = manifest['hair_roi']
    h,w = anchor.shape[:2]
    if not (0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h):
        raise ValueError('hair_roi must bound the player head in cards-a coordinates')
    roi = np.zeros((h,w),bool); roi[y0:y1,x0:x1] = True
    aligned = {}
    for name, frames in raw.items():
        rx0, ry0, rx1, ry1 = manifest['rigid_roi']
        if not (0 <= rx0 < rx1 <= w and 0 <= ry0 < ry1 <= h):
            raise ValueError('rigid_roi must bound stationary non-hair structure')
        _, registration = registered_run(anchor, frames, dict(roi_extent=(w,h), subtitle_top=h,
            rois=dict(rigid_control=manifest['rigid_roi'])))
        if len(registration['included']) != len(frames):
            raise ValueError(name+': registration excluded frames; consecutive evidence required')
        for frame in registration['frames']:
            frame['shift_px'] = tuple(a+b for a,b in zip(frame['shift_px'], registration['shift_px']))
        aligned[name] = aligned_frames(frames, registration)
        if not np.isfinite(np.asarray(aligned[name])[:,roi]).all():
            raise ValueError(name+': registration clipped player hair ROI')
    return diagnostic_flicker(aligned, roi, manifest['code_quantum'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    try:
        manifest = json.loads(a.manifest.read_text(encoding="utf-8"))
        report = (diagnose if manifest.get("kind") == "hv6" else analyze)(manifest, a.manifest.parent)
    except (KeyError, ValueError, OSError) as e:
        report = dict(schema=1, status="INVALID", checks=[hair.verdict("input", why=str(e), invalid=True)])
    write_report(report, a.out)
    print(report["status"])
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
