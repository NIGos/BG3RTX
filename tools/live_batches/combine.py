"""Combine prepared jobs by save, or extract and judge their slices after a run.

  python tools/live_batches/combine.py job.json ... --out tools/live_batches/combined
  python tools/live_batches/combine.py --extract manifest.json --log run.log --runner run.runner.txt

Preparation always runs live_flow's dryrun lints and never stages a queue inbox.
Extraction runs original post commands only with --run-post (the combined job sets it).
"""
from __future__ import annotations

import argparse
import math
from copy import deepcopy
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from scripts import queue_gates as gates


def settings(text):
    out = {}
    gates._apply(out, text)
    return out


def defaults(preset):
    """Use the shipped settings and generated central preset schema, never guesses."""
    state = {}
    for line in (ROOT / "bg3rtx.ini").read_text(encoding="utf-8-sig").splitlines():
        line = line.split("#", 1)[0].split(";", 1)[0].strip()
        if "=" in line:
            key, value = line.split("=", 1)
            state[key.strip()] = value.strip()
    schema = flow.read(ROOT / "data/settings_schema.json")
    if preset:
        for knob in schema["knobs"]:
            value = knob.get("preset_values", {}).get(preset)
            if value is None:
                continue
            if knob["kind"] == "choice":
                state.update(knob["choices"][int(value)]["keys"])
            else:
                for key in knob["keys"]:
                    state[key] = format(float(value) * float(knob.get("scale", 1)), ".9g")
        for p in schema["presets"]:
            if p["id"] == preset:
                state.update(p.get("extra", {}))
    return state


def assignments(state):
    return ",".join(f"{k}={v}" for k, v in sorted(state.items()))


def slug(text):
    return re.sub(r"[^a-zA-Z0-9_-]+", "-", text).strip("-")


def path_arg(path):
    try:
        return Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(Path(path).resolve())


def combine_group(sources, plan_arg):
    """Pure composition. sources is [(source_path, job, plan), ...]."""
    first = sources[0][1]
    def options(j):
        args = list(j.get("args", []))
        i = args.index("-MovePlan")
        return args[:i] + args[i + 2:]
    for _, job, _ in sources:
        if any(job.get(k) != first.get(k) for k in ("save", "tree", "window_mode")) or options(job) != options(first):
            raise ValueError("same-save jobs require compatible tree, window_mode and non-plan args")
        if job.get("command") or job.get("session") or job.get("phases") or job.get("repeat", 1) != 1:
            raise ValueError("combine requires single MovePlan jobs, no command/session/phases/repeat")
        for key in ("expected_fps", "settings_latency_s", "trace_stall_s"):
            if job.get("live_flow", {}).get(key) != first.get("live_flow", {}).get(key):
                raise ValueError(f"incompatible live_flow.{key} timing assumptions")
    startup = [settings(j.get("set", "")) for _, j, _ in sources]
    presets = {s.get("preset", "") for s in startup}
    if len(presets) != 1:
        raise ValueError("incompatible startup presets")
    baseline = defaults(next(iter(presets)))
    keys = set().union(*(set(s) for s in startup),
                       *(set(settings(p.get("set", ""))) for _, _, plan in sources for p in plan))
    unknown = keys - set(baseline) - set.intersection(*(set(s) for s in startup))
    if unknown:
        raise ValueError(f"cannot reset unknown settings without explicit baselines in every job: {sorted(unknown)}")
    for k in set().union(*(set(j.get("live_flow", {}).get("startup_only", [])) for _, j, _ in sources)) | {"preset"}:
        if len({s.get(k, baseline.get(k)) for s in startup}) > 1:
            raise ValueError(f"incompatible startup-only key {k}")
    combined = deepcopy(first)
    combined.update(name="combined-" + slug(first["save"]), live_batch="combined", requires_success_of=[],
                    patterns=[], expect=[], expect_every=[], expect_runner=[], post_gate=True)
    combined.pop("post", None)
    combined.pop("live_flow", None)
    for field in ("estimated_phase_seconds", "purpose", "framing_status"):
        combined.pop(field, None)
    args = combined["args"]
    args[args.index("-MovePlan") + 1] = str(plan_arg)
    combined["forbid"] = sorted(set.intersection(*(set(j.get("forbid", [])) for _, j, _ in sources)))
    plan, slices = [], []
    for n, ((path, job, original), initial) in enumerate(zip(sources, startup)):
        prefix = f"b{n + 1}-{slug(job['name'])}--"
        reset = {k: initial.get(k, baseline.get(k)) for k in keys}
        if n:
            plan.append(dict(label=prefix + "settings-reset", kind="static", seconds=5, repeat_ms=0,
                             capture_at=0, serial=0, motion_start=5, motion_end=5, set=assignments(reset)))
        else:
            combined["set"] = assignments(reset)
        offset = len(plan)
        labels = {p["label"]: prefix + p["label"] for p in original}
        for p in original:
            p = deepcopy(p)
            p["label"] = labels[p["label"]]
            plan.append(p)
        for field in ("patterns", "expect"):
            combined[field] += [p for p in job.get(field, []) if p not in combined[field]]
        for g in job.get("expect_every", []):
            g = deepcopy(g)
            g["name"] = prefix + g.get("name", g["select"])
            g["windows"] = [labels[w] for w in g.get("windows", list(labels))]
            if g.get("trend"):
                for field in ("baseline", "tail"):
                    g[field] = [labels[w] for w in g.get(field, [])]
            combined["expect_every"].append(g)
        for rx in job.get("expect_runner", []):
            if "motion-plan" in rx and "complete phases=" in rx:
                continue
            rx = re.sub(r"phase=(\d+)", lambda m: f"phase={int(m[1]) + offset}", rx)
            for old, new in labels.items():
                rx = rx.replace("label=" + old, "label=" + re.escape(new))
            if rx not in combined["expect_runner"]:
                combined["expect_runner"].append(rx)
        slices.append(dict(prefix=prefix, offset=offset, count=len(original), source=str(path), job=job, plan=original))
    combined["expect_runner"].append(rf"\[motion-plan\] complete phases={len(plan)}\b")
    combined["timeout"] = int(sum(float(p["seconds"]) + float(p.get("wait_log_timeout", 0)) for p in plan) + 300)
    combined["hold"] = max(90, *(j.get("hold", 0) for _, j, _ in sources))
    combined["estimated_phase_seconds"] = sum(float(p["seconds"]) for p in plan)
    combined["live_flow"] = deepcopy(first.get("live_flow", {}))
    combined["live_flow"]["segments"] = [dict(offset=s["offset"], count=s["count"],
                                                set=s["job"].get("set", ""), live_flow=s["job"].get("live_flow", {}))
                                           for s in slices]
    return combined, plan, dict(version=1, slices=slices)


