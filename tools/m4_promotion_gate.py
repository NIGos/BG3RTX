"""Gate table of a lean live A/B batch (tools/live_batches/m4_promotion and older lean batches).

    py -3 -m tools.m4_promotion_gate                      # latest log of every m4_promotion job
    py -3 -m tools.m4_promotion_gate <job>.<n>.<HHMMSS>.log [...]   # given queue logs (any lean batch)
    options: --batch m4_promotion  --queue-logs <dir>  --captures-root <dir> (if the trace dirs moved)

Per log: the job's plan (tools/live_batches/*/jobs/<job>.json -> its -MovePlan in plans/) names the
phases; the runner's `[pan-plan] window i` lines time them. A periodic line (`gi: trace pass ...
(300 frames)`, `[pace] ... over 600 frames`) counts for a phase only when the whole window it averages
lies inside the phase: the previous line of the same kind is after the phase's `[cfg] settings read`
and the line itself is before the next phase starts. Nothing is tuned: the data says which lines count.

Gates printed (the coordinator decides):
- Energy: scene colour before post (dump of pass 5c3fdabc, slot 0, in each trace's dumps.json; the
  float mean, not a PNG preview and not the bloom chain: TRACK_D_LOCAL_LIGHTS 9.11). ON vs the mean of
  OFF-a/OFF-b: `within` if |ON - OFF| <= |OFF-b - OFF-a|; `brighter`/`darker` otherwise (a change that
  needs its physical explanation; darker is the energy-rule failure: the bundle only adds light where
  the old term was zero or replaces a probe by a shaded hit).
  Exposure: the lighting is pre-exposed by s1b13[0] = 0.8333 / 2^EV (TRACK_A_RR "Pre-exposure"), so the
  game's eye adaptation scales every buffer before post. Each trace's value is the `[rrpost] exposure
  probe (frame N)` line of the trace frame (`lighting s1b13 = [x ...]`); with all three present the
  verdict is on scene colour / s1b13[0] (raw change and the exposure ratio printed beside it). gi_far
  S8 2026-11-01: raw +2.87%, exposure ON / OFF 1.032, normalised -0.32% (the sign flip was the adaptation).
- Cost: every GPU line, ON minus the mean of OFF-a/OFF-b, against |OFF-b - OFF-a|.
- Participation and bias: the last diag-* window's hit material, GI hit direct/sun, rf hit shade (tier a
  ratio L_b / L_a, mean (L_b - L_a) / standard error), hit emission, instanced clip lines.
- Capacity: `[geo] RT capture` (alpha made opaque, opaque dropped) and `[cascade] casters` (buffer full)
  per frame over the run, all 0 expected.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SCENE_FS = "5c3fdabc"   # BG3 compose: lit HDR scene colour before transparents and post (RR's input)

TS = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3})\s+(.*)$")
WINDOW = re.compile(r"^(\d\d:\d\d:\d\d\.\d{3}) \[pan-plan\] window (\d+) ")
TRACE = re.compile(r"\[trace\] frame (\d+) written: .* -> (.+)$")
# layer/rrpost.cpp rrpost_on_present: the pre-exposure the lighting used (s1b13[0]), per present.
EXPOSURE = re.compile(r"\[rrpost\] exposure probe \(frame (\d+)\):.* lighting s1b13 = \[([-+0-9.eE]+)")
EXPOSURE_FRAMES = 2   # the probe reads the latest write, "a frame or two old" (rrpost.cpp)

# name -> (regex, value group, frames group or None). Frames only document the window size.
COST = {
    "frame time (pace median)": (re.compile(r"\[pace\] rendered frame time over (\d+) frames: median ([0-9.]+) ms"), 2),
    "gi: whole GI": (re.compile(r"gi: GPU cost ([0-9.]+) ms/frame \(average of (\d+) frames\)"), 1),
    "gi: trace pass": (re.compile(r"gi: trace pass \(.*\) GPU median ([0-9.]+) ms"), 1),
    "rf: reflection pass": (re.compile(r"rf: reflection pass GPU median ([0-9.]+) ms"), 1),
    "ls: local-light pass": (re.compile(r"ls: local-light shadow pass GPU median ([0-9.]+) ms"), 1),
    "rt passes": (re.compile(r"rt passes: GPU cost ([0-9.]+) ms/frame"), 1),
    "rt scene build (static)": (re.compile(r"\[rt\] scene build GPU \(split, static, \d+ samples\): .*total median ([0-9.]+) ms"), 1),
    "cascade capture pass": (re.compile(r"\[cascade\] casters: .*captured pass GPU median ([0-9.]+) ms"), 1),
}

DIAG = {
    "hit material": re.compile(r"hit material \(\d+ frames\): attempted (\d+) qualified (\d+) unsupported (\d+); reasons [^ ]+ (\d+)/(\d+)/(\d+)/(\d+)"),
    "GI hit direct": re.compile(r"GI hit direct \(\d+ frames\): candidates (\d+) qualified (\d+) direct supported (\d+) nonzero (\d+); added weighted luminance sum ([0-9.]+)"),
    "GI hit sun": re.compile(r"GI hit sun \(\d+ frames\): evaluated (\d+), .*traced lit (\d+), traced occluded (\d+), roof-unsupported [^)]*\) (\d+).*added weighted luminance sum ([0-9.]+)"),
    "rf hit shade": re.compile(r"rf hit shade \(\d+ frames\): tier b hidden hits (\d+), shaded (\d+) \(([0-9.]+)%\).*tier a check: (\d+) hits .*mean L_b ([0-9.]+), mean L_a ([0-9.]+), mean \(L_b - L_a\) per frame (-?[0-9.]+), standard error ([0-9.]+) \((\d+) frames\)"),
    "rf hit shade terms": re.compile(r"rf hit shade terms .*AO at the hit ([0-9.]+) vs the screen's AO there ([0-9.]+);.*ratio L_b / L_a ([0-9.]+) \(at AO 1 ([0-9.]+)\)"),
    "rf hit shade boxes": re.compile(r"rf hit shade boxes \(\d+ frames\): tier a L_b projector lights ([0-9.]+) of L_b ([0-9.]+); tier b shaded L_b projector lights ([0-9.]+) of ([0-9.]+)"),
    "hit emission": re.compile(r"hit emission \(\d+ frames\): GI hidden hits: lookups (\d+), emissive (\d+), skipped (\d+), added weighted luminance ([0-9.]+);.*rf tier b: lookups (\d+), emissive (\d+), skipped (\d+), added luminance ([0-9.]+);.*approximate uncalibrated (\d+)"),
    "instanced clip": re.compile(r"hit material instanced clip \(\d+ frames\): .*rejected (\d+) = ([0-9.]+)% of all lookups"),
    "instanced rows": re.compile(r"\[hit material instanced\] frame \d+ key (\d); wind: .*mapped (\d+) rows (\d+) triangles; alpha-test 2S: .*mapped (\d+) rows (\d+) triangles"),
}

CAPACITY = [
    ("alpha made opaque /frame", re.compile(r"\[geo\] RT capture, .*alpha draws made opaque \(region full\) ([0-9.]+)")),
    ("opaque dropped /frame", re.compile(r"\[geo\] RT capture, .*opaque draws dropped \(buffer full\) ([0-9.]+)")),
    ("cascade buffer full /frame", re.compile(r"\[cascade\] casters: .*buffer full ([0-9.]+)")),
]
CAPACITY_PEAK = re.compile(r"\[geo\] RT capture, .*alpha region ([0-9.]+) MB \(peak demand ([0-9.]+) MB\), opaque peak ([0-9.]+) MB of a ([0-9.]+) MB buffer")


def secs(hms: str) -> float:
    h, m, s = hms.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def unwrap(times):
    """Clock times of one run, made monotonic across midnight."""
    out, day, last = [], 0.0, None
    for t in times:
        if last is not None and t + day < last - 43200:
            day += 86400
        out.append(t + day)
        last = t + day
    return out


def main_checkout() -> Path:
    parts = REPO.parts
    for i in range(len(parts) - 1):
        if parts[i] == ".claude" and parts[i + 1] == "worktrees":
            return Path(*parts[:i])
    return REPO


def find_job(name: str):
    for f in sorted((REPO / "tools" / "live_batches").glob(f"*/jobs/{name}.json")):
        job = json.loads(f.read_text(encoding="utf-8"))
        args = job.get("args", [])
        plan_rel = args[args.index("-MovePlan") + 1] if "-MovePlan" in args else None
        if plan_rel:
            plan = f.parent.parent / "plans" / Path(plan_rel).name
            if plan.exists():
                return job, json.loads(plan.read_text(encoding="utf-8"))
    return None, None


class Run:
    def __init__(self, log: Path, captures_root: Path | None = None, plan: Path | None = None):
        self.log = log
        self.name = log.name.split(".")[0]
        if plan:
            self.job, self.plan = {"save": "?"}, json.loads(plan.read_text(encoding="utf-8"))
        else:
            self.job, self.plan = find_job(self.name)
        if not self.plan:
            raise SystemExit(f"{log.name}: no job/plan {self.name} under tools/live_batches/*/jobs")
        self.labels = [p["label"] for p in self.plan]
        runner = log.with_suffix(".runner.txt")
        starts = {}
        for line in runner.read_text(encoding="utf-8", errors="replace").splitlines() if runner.exists() else []:
            m = WINDOW.match(line)
            if m:
                starts.setdefault(int(m.group(2)), secs(m.group(1)))
        if len(starts) != len(self.plan):
            print(f"warning: {log.name}: {len(starts)} of {len(self.plan)} phases started (incomplete run?)")
        raw = []
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            m = TS.match(line)
            if m:
                raw.append((int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000, m.group(5)))
        t = unwrap([r[0] for r in raw])
        self.lines = [(t[i], raw[i][1]) for i in range(len(raw))]
        base = self.lines[0][0] if self.lines else 0.0
        self.phases = []   # (label, effective start, end)
        idx = sorted(starts)
        for i in idx:   # the runner's clock on the log's (unwrapped) day
            while starts[i] < base - 43200:
                starts[i] += 86400
        for k, i in enumerate(idx):
            s = starts[i]
            end = (starts[idx[k + 1]] if k + 1 < len(idx) else s + float(self.plan[i]["seconds"]))
            cfg = next((tt for tt, txt in self.lines if s <= tt <= s + 3 and "[cfg] settings read" in txt), s)
            self.phases.append((self.labels[i], cfg, end))
        self.captures_root = captures_root

    def phase_of(self, label):
        return next((p for p in self.phases if p[0] == label), None)

    def cost(self, label):
        p = self.phase_of(label)
        out = {}
        if not p:
            return out
        _, s, e = p
        for name, (rx, g) in COST.items():
            prev, vals = None, []
            for t, txt in self.lines:
                m = rx.search(txt)
                if not m:
                    continue
                if prev is not None and prev >= s and t <= e:
                    vals.append(float(m.group(g)))
                prev = t
            if vals:
                out[name] = (statistics.median(vals), len(vals))
        return out

    def last_in(self, label, rx):
        p = self.phase_of(label)
        if not p:
            return None
        hit = None
        for t, txt in self.lines:
            if p[1] <= t <= p[2] + 1.0:
                m = rx.search(txt)
                if m:
                    hit = m
        return hit

    def pre_exposure(self, frame: int):
        """s1b13[0] of the probe line at `frame`, else the nearest within EXPOSURE_FRAMES; None if absent."""
        best = None
        for _, txt in self.lines:
            m = EXPOSURE.search(txt)
            if m:
                df = abs(int(m.group(1)) - frame)
                if df <= EXPOSURE_FRAMES and (best is None or df < best[0]):
                    best = (df, float(m.group(2)))
        return best[1] if best and best[1] > 0 else None

    def scene_colour(self, label, fs=SCENE_FS):
        """(luminance, nonfinite dumps, pre-exposure or None) of the phase's trace (last dump of pass fs, slot 0)."""
        p = self.phase_of(label)
        if not p:
            return None
        for t, txt in self.lines:
            if p[1] <= t <= p[2] + 2.0:
                m = TRACE.search(txt)
                if m:
                    frame = int(m.group(1))
                    cap = Path(m.group(2).strip())
                    if self.captures_root:
                        cap = self.captures_root / cap.name
                    f = cap / "dumps.json"
                    if not f.exists():
                        return ("missing", str(f))
                    dumps = json.loads(f.read_text(encoding="utf-8"))
                    cand = [(int(k[1:]), d) for k, d in dumps.items()
                            if str(d.get("pass_fs", "")).startswith(fs) and d.get("slot") == 0 and d.get("mean")]
                    if not cand:
                        return ("no scene-colour dump", str(f))
                    _, d = max(cand, key=lambda c: c[0])
                    r, g, b = d["mean"][:3]
                    nonfinite = sum(int(x.get("nonfinite", 0) or 0) for x in dumps.values())
                    return (0.2126 * r + 0.7152 * g + 0.0722 * b, nonfinite, self.pre_exposure(frame))
        return None

    def capacity(self):
        # From the first phase on: the adaptive alpha partition starts at its 64 MB floor and the first
        # window after the load overflows in every run (with or without this bundle).
        t0 = self.phases[0][1] if self.phases else 0.0
        lines = [txt for t, txt in self.lines if t >= t0]
        out = []
        for name, rx in CAPACITY:
            vals = [float(m.group(1)) for txt in lines for m in [rx.search(txt)] if m]
            out.append((name, max(vals) if vals else None, len(vals)))
        peaks = [m for txt in lines for m in [CAPACITY_PEAK.search(txt)] if m]
        if peaks:
            out.append(("capture peak alpha / opaque of buffer MB",
                        "%.0f / %.0f of %s" % (max(float(m.group(2)) for m in peaks), max(float(m.group(3)) for m in peaks), peaks[-1].group(4)), len(peaks)))
        return out


