"""Regenerate active drafts and their lint inventory. Never stages or launches.

Run as python -m tools.live_batches.regenerate_active. --audit-only refreshes
the inventory after individual generators; --before accepts a live_flow report.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from tools.live_batches import live_flow as flow

BATCHES = "smoke_ultra water_surface_fresnel cov_paks_compact post_temporal hair_validation hair_crowd gpu_opt1 gpu_opt2 water_offscreen water_flicker_wf9 edge_flicker combined_20261103".split()


def active_paths():
    for batch in BATCHES:
        paths = flow.job_files(flow.ROOT / "tools/live_batches" / batch)
        for path in paths:
            # Oversized historical combinations are retained, but superseded
            # by independently gated chunks from the same source generators.
            if batch == "combined_20261103" and any(p.stem.startswith(path.stem + "-part") for p in paths):
                continue
            yield path


def comparison(path, job, result, before):
    duration = result['duration']
    old = before.get(str(path), {}).get('duration', {})
    basis = 'original job'
    if not old and job.get('live_flow', {}).get('segments'):
        manifest = flow.read(path.parent.parent / (path.stem + '.manifest.json'))
        originals = [before.get(s['source'], {}).get('duration', {}) for s in manifest['slices']]
        if all(originals):
            old = dict(seconds=sum(d['seconds'] for d in originals) + 1.1 * (len(originals) - 1),
                       expected_readiness_seconds=sum(d.get('expected_readiness_seconds', 0) for d in originals))
            basis = 'original source jobs plus settings resets; new split'
    return dict(before_seconds=round(old['seconds'], 3) if 'seconds' in old else None,
        before_readiness_seconds=old.get('expected_readiness_seconds'), before_basis=basis,
        after_seconds=round(duration['seconds'], 3), readiness_seconds=duration['expected_readiness_seconds'],
        preflight_seconds=round(duration['seconds'] + duration['expected_readiness_seconds'], 3),
        problems=result['problems'])


def mark_historical(inventory, before):
    normalized = {str(Path(k)): v for k, v in inventory.items()}
    inventory.clear()
    inventory.update(normalized)
    for path in flow.job_files(flow.ROOT / 'tools/live_batches'):
        key = str(path.relative_to(flow.ROOT))
        inventory.setdefault(key, dict(before.get(str(path), {})))
    for result in inventory.values():
        result['active'] = False


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--audit-only", action="store_true")
    ap.add_argument("--before", type=Path)
    args = ap.parse_args()
    inventory_path = flow.ROOT / "tools/live_batches/duration_lint.json"
    inventory = flow.read(inventory_path)
    before = flow.read(args.before)["jobs"] if args.before else {str(flow.ROOT / Path(k)): v for k, v in inventory.items()}
    if not args.audit_only:
        env = dict(os.environ, PYTHONPATH=str(flow.ROOT))
        for i, batch in enumerate(BATCHES):
            generator = next((flow.ROOT / "tools/live_batches" / batch).glob("make*.py"))
            print(f"Generating {i + 1}/{len(BATCHES)}: {batch}", flush=True)
            subprocess.run([sys.executable, str(generator)], env=env, check=True)
    index = flow.corpus(flow.default_results(), flow.ROOT / "captures/.cache/live_flow_corpus.json", newest=100000)
    # Share regex findings across jobs, including a newly rebuilt corpus index.
    index.setdefault('matches', {})
    flow.fps_windows.configure(index["entries"])
    flow.configure_trace_exports(index["entries"])
    flow.readiness_budget.configure(index["entries"])
    mark_historical(inventory, before)
    report = dict(jobs={}, conservative_keys=sorted(flow.pipeline_readiness.CONSERVATIVE), no_game_launched=True)
    failed = 0
    paths = list(active_paths())
    progress = dict(done=0, failures=0)
    stopped = threading.Event()
    started = time.monotonic()
    def heartbeat():
        while not stopped.wait(30):
            elapsed = time.monotonic() - started
            rate = progress['done'] / elapsed
            eta = f"{(len(paths) - progress['done']) / rate:.0f} s" if rate else 'pending first job'
            print(f"Audit {progress['done']}/{len(paths)}, {rate:.3f} jobs/s, ETA {eta}, failures {progress['failures']}", flush=True)
    threading.Thread(target=heartbeat, daemon=True).start()
    for i, path in enumerate(paths):
        print(f"Auditing {i + 1}/{len(paths)}: {path.stem}", flush=True)
        job, plan, _ = flow.load_job(path)
        result = flow.lint(job, plan, index["entries"], index.get("matches", {}))
        result["problems"] += flow.launch_problems(job, result["duration"])
        result["active"] = True
        inventory[str(path.relative_to(flow.ROOT))] = result
        report["jobs"][job["name"]] = comparison(path, job, result, before)
        failed += bool(result["problems"])
        progress.update(done=i + 1, failures=failed)
    stopped.set()
    report["superseded"] = {Path(k).stem: v["duration"]["seconds"] for k, v in before.items()
                            if "combined_20261103" in k and Path(k) not in paths and "duration" in v}
    report['historical_jobs'] = sum(not v['active'] for v in inventory.values())
    report['max_preflight_seconds'] = max(v['preflight_seconds'] for v in report['jobs'].values())
    flow.write(inventory_path, inventory)
    flow.write(flow.ROOT / "tools/live_batches/readiness_report.json", report)
    flow.write(flow.ROOT / "captures/.cache/live_flow_corpus.json", index)
    print(f"Active lint: {failed}/{len(paths)} failing", flush=True)
    return int(bool(failed))


if __name__ == "__main__":
    sys.exit(main())
