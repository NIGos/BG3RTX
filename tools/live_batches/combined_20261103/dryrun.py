"""Lint combined jobs and exercise every real manifest's extraction without a game."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import combine as c, live_flow as f


def stamp(seconds):
    hours, rem = divmod(seconds, 3600)
    minutes, sec = divmod(rem, 60)
    return f"{int(hours):02}:{int(minutes):02}:{sec:06.3f}"


def main():
    here = Path(__file__).resolve().parent
    if f.main([str(here), "--preflight", "--active"]):
        return 1
    manifests = sorted(here / (p.stem + ".manifest.json") for p in f.active_job_files() if p.parent.parent == here)
    assert manifests, "expected active combined manifests"
    with tempfile.TemporaryDirectory(prefix="bg3rtx-combined-offline-") as scratch:
        scratch = Path(scratch)
        for path in manifests:
            manifest = f.read(path)
            loglines = ["00:00:00.000 synthetic launch preamble"]
            runner, elapsed = [], 1.0
            for i, step in enumerate(manifest["plan"]):
                runner.append(f"{stamp(elapsed)} [pan-plan] window {i} set={step['set']}")
                loglines.append(f"{stamp(elapsed + .1)} synthetic-phase-{i} END")
                for repeat, at in enumerate(step.get("trace_at", [])):
                    runner.append(f"{stamp(elapsed + at)} [motion-trace] phase={i} label={step['label']} repeat={repeat}")
                elapsed += step["seconds"]
            runner += [f"{stamp(elapsed)} [motion-plan] complete phases={len(manifest['plan'])}", "Run ended: done"]
            log, run = scratch / f"{path.stem}.1.000000.log", scratch / f"{path.stem}.runner.txt"
            log.write_text("\n".join(loglines), encoding="utf-8")
            run.write_text("\n".join(runner), encoding="utf-8")
            # Real gates must reject synthetic logs; no original post is run.
            rejected = c.extract(manifest, log, run, scratch / path.stem / "gated")
            assert all(not r["valid"] for r in rejected), rejected
            structural = deepcopy(manifest)
            for batch in structural["slices"]:
                for key in ("expect", "expect_every", "expect_runner", "forbid", "post"):
                    batch["job"].pop(key, None)
            results = c.extract(structural, log, run, scratch / path.stem / "structural")
            assert all(r["valid"] for r in results), results
            for batch, result in zip(manifest["slices"], results):
                text = Path(result["log"]).read_text(encoding="utf-8")
                expected = set(range(batch["offset"], batch["offset"] + batch["count"]))
                for i in range(len(manifest["plan"])):
                    assert (f"synthetic-phase-{i} END" in text) == (i in expected)
                restored = Path(result["log"]).with_suffix(".runner.txt").read_text(encoding="utf-8")
                assert batch["prefix"] not in restored
                assert f"complete phases={batch['count']}" in restored
            print(f"PASS {path.stem}: {len(results)} slices; real gates reject fake data; extraction restores isolated phases", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