def triples(labels, prefix):
    """(off-a, on, off-b) label triples: <prefix>-off-a/-off-b against every other <prefix>-* label;
    plus the S6 emission triple <prefix>-em-off-a / <prefix>-on / <prefix>-em-off-b."""
    out = []
    a, b = f"{prefix}-off-a", f"{prefix}-off-b"
    if a in labels and b in labels:
        for l in labels:
            if l.startswith(prefix + "-") and l not in (a, b) and "-em-off-" not in l:
                out.append((a, l, b))
    ea, eb = f"{prefix}-em-off-a", f"{prefix}-em-off-b"
    if ea in labels and eb in labels and f"{prefix}-on" in labels:
        out.append((ea, f"{prefix}-on", eb))
    return out


def energy_verdict(d, s):
    """d: relative change of ON against the OFF mean; s: relative OFF-a/OFF-b spread."""
    return "within" if abs(d) <= s else ("brighter: explain" if d > 0 else "DARKER: energy rule")


def energy(run):
    out = []
    for a, on, b in triples(run.labels, "shot"):
        vals = [run.scene_colour(x) for x in (a, on, b)]
        if any(not v or isinstance(v[0], str) for v in vals):
            out.append({"a": a, "on": on, "b": b, "values": vals, "verdict": "INCOMPLETE"})
            continue
        va, vo, vb = (v[0] for v in vals)
        off = (va + vb) / 2
        raw_d, raw_s = (vo - off) / off, abs(vb - va) / off
        pre = [v[2] if len(v) > 2 else None for v in vals]
        e = {"a": a, "on": on, "b": b, "values": (va, vo, vb), "raw_d": raw_d, "raw_s": raw_s,
             "nonfinite": sum(v[1] for v in vals), "pre": pre, "exposure": None}
        if all(pre):
            # Verdict on scene colour / pre-exposure: the eye adaptation scales the whole lit image.
            na, no, nb = va / pre[0], vo / pre[1], vb / pre[2]
            noff = (na + nb) / 2
            e["d"], e["s"] = (no - noff) / noff, abs(nb - na) / noff
            e["exposure"] = pre[1] / ((pre[0] + pre[2]) / 2)
        else:
            e["d"], e["s"] = raw_d, raw_s
        e["verdict"] = energy_verdict(e["d"], e["s"])   # on the raw change when an exposure is unknown
        out.append(e)
    return out


