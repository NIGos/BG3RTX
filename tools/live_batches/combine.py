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


def _repo_relative(path):
    """Manifests store repo-relative sources: an absolute worktree path breaks every other checkout."""
    root = Path(__file__).resolve().parents[2]
    try:
        return Path(path).resolve().relative_to(root).as_posix()
    except ValueError:
        return str(path)


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
    names = {j["name"] for _, j, _ in sources}
    prerequisites = list(dict.fromkeys(dep for _, j, _ in sources for dep in j.get("requires_success_of", [])))
    if names.intersection(prerequisites):
        raise ValueError("cannot combine batches that depend on each other")
    def options(j):
        args = list(j.get("args", []))
        i = args.index("-MovePlan")
        return args[:i] + args[i + 2:]
    for _, job, _ in sources:
        if any(job.get(k) != first.get(k) for k in ("save", "tree", "window_mode")) or options(job) != options(first):
            raise ValueError("same-save jobs require compatible tree, window_mode and non-plan args")
        if job.get("command") or job.get("session") or job.get("phases") or job.get("repeat", 1) != 1:
            raise ValueError("combine requires single MovePlan jobs, no command/session/phases/repeat")
        for key in ("expected_fps", "settings_latency_s"):
            if job.get("live_flow", {}).get(key) != first.get("live_flow", {}).get(key):
                raise ValueError(f"incompatible live_flow.{key} timing assumptions")
    startup = [settings(j.get("set", "")) for _, j, _ in sources]
    baselines = [defaults(s.get("preset", "")) for s in startup]
    baseline = baselines[0]
    keys = set().union(*(set(s) for s in startup),
                       *(set(settings(p.get("set", ""))) for _, _, plan in sources for p in plan))
    # Preset changes also reset keys untouched by the source plans. Pin every
    # differing preset value so the next batch cannot inherit the prior preset.
    preset_keys = set().union(*(set(b) for b in baselines))
    keys.update(k for k in preset_keys if len({b.get(k) for b in baselines}) > 1)
    unknown = keys - set.intersection(*(set(b) for b in baselines)) - set.intersection(*(set(s) for s in startup))
    if unknown:
        raise ValueError(f"cannot reset unknown settings without explicit baselines in every job: {sorted(unknown)}")
    for k in set().union(*(set(j.get("live_flow", {}).get("startup_only", [])) for _, j, _ in sources)):
        if len({s.get(k, b.get(k)) for s, b in zip(startup, baselines)}) > 1:
            raise ValueError(f"incompatible startup-only key {k}")
    combined = deepcopy(first)
    combined.update(name="combined-" + slug(first["save"]), live_batch="combined", requires_success_of=prerequisites,
                    patterns=[], expect=[], expect_every=[], expect_runner=[], post_gate=True, fail_fast=True)
    combined.pop("post", None)
    combined.pop("live_flow", None)
    for field in ("estimated_phase_seconds", "purpose", "framing_status"):
        combined.pop(field, None)
    args = combined["args"]
    args[args.index("-MovePlan") + 1] = str(plan_arg)
    combined["forbid"] = sorted(set.intersection(*(set(j.get("forbid", [])) for _, j, _ in sources)))
    plan, slices = [], []
    for n, ((path, job, original), initial, baseline) in enumerate(zip(sources, startup, baselines)):
        prefix = f"b{n + 1}-{slug(job['name'])}--"
        reset = {k: initial.get(k, baseline.get(k)) for k in keys}
        if n:
            plan.append(dict(label=prefix + "settings-reset", kind="static", seconds=1.1, repeat_ms=0,
                             capture_at=0, serial=0, motion_start=1.1, motion_end=1.1, set=assignments(reset)))
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
        slices.append(dict(prefix=prefix, offset=offset, count=len(original), source=_repo_relative(path), job=job, plan=original))
    combined["fail_fast_slices"] = [dict(offset=s["offset"], count=s["count"],
        forbid=s["job"].get("forbid", []), expect=s["job"].get("expect", []),
        expect_every=[g for g in combined["expect_every"] if g["name"].startswith(s["prefix"])],
        fail_fast=s["job"].get("fail_fast", True), set=s["job"].get("set", ""),
        live_flow=s["job"].get("live_flow", {})) for s in slices]
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
    failures = {}
    for match in re.finditer(r"\[fail-fast\] phase=(-?\d+) slice=(\d+) check=(.*)", runtext):
        failures[int(match[2])] = dict(phase=int(match[1]), check=match[3].strip())
    # A final failed slice has no plan-complete event. Its failure timestamp
    # closes the last observed window, without manufacturing a successful end.
    if wins and failures and not math.isfinite(wins[-1]["end"]):
        clock = gates._Clock(lines[0][0] if lines else None)
        failure_end = None
        for line in runtext.splitlines():
            timestamp = clock(gates._sec(line))
            if "[fail-fast]" in line and timestamp is not None:
                failure_end = timestamp
        if failure_end is not None:
            wins[-1]["end"] = failure_end
    by_index = {w["index"]: w for w in wins}
    permitted_missing = {i for batch in manifest["slices"] if batch["offset"] in failures
                         for i in range(batch["offset"], batch["offset"] + batch["count"])}
    for batch in manifest["slices"]:
        offset = batch["offset"]
        if offset in failures and offset and plan[offset-1].get("label", "").endswith("settings-reset"):
            permitted_missing.add(offset-1)
    missing = set(range(len(plan))) - set(by_index)
    preamble_end = wins[0]["start"] if wins else float("inf")
    if (gates.plan_drift(wins, plan) or missing - permitted_missing
            or any(not math.isfinite(w['end']) for w in wins)
            or not re.search(r"^Run ended: (?:done|fail-fast)\b", runtext, re.MULTILINE)):
        raise ValueError("cannot extract: incomplete runner or plan drift")
    results = []
    for batch in manifest["slices"]:
        job, original = batch["job"], batch["plan"]
        offset, count = batch["offset"], batch["count"]
        batch_wins = [w for w in wins if offset <= w["index"] < offset + count]
        failure = failures.get(offset)
        completed = not failure and len(batch_wins) == count
        start = batch_wins[0]["start"] if batch_wins else 0
        end = batch_wins[-1]["end"] if batch_wins else start
        sliced = "\n".join(l for t, l in lines if t < preamble_end or start <= t < end) + "\n"
        kept = []
        clock = gates._Clock(lines[0][0] if lines else None)
        for line in runtext.splitlines():
            timestamp = clock(gates._sec(line))
            m = re.search(r"\[pan-plan\] window (\d+)\b|\[[\w-]+\] phase=(\d+)\b", line)
            if m:
                i = int(m[1] or m[2])
                if not offset <= i < offset + count:
                    continue
                line = re.sub(r"(\[pan-plan\] window |\[[\w-]+\] phase=)\d+", lambda x: x[1] + str(i - offset), line)
                line = line.replace("label=" + batch["prefix"], "label=")
            elif "[motion-plan] complete" in line or "Run ended:" in line:
                continue
            elif timestamp is not None:
                if not (timestamp < preamble_end or start <= timestamp < end):
                    continue
            kept.append(line)
        h, rem = divmod(end % 86400, 3600)
        mi, sec = divmod(rem, 60)
        if completed:
            kept += [f"{int(h):02}:{int(mi):02}:{sec:06.3f} [motion-plan] complete phases={count}", "Run ended: done"]
        else:
            kept.append("Run ended: fail-fast")
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
        if not completed:
            errors.insert(0, "fail-fast: " + (failure["check"] if failure else "incomplete slice"))
        for field, source in (("expect", sliced), ("expect_runner", run_slice)):
            errors += [f"missing {field}: {rx}" for rx in job.get(field, []) if not re.search(rx, source, re.IGNORECASE | re.MULTILINE)]
        errors += [f"forbidden log pattern: {rx}" for rx in job.get("forbid", []) if re.search(rx, sliced, re.IGNORECASE | re.MULTILINE)]
        post_exit = None
        if completed and run_post and job.get("post"):
            captures = re.findall(r"\[trace\].*written:.* -> (.+)", sliced)
            cmd = job["post"].replace("{log}", str(dst)).replace("{runner}", str(dst.with_suffix(".runner.txt"))).replace("{status}", str(status)).replace("{captures}", " ".join(captures))
            post = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True)
            post_exit = post.returncode
            (folder / "post.txt").write_text(post.stdout + post.stderr, encoding="utf-8")
            if job.get("post_gate") and post_exit:
                errors.append(f"post gate exit {post_exit}")
        results.append(dict(job=job["name"], valid=not errors, completed=completed, failed_check=failure["check"] if failure else None,
                            failed_phase=failure["phase"] if failure else None,
                            problems=errors, log=str(dst), post_exit=post_exit))
    flow.write(out / "results.json", results)
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jobs", nargs="*")
    ap.add_argument("--out", type=Path, default=ROOT / "tools/live_batches/combined")
    ap.add_argument("--results", type=Path, default=flow.default_results())
    ap.add_argument("--split-seconds", type=float, help="split same-save groups at source boundaries under this preflight budget")
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
        flow.pipeline_readiness.apply(job, plan)
        groups.setdefault(job["save"], []).append((path, job, plan))
    names = {j["name"] for sources in groups.values() for _, j, _ in sources}
    if any(names.intersection(j.get("requires_success_of", []))
           for sources in groups.values() for _, j, _ in sources):
        raise ValueError("cannot combine batches that depend on each other")
    index = flow.corpus(a.results, ROOT / "captures/.cache/live_flow_corpus.json", newest=100000)
    flow.fps_windows.configure(index["entries"])
    flow.configure_trace_exports(index["entries"])
    flow.readiness_budget.configure(index["entries"])
    prepared = []
    for save, sources in groups.items():
        chunks, chunk = [], []
        for source in sources:
            candidate = chunk + [source]
            trial, phases, _ = combine_group(candidate, "unused.json")
            flow.retime(trial, phases)
            budget = flow.duration_budget(trial, phases)
            if a.split_seconds and chunk and budget["seconds"] + budget["expected_readiness_seconds"] > a.split_seconds:
                chunks.append(chunk)
                chunk = [source]
            else:
                chunk = candidate
        chunks.append(chunk)
        for i, chunk in enumerate(chunks):
            suffix = f"-part{i + 1}" if len(chunks) > 1 else ""
            prepared.append((save, chunk, suffix))
    bad = False
    memo = {}
    for save, sources, suffix in prepared:
        stem = "combined-" + slug(save) + suffix
        plan_path = a.out / "plans" / (stem + ".json")
        job, plan, manifest = combine_group(sources, path_arg(plan_path))
        flow.retime(job, plan)
        job["name"] = stem
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
        problems += flow.launch_problems(job, flow.duration_budget(job, plan))
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


# Pure helper for hair_validation: concatenate disjoint phase groups into one plan.
def combine(groups):
    plan = [__import__('copy').deepcopy(step) for group in groups for step in group]
    labels = [p["label"] for p in plan]
    if not plan or len(labels) != len(set(labels)):
        raise ValueError("combined plan needs unique labels")
    if any(p.get("turn_mode") not in (None, "keys") for p in plan):
        raise ValueError("camera turns must use held Q/E")
    return plan