def extract(manifest, log, runner, out, run_post=False, status=""):
    """Restore each source plan's phase numbering and labels. Fail closed on drift.

    Retain the shared launch preamble plus only this batch's timestamped game
    lines. Trace paths remain original, so existing float-energy readers work.
    """
    log, runner, out = Path(log), Path(runner), Path(out)
    text = log.read_text(encoding="utf-8-sig", errors="replace")
    runtext = runner.read_text(encoding="utf-8-sig", errors="replace")
    plan = manifest["plan"]
    lines = gates.stamped_lines(text)
    wins = gates.windows(runtext, plan, lines)
    if (len(wins) != len(plan) or gates.plan_drift(wins, plan)
            or any(not math.isfinite(w['end']) for w in wins)
            or not re.search(r"^Run ended: done\b", runtext, re.MULTILINE)):
        raise ValueError("cannot extract: incomplete runner or plan drift")
    results = []
    for batch in manifest["slices"]:
        job, original = batch["job"], batch["plan"]
        offset, count = batch["offset"], batch["count"]
        start, end = wins[offset]["start"], wins[offset + count - 1]["end"]
        sliced = "\n".join(l for t, l in lines if t < wins[0]["start"] or start <= t < end) + "\n"
        kept = []
        clock = gates._Clock()
        for line in runtext.splitlines():
            timestamp = clock(gates._sec(line))
            m = re.search(r"\[pan-plan\] window (\d+)\b|\[motion-trace\] phase=(\d+)\b", line)
            if m:
                i = int(m[1] or m[2])
                if not offset <= i < offset + count:
                    continue
                line = re.sub(r"(\[pan-plan\] window |\[motion-trace\] phase=)\d+", lambda x: x[1] + str(i - offset), line)
                line = line.replace("label=" + batch["prefix"], "label=")
            elif "[motion-plan] complete" in line or "Run ended:" in line:
                continue
            elif timestamp is not None:
                if not (timestamp < wins[0]["start"] or start <= timestamp < end):
                    continue
            kept.append(line)
        h, rem = divmod(end % 86400, 3600)
        mi, sec = divmod(rem, 60)
        kept += [f"{int(h):02}:{int(mi):02}:{sec:06.3f} [motion-plan] complete phases={count}", "Run ended: done"]
        run_slice = "\n".join(kept) + "\n"
        # Preserve the original queue log basename convention used by Run.find_job.
        folder = out / batch["prefix"].removesuffix("--")
        folder.mkdir(parents=True, exist_ok=True)
        suffix = ".".join(log.name.split(".")[1:]) if "." in log.name else "1.000000.log"
        dst = folder / (job["name"] + "." + suffix)
        dst.write_text(sliced, encoding="utf-8")
        dst.with_suffix(".runner.txt").write_text(run_slice, encoding="utf-8")
        flow.write(folder / "plan.json", original)
        flow.write(folder / "job.json", job)
        errors = gates.check(job.get("expect_every", []), sliced, run_slice, original)
        for field, source in (("expect", sliced), ("expect_runner", run_slice)):
            errors += [f"missing {field}: {rx}" for rx in job.get(field, []) if not re.search(rx, source, re.IGNORECASE | re.MULTILINE)]
        errors += [f"forbidden log pattern: {rx}" for rx in job.get("forbid", []) if re.search(rx, sliced, re.IGNORECASE | re.MULTILINE)]
        post_exit = None
        if run_post and job.get("post"):
            captures = re.findall(r"\[trace\].*written:.* -> (.+)", sliced)
            cmd = job["post"].replace("{log}", str(dst)).replace("{runner}", str(dst.with_suffix(".runner.txt"))).replace("{status}", str(status)).replace("{captures}", " ".join(captures))
            post = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True)
            post_exit = post.returncode
            (folder / "post.txt").write_text(post.stdout + post.stderr, encoding="utf-8")
            if job.get("post_gate") and post_exit:
                errors.append(f"post gate exit {post_exit}")
        results.append(dict(job=job["name"], valid=not errors, problems=errors, log=str(dst), post_exit=post_exit))
    flow.write(out / "results.json", results)
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jobs", nargs="*")
    ap.add_argument("--out", type=Path, default=ROOT / "tools/live_batches/combined")
    ap.add_argument("--results", type=Path, default=flow.default_results())
    ap.add_argument("--extract", type=Path)
    ap.add_argument("--log", type=Path)
    ap.add_argument("--runner", type=Path)
    ap.add_argument("--status", default="")
    ap.add_argument("--run-post", action="store_true")
    a = ap.parse_args(argv)
    if a.extract:
        if not a.log or not a.runner:
            ap.error("--extract requires --log and --runner")
        results = extract(flow.read(a.extract), a.log, a.runner, a.out, a.run_post, a.status)
        for r in results:
            print(f"{r['job']}: {'VALID' if r['valid'] else 'INVALID'} {r['problems']}")
        return int(any(not r["valid"] for r in results))
    if not a.jobs:
        ap.error("provide job.json files")
    if "pending" in a.out.resolve().parts:
        ap.error("draft output must be outside every pending inbox")
    groups = {}
    for path in a.jobs:
        job, plan, _ = flow.load_job(path)
        groups.setdefault(job["save"], []).append((path, job, plan))
    index = flow.corpus(a.results, ROOT / "captures/.cache/live_flow_corpus.json")
    bad = False
    memo = {}
    for save, sources in groups.items():
        stem = "combined-" + slug(save)
        plan_path = a.out / "plans" / (stem + ".json")
        job, plan, manifest = combine_group(sources, path_arg(plan_path))
        manifest["plan"] = plan
        manifest_path = a.out / (stem + ".manifest.json")
        job["post"] = f'py -3 tools/live_batches/combine.py --extract "{path_arg(manifest_path)}" --log "{{log}}" --runner "{{runner}}" --status "{{status}}" --out "{{log}}.batches" --run-post'
        problems = []
        # Check each batch independently: a prior batch must not accidentally
        # satisfy its warmup or hide insufficient windows.
        for path, src, phases in sources:
            problems += [f"{path}: {p}" for p in flow.lint(src, phases, index["entries"], memo)["problems"]]
        problems += flow.window_problems(job, plan)[0]
        problems += flow.regex_problems(job, index["entries"], memo)[0]
        if not index["entries"]:
            problems.append("no VALID corpus evidence")
        flow.write(plan_path, plan)
        flow.write(manifest_path, manifest)
        flow.write(a.out / "jobs" / (stem + ".json"), job)
        flow.write(a.out / (stem + ".dryrun.json"), dict(problems=problems, launches_before=len(sources), launches_after=1))
        print(f"{save}: {len(sources)} launches -> 1; {len(plan)} phases; dryrun {'FAIL' if problems else 'PASS'}")
        for problem in problems:
            print(problem)
        bad |= bool(problems)
    return int(bad)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError) as e:
        print(f"combine: {e}", file=sys.stderr)
        sys.exit(1)