DIFFUSE_FS = "d1669194"   # the SSS pass: the diffuse light after our composite (RR's diffuse input side)
GIVEBACK = re.compile(r"ls stats \(\d+ frames\): ao_direct \d gives back ([0-9.]+) per px \(([0-9.]+)% of the lit luminance\)")


def stages(run):
    """gi_far stage windows (tools/live_batches/gi_far README): view-<x>-off / view-<x>-on pairs (gi_debug views:
    the diffuse buffer's float mean, ON / OFF), and diag-<x>-off / diag-<x>-on ls stats give-back lines."""
    out = []
    for l in run.labels:
        if l.startswith("view-") and l.endswith("-off") and l[:-4] + "-on" in run.labels:
            off, on = (run.scene_colour(x, DIFFUSE_FS) for x in (l, l[:-4] + "-on"))
            ok = all(v and not isinstance(v[0], str) for v in (off, on))
            out.append(("view", l[5:-4], off[0] if ok else None, on[0] if ok else None))
        if l.startswith("diag-") and l.endswith("-off") and l[:-4] + "-on" in run.labels:
            off, on = (run.last_in(x, GIVEBACK) for x in (l, l[:-4] + "-on"))
            out.append(("giveback", l[5:-4], float(off.group(1)) if off else None, float(on.group(1)) if on else None))
    return out


def fmt(x, nd=3):
    return "n/a" if x is None else f"{x:.{nd}f}"


def report(run: Run):
    print(f"\n## {run.name} ({run.job.get('save', '?')}) - {run.log.name}")
    print(f"phases {len(run.phases)}/{len(run.plan)}: " + ", ".join(p[0] for p in run.phases))

    print("\n### Energy: scene colour before post (5c3fdabc float mean luminance) / pre-exposure s1b13[0]")
    print("| comparison | OFF-a | ON | OFF-b | raw ON vs OFF | exposure ON / OFF | ON vs mean OFF | OFF spread | verdict |")
    print("|---|---|---|---|---|---|---|---|---|")
    for e in energy(run):
        if e["verdict"] == "INCOMPLETE":
            print(f"| {e['on']} | {e['values']} | | | | | | | INCOMPLETE |")
            continue
        va, vo, vb = e["values"]
        ex = "unknown (raw verdict)" if e["exposure"] is None else f"{e['exposure']:.4f}"
        print(f"| {e['on']} vs {e['a']}/{e['b']} | {va:.5f} | {vo:.5f} | {vb:.5f} | {100 * e['raw_d']:+.2f}% | {ex} | "
              f"{100 * e['d']:+.2f}% | {100 * e['s']:.2f}% | "
              f"{e['verdict']}{'' if e['nonfinite'] == 0 else ', nonfinite %d' % e['nonfinite']} |")

    st = stages(run)
    if st:
        print("\n### Stages (view: diffuse after SSS, d1669194 float mean; giveback: ls stats ao_direct per px)")
        print("| stage | OFF | ON | ON / OFF |")
        print("|---|---|---|---|")
        for kind, name, off, on in st:
            ratio = f"{on / off:.4f}" if off and on is not None else "n/a"
            print(f"| {kind} {name} | {fmt(off, 5)} | {fmt(on, 5)} | {ratio} |")

    print("\n### Cost (GPU ms, medians of the windows fully inside each phase; n = windows)")
    for a, on, b in triples(run.labels, "cost"):
        ca, co, cb = run.cost(a), run.cost(on), run.cost(b)
        print(f"\n{on} vs {a}/{b}")
        print("| line | OFF-a | ON | OFF-b | ON - mean OFF | OFF spread | n |")
        print("|---|---|---|---|---|---|---|")
        for name in COST:
            if name not in co and name not in ca:
                continue
            xa, xo, xb = (c.get(name, (None, 0)) for c in (ca, co, cb))
            if None in (xa[0], xo[0], xb[0]):
                print(f"| {name} | {fmt(xa[0])} | {fmt(xo[0])} | {fmt(xb[0])} | | | {xa[1]}/{xo[1]}/{xb[1]} |")
                continue
            off = (xa[0] + xb[0]) / 2
            print(f"| {name} | {xa[0]:.3f} | {xo[0]:.3f} | {xb[0]:.3f} | {xo[0] - off:+.3f} | {abs(xb[0] - xa[0]):.3f} | {xa[1]}/{xo[1]}/{xb[1]} |")
    extra = [l for l in run.labels if l.startswith("cost-") and not any(l in t for t in triples(run.labels, "cost"))]
    for l in extra:
        c = run.cost(l)
        print(f"\n{l}: " + ", ".join(f"{k} {v[0]:.3f} (n {v[1]})" for k, v in c.items()))

    print("\n### Participation and bias (last diag-* window)")
    diag = [l for l in run.labels if l.startswith("diag-")]
    for label in diag:
        print(f"\n{label}:")
        m = run.last_in(label, DIAG["hit material"])
        if m:
            att, q = int(m.group(1)), int(m.group(2))
            print(f"- hit material: qualified {q} of {att} attempted = {100 * q / max(att, 1):.1f}%; base {m.group(4)}, row {m.group(6)}, clip {m.group(7)}")
        m = run.last_in(label, DIAG["GI hit direct"])
        if m:
            print(f"- GI hit direct: candidates {m.group(1)}, qualified {m.group(2)}, supported {m.group(3)}, nonzero {m.group(4)}, added {m.group(5)}")
        m = run.last_in(label, DIAG["GI hit sun"])
        if m:
            print(f"- GI hit sun: evaluated {m.group(1)}, lit {m.group(2)}, occluded {m.group(3)}, roof-unsupported {m.group(4)}, added {m.group(5)}")
        m = run.last_in(label, DIAG["rf hit shade"])
        if m:
            d, se, n = float(m.group(7)), float(m.group(8)), int(m.group(9))
            z = d / se if se > 0 else float("inf")
            print(f"- rf hit shade: tier b shaded {m.group(3)}% of {m.group(1)}; tier a {m.group(4)} hits, L_b {m.group(5)} vs L_a {m.group(6)}, "
                  f"mean (L_b - L_a) {d:+.5f} = {z:+.1f} standard errors ({n} frames)")
        m = run.last_in(label, DIAG["rf hit shade terms"])
        if m:
            print(f"- rf hit shade terms: ratio L_b / L_a {m.group(3)} (AO 1: {m.group(4)}); AO at hit {m.group(1)} vs screen {m.group(2)}")
        m = run.last_in(label, DIAG["rf hit shade boxes"])
        if m:
            print(f"- rf hit shade boxes: tier a projector part {m.group(1)} of L_b {m.group(2)}; tier b shaded {m.group(3)} of {m.group(4)}")
        m = run.last_in(label, DIAG["hit emission"])
        if m:
            print(f"- hit emission: GI hidden lookups {m.group(1)} emissive {m.group(2)} added {m.group(4)}; rf tier b lookups {m.group(5)} "
                  f"emissive {m.group(6)} added {m.group(8)}; approximate uncalibrated skipped {m.group(9)}")
        m = run.last_in(label, DIAG["instanced rows"])
        if m:
            print(f"- instanced rows (key {m.group(1)}): wind {m.group(2)} rows {m.group(3)} tri, 2S {m.group(4)} rows {m.group(5)} tri")
        m = run.last_in(label, DIAG["instanced clip"])
        if m:
            print(f"- instanced clip: rejected {m.group(1)} = {m.group(2)}% of lookups (gate < 0.04%)")

    print("\n### Capacity over the run (all 0 expected)")
    for name, v, n in run.capacity():
        print(f"- {name}: {v if isinstance(v, str) else fmt(v, 2)} (max of {n} lines)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--batch", default="m4_promotion")
    ap.add_argument("--queue-logs", type=Path, default=main_checkout() / "captures" / "queue" / "logs")
    ap.add_argument("--captures-root", type=Path, default=None)
    ap.add_argument("--plan", type=Path, default=None, help="plan json (default: from the job of the log's name)")
    a = ap.parse_args(argv)
    logs = list(a.logs)
    if not logs:
        for f in sorted((REPO / "tools" / "live_batches" / a.batch / "jobs").glob("*.json")):
            name = f.stem
            found = sorted(a.queue_logs.glob(f"{name}.*.log"), key=lambda p: p.stat().st_mtime)
            if found:
                logs.append(found[-1])
            else:
                print(f"{name}: no log in {a.queue_logs}")
    if not logs:
        return 1
    for log in logs:
        report(Run(log, a.captures_root, a.plan))
    return 0


if __name__ == "__main__":
    sys.exit(main())
