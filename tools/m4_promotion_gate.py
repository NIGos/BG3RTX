"""Gate table of a lean live A/B batch (tools/live_batches/m4_promotion and older lean batches).

    py -3 -m tools.m4_promotion_gate                      # latest log of every m4_promotion job
    py -3 -m tools.m4_promotion_gate <job>.<n>.<HHMMSS>.log [...]   # given queue logs (any lean batch)
    options: --batch m4_promotion  --queue-logs <dir>  --captures-root <dir> (if the trace dirs moved)
             --pool run (OFF noise from the run's own OFF shots only; default: every lean run of the save)

Exit 1 when any gate fails (energy, plan drift, capacity), 0 when all hold.

Per log: the job's plan (tools/live_batches/*/jobs/<job>.json -> its -MovePlan in plans/) names the
phases; the runner's `[pan-plan] window i ... set=<keys>` lines time them. When the plan file changed
after the run (the runner's set= differs from the plan step), the plan version from git history whose
sets match is used; if none matches, the run fails with PLAN DRIFT (labels would be one phase off:
m4p3 / m4p4 11:2x ran before cost-ao0 was inserted). A periodic line (`gi: trace pass ... (300 frames)`,
`[pace] ... over 600 frames`) counts for a phase only when the whole window it averages lies inside the
phase: the previous line of the same kind is after the phase's `[cfg] settings read` and the line itself
is before the next phase starts. Nothing is tuned: the data says which lines count.

Gates:
- Energy: scene colour before post (dump of pass 5c3fdabc, slot 0, in each trace's dumps.json, or the job's
  "energy_fs": "water-compose" for water jobs, 5c3fdabc plus the in-frame compose delta (WATER_COMPOSE); the
  float mean, not a PNG preview and not the bloom chain: TRACK_D_LOCAL_LIGHTS 9.11). Each ON shot is
  compared with the mean of the nearest OFF shot before and after it (shot-off-* family; S6 also the
  shot-em-off-* family for shot-on). d = (ON - OFF mean) / OFF mean. Two-sided, against a PREDICTED
  delta p written before the run (tools/live_batches/<batch>/predicted.json; no entry: p = 0):
      pass  |d - p| <= t(1 - ALPHA_FW / 2m, dof) * sigma * sqrt(3/2)
  sigma: the relative std of one shot's scene-colour mean at a static view, from consecutive OFF-OFF
  pairs (sigma^2 = sum d_ij^2 / 2n over n pairs, dof n) of every lean run on the same save (and the
  run's own); m: the run's comparisons (Bonferroni); sqrt(3/2): ON minus the mean of two OFFs. The old
  rule |d| <= |OFF-b - OFF-a| is printed for reference only: a one-sample range, it flags 46% of pure
  noise (P(|N(0, 1.5)| > |N(0, 2)|)) and lsk1 071324's own OFF repeats fail it (+0.61%, -0.28%), while a
  one-sided "brighter: explain" passed m5w4's +2.2% for a key that only removes light. A prediction
  committed after the run started is marked "post hoc" and does not pass.
  Exposure first: the lighting is pre-exposed by s1b13[0] = 0.8333 / 2^EV (TRACK_A_RR "Pre-exposure"), so
  the game's eye adaptation scales every buffer before post. Each trace's value is the `[rrpost] exposure
  probe (frame N)` line of the trace frame (`lighting s1b13 = [x ...]`, within EXPOSURE_FRAMES); d and the
  OFF-OFF noise pairs use scene colour / s1b13[0] when every shot of the comparison has it, else the raw
  value (marked). The raw change and the exposure ratio ON / OFF are printed beside it. gi_far S8
  2026-11-01: raw +2.87%, exposure ON / OFF 1.032, normalised -0.32% (the sign flip was the adaptation).
  Jobs with an energy gate hold the adaptation (`qa_exposure_hold=1` in every shot-* phase;
  scripts/queue_gates.py exposure_hold_problems), so the correction is a check, not the measurement.
- Cost: every GPU line, ON minus the mean of OFF-a/OFF-b, against |OFF-b - OFF-a| (reported, not gated).
- Participation and bias: the last diag-* window's hit material, GI hit direct/sun, rf hit shade (tier a
  ratio L_b / L_a, mean (L_b - L_a) / standard error), hit emission, instanced clip lines (reported).
- Capacity: `[geo] RT capture` (alpha made opaque, opaque dropped) and `[cascade] casters` (buffer full)
  per frame over the run: any value above 0 fails.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import math
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

# Prefer this checkout even when another environment provides a tools package.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.longrun import FileCache, Progress

REPO = Path(__file__).resolve().parents[1]
SCENE_FS = "5c3fdabc"   # BG3 compose: lit HDR scene colour before transparents and post (RR's input)
# The first pass after the forward water / transparents pass on the same HDR scene image (frame.json order:
# 5c3fdabc, the water pass, a copy, then this pass): the first dump that holds the water compose's writes, before
# post. It sees the compose (m5p1 213849 water sun shadows -10% here, 5c3fdabc unchanged), but its mean also carries
# the water FS's sun glints (below): prefer WATER_COMPOSE for water jobs. Jobs select a dump with "energy_fs".
WATER_AFTER_FS = "423e0548"
# "energy_fs": "water-compose": the scene colour before the water (SCENE_FS) plus what the water compose changed
# IN THE SAME FRAME: for each pass with an audited water FS (common/water_policy.h), the mean of the next dump of the
# scene image (after core on_pass_end's compose) minus the water pass's own dump (taken before it: layer/cmd.cpp
# end_pass dumps, then calls the core). The water FS's own output cancels: its sun glints (dump max 640..1584 on S8,
# one 1000 px moves the frame mean ~0.8%) made the plain after-water mean swing 2.5% between keys that change no
# water pixel (m5p1 off -> rt_water_reflections measured: +2.5%), and m5w3's "compose -2%" was that noise.
# New traces provide an explicit post_core readback paired with the same pass, since the image may never be
# attached again. Only legacy traces use the following attachment dump; missing readbacks remain invalid.
WATER_COMPOSE = "water-compose"
_WATER_FS = None


def water_fs_prefixes():
    """The audited water FS (first 8 hex digits) from common/water_policy.h."""
    global _WATER_FS
    if _WATER_FS is None:
        src = (REPO / "common" / "water_policy.h").read_text(encoding="utf-8")
        _WATER_FS = {m.group(1) for m in re.finditer(r"\{0x([0-9a-f]{8})[0-9a-f]{8}ull, 0x[0-9a-f]{16}ull, \"FLUID", src)}
    return _WATER_FS


def water_compose_colour(cap: Path, dumps: dict):
    """(luminance of SCENE_FS + sum over water passes of (post-core scene dump - the water pass's dump), nonfinite of
    those dumps) or (reason, path). Pass order from frame.json (pass, draws, end with its dump ids)."""
    lum = lambda d: 0.2126 * d["mean"][0] + 0.7152 * d["mean"][1] + 0.0722 * d["mean"][2]  # noqa: E731
    scene = [(int(k[1:]), d) for k, d in dumps.items()
             if str(d.get("pass_fs", "")).startswith(SCENE_FS) and d.get("slot") == 0 and d.get("mean")]
    fj = cap / "frame.json"
    if not scene or not fj.exists():
        return ("no scene-colour dump or frame.json", str(cap))
    s = max(scene, key=lambda c: c[0])[1]
    img = s["image"]
    water = water_fs_prefixes()
    frame = json.loads(fj.read_text(encoding="utf-8"))
    ends = []   # (water pass?, pre-core dump, explicit post-core dump or None), in order
    fs_seen = set()
    for sub in frame.get("submits", []):
        for c in sub.get("cmds", []):
            for e in c.get("events", []):
                op = e.get("op")
                if op == "pass":
                    fs_seen = set()
                elif op == "draw" and e.get("fs"):
                    fs_seen.add(e["fs"][:8])
                elif op == "end":
                    hit = [dumps[k] for k in e.get("dumps", []) if k in dumps and dumps[k].get("image") == img and
                           dumps[k].get("slot") == 0 and dumps[k].get("mean")]
                    ends.append((bool(fs_seen & water), hit[0] if hit else None, None))
                elif op == "post_core" and ends:
                    hit = [dumps[k] for k in e.get("dumps", []) if k in dumps and dumps[k].get("image") == img and
                           dumps[k].get("slot") == 0 and dumps[k].get("mean")]
                    is_w, before, _ = ends[-1]
                    if is_w and before is not None and len(hit) != 1:
                        return ("missing or ambiguous post-core water scene dump", str(cap))
                    ends[-1] = (is_w, before, hit[0] if hit else None)
    delta, nf, n = 0.0, int(s.get("nonfinite", 0) or 0), 0
    for i, (is_w, d, post) in enumerate(ends):
        if not is_w or d is None:
            continue
        if post is None:
            nxt = next((x for x in ends[i + 1:] if x[1] is not None), None)
            if nxt is None or nxt[0]:
                return ("water pass without a non-water scene dump after it", str(cap))
            post = nxt[1]  # legacy traces that contain the required following attachment
        delta += lum(post) - lum(d)
        nf += int(d.get("nonfinite", 0) or 0) + int(post.get("nonfinite", 0) or 0)
        n += 1
    if not n:
        return ("no water pass on the scene image", str(cap))
    return (lum(s) + delta, nf)
ALPHA_FW = 0.05         # scripts/qa/thresholds.py: 5% family-wise false alarm, Bonferroni per family

TS = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3})\s+(.*)$")
WINDOW = re.compile(r"^(\d\d:\d\d:\d\d\.\d{3}) \[pan-plan\] window (\d+) ")
WINDOW_SET = re.compile(r" set=(\S*)")
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
    "rf hit gi": re.compile(r"rf hit gi \(\d+ frames, (\d+) tier a hits, (\d+) of them from the screen's GI\): tier a GI term ([0-9.]+) vs the screen's GI term there ([0-9.]+); ratio L_b / L_a ([0-9.]+) \(without the GI term ([0-9.]+)\); tier b shaded GI term ([0-9.]+); AO-ray hits (\d+), screen source (\d+) \(([0-9.]+)%\), cache source (\d+)"),
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


def t_quantile(p, dof):
    """Student t quantile (scripts/qa/em_gate.py: exact for dof 1 and 2, Cornish-Fisher above)."""
    from statistics import NormalDist
    if dof <= 1.0:
        return math.tan(math.pi * (p - 0.5))
    if dof <= 2.0:
        return (2 * p - 1) / math.sqrt(2 * p * (1 - p))
    z = NormalDist().inv_cdf(p)
    v = dof
    return (z + (z ** 3 + z) / (4 * v) + (5 * z ** 5 + 16 * z ** 3 + 3 * z) / (96 * v ** 2)
            + (3 * z ** 7 + 19 * z ** 5 + 17 * z ** 3 - 15 * z) / (384 * v ** 3))


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


def find_job_file(name: str):
    # Nested batches too (m4_promotion/s6_diag: the S6 diagnostic and emission rerun, its own predicted.json).
    root = REPO / "tools" / "live_batches"
    for f in sorted(root.glob(f"*/jobs/{name}.json")) + sorted(root.glob(f"*/*/jobs/{name}.json")):
        return f
    return None


def plan_path(job_file: Path, job: dict):
    args = job.get("args", [])
    if "-MovePlan" not in args:
        return None
    p = job_file.parent.parent / "plans" / Path(args[args.index("-MovePlan") + 1]).name
    return p if p.exists() else None


def find_job(name: str):
    f = find_job_file(name)
    if f:
        job = json.loads(f.read_text(encoding="utf-8-sig"))
        p = plan_path(f, job)
        if p:
            return job, json.loads(p.read_text(encoding="utf-8-sig"))
    return None, None


def _git_run(*args):
    try:
        r = subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
        return r.stdout if r.returncode == 0 else ""
    except OSError:
        return ""


_GIT_MEMO = {}
_GIT_DISK = None


def _git(*args):
    """git output, memoised in the process; `show <sha>:<path>` (immutable) and `log -- <path>` (keyed by
    HEAD) also across runs in captures/.cache/m4_git.json (tools/longrun.py). 144 git calls were 38 of
    the gate's 42 s (2026-11-01)."""
    global _GIT_DISK
    if args in _GIT_MEMO:
        return _GIT_MEMO[args]
    disk_sig = None
    if args[0] == "show" and re.fullmatch(r"[0-9a-f]{40}:.+", args[1]):
        disk_sig = ["immutable"]
    elif args[0] == "log" and args[1] == "--format=%H":
        head = _GIT_MEMO.get(("rev-parse", "HEAD"))
        if head is None:
            head = _GIT_MEMO[("rev-parse", "HEAD")] = _git_run("rev-parse", "HEAD").strip()
        disk_sig = [head] if head else None
    if disk_sig is not None:
        if _GIT_DISK is None:
            _GIT_DISK = FileCache("m4_git")
        key = " | ".join(args)
        out = _GIT_DISK.get(key, disk_sig)
        if out is None:
            out = _git_run(*args)
            if out:
                _GIT_DISK.put(key, disk_sig, out)
                _GIT_DISK.save()
    else:
        out = _git_run(*args)
    _GIT_MEMO[args] = out
    return out


def plan_matches(plan, sets):
    """True when every runner window's set= equals the plan step's set (windows without set= pass)."""
    for i, s in sets.items():
        if s is None:
            continue
        if not (0 <= i < len(plan)) or str(plan[i].get("set", "")) != s:
            return False
    return True


def plan_from_history(path: Path, sets):
    """(plan, commit) of the newest committed version of `path` whose sets match the runner's."""
    rel = path.resolve().relative_to(REPO.resolve()).as_posix()
    for h in _git("log", "--format=%H", "--", rel).split():
        txt = _git("show", f"{h}:{rel}")
        try:
            plan = json.loads(txt.lstrip("\ufeff"))
        except ValueError:
            continue
        if plan_matches(plan, sets):
            return plan, h[:7]
    return None, None


class Run:
    def __init__(self, log: Path, captures_root: Path | None = None, plan: Path | None = None):
        self.log = log
        self.name = log.name.split(".")[0]
        self.batch = None
        self.drift = None      # None, "plan <commit> from history", or "PLAN DRIFT ..."
        runner = log.with_suffix(".runner.txt")
        starts, sets = {}, {}
        for line in runner.read_text(encoding="utf-8", errors="replace").splitlines() if runner.exists() else []:
            m = WINDOW.match(line)
            if m:
                i = int(m.group(2))
                starts.setdefault(i, secs(m.group(1)))
                sm = WINDOW_SET.search(line)
                sets.setdefault(i, sm.group(1) if sm else None)
        if plan:
            self.job, self.plan = {"save": "?"}, json.loads(plan.read_text(encoding="utf-8-sig"))
        else:
            self.job, self.plan = find_job(self.name)
            jf = find_job_file(self.name)
            if jf:
                self.batch = jf.parent.parent
            if self.plan and not plan_matches(self.plan, sets):
                old, h = plan_from_history(plan_path(jf, self.job), sets)
                if old:
                    self.plan, self.drift = old, f"plan from {h} (the plan file changed after this run)"
                else:
                    self.drift = "PLAN DRIFT: the runner's window keys match no committed version of the plan"
        if not self.plan:
            raise SystemExit(f"{log.name}: no job/plan {self.name} under tools/live_batches/*/jobs")
        if self.drift is None and plan and not plan_matches(self.plan, sets):
            self.drift = "PLAN DRIFT: the runner's window keys differ from the given plan"
        self.labels = [p["label"] for p in self.plan]
        # The energy dump: the job's "energy_fs" (a pass FS prefix) or SCENE_FS. Water jobs need the scene colour
        # AFTER the forward water pass (WATER_AFTER_FS): SCENE_FS runs before it and does not see the water compose.
        self.energy_fs = str(self.job.get("energy_fs") or SCENE_FS)
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
        idx = sorted(i for i in starts if 0 <= i < len(self.plan))
        for i in idx:   # the runner's clock on the log's (unwrapped) day
            while starts[i] < base - 43200:
                starts[i] += 86400
        for k, i in enumerate(idx):
            s = starts[i]
            end = (starts[idx[k + 1]] if k + 1 < len(idx) else s + float(self.plan[i]["seconds"]))
            cfg = next((tt for tt, txt in self.lines if s <= tt <= s + 3 and "[cfg] settings read" in txt), s)
            self.phases.append((self.labels[i], cfg, end))
        self.captures_root = captures_root

    def failed_drift(self):
        return bool(self.drift and self.drift.startswith("PLAN DRIFT"))

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
        if not hasattr(self, "_probes"):
            self._probes = [(int(m.group(1)), float(m.group(2))) for _, txt in self.lines for m in [EXPOSURE.search(txt)] if m]
        best = None
        for f, v in self._probes:
            df = abs(f - frame)
            if df <= EXPOSURE_FRAMES and (best is None or df < best[0]):
                best = (df, v)
        return best[1] if best and best[1] > 0 else None

    def scene_colour(self, label, fs=None):
        """(luminance, nonfinite px of that same dump, pre-exposure s1b13[0] or None) of the phase's trace,
        or (reason, path), or None. fs: the dump's pass FS prefix (default the run's energy_fs)."""
        fs = fs or self.energy_fs
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
                    if fs == WATER_COMPOSE:
                        v = water_compose_colour(cap, dumps)
                        if isinstance(v[0], str):
                            return v
                        other_nf = {f"{x.get('pass_fs', '?')[:8]} {x.get('fmt', '?')}": int(x.get("nonfinite", 0) or 0)
                                    for x in dumps.values() if int(x.get("nonfinite", 0) or 0)}
                        return (v[0], v[1], self.pre_exposure(frame), other_nf)
                    cand = [(int(k[1:]), d) for k, d in dumps.items()
                            if str(d.get("pass_fs", "")).startswith(fs) and d.get("slot") == 0 and d.get("mean")]
                    if not cand:
                        return ("no scene-colour dump", str(f))
                    _, d = max(cand, key=lambda c: c[0])
                    r, g, b = d["mean"][:3]
                    # Non-finite values of the measured buffer itself, and of every other dump of the trace
                    # (S6: the game's own D32F depth of 76433aef, ~2900 per trace with every RT key off too;
                    # docs/QA.md "Non-finite"): never summed together, never summed across traces.
                    scene_nf = int(d.get("nonfinite", 0) or 0)
                    other_nf = {f"{x.get('pass_fs', '?')[:8]} {x.get('fmt', '?')}": int(x.get("nonfinite", 0) or 0)
                                for x in dumps.values() if x is not d and int(x.get("nonfinite", 0) or 0)}
                    return (0.2126 * r + 0.7152 * g + 0.0722 * b, scene_nf, self.pre_exposure(frame), other_nf)
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

    def started(self) -> float:
        """Epoch seconds of the run's start: the HHMMSS of the queue log's name on the day of its mtime
        (the day before when that time is later than the mtime: a run across midnight)."""
        mtime = self.log.stat().st_mtime
        parts = self.log.name.split(".")
        hms = parts[2] if len(parts) > 3 and re.fullmatch(r"\d{6}", parts[2]) else None
        if not hms:
            return mtime
        lt = time.localtime(mtime)
        start = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, int(hms[:2]), int(hms[2:4]), int(hms[4:]), 0, 0, -1))
        return start - 86400 if start > mtime else start


def triples(labels, prefix):
    """(off-a, on, off-b) label triples: <prefix>-off-a/-off-b against every other <prefix>-* label;
    plus the S6 emission triple <prefix>-em-off-a / <prefix>-on / <prefix>-em-off-b. (Cost windows.)"""
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


def is_off(label):
    return "-off-" in label or label.endswith("-off")


def off_family(label):
    return "em" if "-em-off" in label else "base"


def energy_triples(labels):
    """(off before, on, off after, key) for every ON shot: the nearest OFF shot of the base family before
    and after it in plan order (with only one side, the two nearest OFFs); shot-on also against the
    shot-em-off-* family when it has one on both sides (S6 emission). key names the prediction."""
    shots = [l for l in labels if l.startswith("shot-")]
    views = {l.split("-off")[0] for l in shots if is_off(l)}
    out = []
    for fam in ("base", "em"):
        offs = [i for i, l in enumerate(shots) if is_off(l) and off_family(l) == fam]
        if len(offs) < 2:
            continue
        for i, l in enumerate(shots):
            # shot-em-on-* (the S6 emission rerun, m4p5): emission ON with the bundle ON, compared only with the
            # em-off family (the base OFF shots around it have other keys).
            em_on = l.startswith("shot-em-on")
            if is_off(l) or (fam == "em" and l != "shot-on" and not em_on) or (fam == "base" and em_on):
                continue
            # A debug view's ON shot (m4p5 shot-vis-gi-*, shot-add-gi-*) against the OFF shots of the same view only.
            view = lambda s: max((p for p in views if s.startswith(p + "-")), key=len, default="shot")  # noqa: E731
            mine = [j for j in offs if fam == "em" or view(shots[j]) == view(l)]
            before = [j for j in mine if j < i]
            after = [j for j in mine if j > i]
            if before and after:
                a, b = before[-1], after[0]
            elif fam == "em":
                continue
            else:
                near = sorted(mine, key=lambda j: (abs(j - i), j))[:2]
                if len(near) < 2:
                    continue
                a, b = min(near), max(near)
            out.append((shots[a], l, shots[b], l if fam == "base" else f"{l}@em-off"))
    return out


def off_pairs(run):
    """Relative differences of consecutive OFF shots of one family: the shot-to-shot noise at a static view."""
    shots = [l for l in run.labels if l.startswith("shot-") and is_off(l)]
    out = []
    # One sequence per view: the part before "-off" (shot, shot-em, and m4p5's debug views shot-vis / shot-add,
    # which show other images: a pair across them is not shot noise; m4p5 014357 gave sigma 23% that way).
    for fam in dict.fromkeys(l.split("-off")[0] for l in shots):
        seq = [l for l in shots if l.split("-off")[0] == fam]
        vals = []
        for l in seq:
            v = run.scene_colour(l)
            vals.append(v if v and not isinstance(v[0], str) else None)
        for x, y in zip(vals, vals[1:]):
            if x and y and x[0] and y[0]:
                if x[2] and y[2]:   # exposure-normalised (the adaptation drifts between OFF shots too)
                    x0, y0 = x[0] / x[2], y[0] / y[2]
                else:
                    x0, y0 = x[0], y[0]
                out.append((y0 - x0) / ((x0 + y0) / 2))
    return out


_POOL = {}


def pooled_noise(save, queue_logs: Path | None, own_log: str = "", own_pairs=(), captures_root=None, fs=SCENE_FS):
    """(sigma, dof, runs) of the relative shot noise: OFF-OFF pairs of every lean run on this save in the
    queue logs (runs with plan drift excluded), plus the evaluated run's own pairs. Every run's pairs are read
    from the same dump `fs` as the evaluated run (the water-after buffer is noisier: its waves animate)."""
    key = (save, str(queue_logs), fs)
    if key not in _POOL:
        pairs = {}
        if queue_logs and queue_logs.exists() and save not in (None, "?"):
            names = []
            for f in sorted((REPO / "tools" / "live_batches").glob("*/jobs/*.json")):
                try:
                    if json.loads(f.read_text(encoding="utf-8-sig")).get("save") == save:
                        names.append(f.stem)
                except ValueError:
                    continue
            logs = [log for name in names for log in sorted(queue_logs.glob(f"{name}.*.log"))]
            prog = Progress(f"noise pool ({save})", len(logs), unit="logs")
            for log in logs:
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        r = Run(log, captures_root)
                except (SystemExit, OSError, ValueError, KeyError):
                    prog.tick(failed=True)
                    continue
                prog.tick()
                if not r.failed_drift():
                    r.energy_fs = fs
                    p = off_pairs(r)
                    if p:
                        pairs[str(log.resolve())] = p
            prog.finish()
        _POOL[key] = pairs
    pairs = dict(_POOL[key])
    if own_log and own_pairs:
        pairs[own_log] = list(own_pairs)
    flat = [d for ps in pairs.values() for d in ps]
    if not flat:
        return None, 0, 0
    return math.sqrt(sum(d * d for d in flat) / (2 * len(flat))), len(flat), len(pairs)


def predictions(run):
    """{key: entry} for the run's job from <batch>/predicted.json; and the file's commit time."""
    if not run.batch:
        return {}, None
    f = run.batch / "predicted.json"
    if not f.exists():
        return {}, None
    data = json.loads(f.read_text(encoding="utf-8-sig"))
    rel = f.resolve().relative_to(REPO.resolve()).as_posix()
    dirty = bool(_git("status", "--porcelain", "--", rel).strip())
    ct = _git("log", "-1", "--format=%ct", "--", rel).strip()
    committed = None if dirty or not ct else float(ct)
    return data.get(run.name, {}) or {}, committed


def energy_verdict(d, band, p=None, post_hoc=False):
    """Two-sided: pass when |d - p| <= band (p = 0 without a prediction)."""
    if band is None:
        return "NO NOISE ESTIMATE", False
    if p is None:
        if abs(d) <= band:
            return "within (no change)", True
        return ("CHANGED WITHOUT A PREDICTION: brighter" if d > 0 else "CHANGED WITHOUT A PREDICTION: darker"), False
    if abs(d - p) <= band:
        return ("as predicted, post hoc (does not pass)", False) if post_hoc else ("as predicted", True)
    return ("BRIGHTER than predicted" if d > p else "DARKER than predicted"), False


def energy(run, queue_logs: Path | None = None, pool="save"):
    trip = energy_triples(run.labels)
    own = off_pairs(run)
    if pool == "run":
        n = len(own)
        sigma, dof, nruns = (math.sqrt(sum(d * d for d in own) / (2 * n)), n, 1) if n else (None, 0, 0)
    else:
        sigma, dof, nruns = pooled_noise(run.job.get("save"), queue_logs, str(run.log.resolve()), own, run.captures_root,
                                         run.energy_fs)
    m = max(len(trip), 1)
    band = t_quantile(1 - ALPHA_FW / (2 * m), dof) * sigma * math.sqrt(1.5) if sigma is not None and dof >= 1 else None
    pred, committed = predictions(run)
    started = run.started()
    out = []
    for a, on, b, key in trip:
        vals = [run.scene_colour(x) for x in (a, on, b)]
        if any(not v or isinstance(v[0], str) for v in vals):
            out.append({"a": a, "on": on, "b": b, "key": key, "values": vals, "verdict": "INCOMPLETE", "ok": False})
            continue
        va, vo, vb = (v[0] for v in vals)
        off = (va + vb) / 2
        raw_d = (vo - off) / off
        pre = [v[2] for v in vals]
        if all(pre):
            # Exposure-normalised: the eye adaptation scales the whole lit image (gf2 S8: +2.87% raw, -0.32%).
            na, no, nb = va / pre[0], vo / pre[1], vb / pre[2]
            noff = (na + nb) / 2
            d, s = (no - noff) / noff, abs(nb - na) / noff
            exposure = pre[1] / ((pre[0] + pre[2]) / 2)
        else:
            d, s, exposure = raw_d, abs(vb - va) / off, None
        entry = pred.get(key)
        p = entry.get("predicted") if isinstance(entry, dict) else None
        post_hoc = p is not None and (committed is None or committed > started)
        verdict, ok = energy_verdict(d, band, p, post_hoc)
        # Per trace (OFF before, ON, OFF after): the scene colour's own non-finite count; other dumps by (pass,
        # format), those non-finite in an OFF trace too marked game-owned (as scripts/qa/analyze.py).
        off_sigs = set(vals[0][3]) | set(vals[2][3])
        other = [{k: (n, k in off_sigs) for k, n in v[3].items()} for v in vals]
        out.append({"a": a, "on": on, "b": b, "key": key, "values": (va, vo, vb), "d": d, "s": s, "p": p,
                    "raw_d": raw_d, "exposure": exposure, "pre": pre,
                    "band": band, "sigma": sigma, "dof": dof, "pool_runs": nruns,
                    "verdict": verdict, "ok": ok, "nonfinite": tuple(v[1] for v in vals), "other_nonfinite": other})
    return out


def other_nonfinite(rows):
    """{'<pass> <fmt>': {trace label: (count, game_owned)}} over the traces of the given energy rows."""
    out = {}
    for e in rows:
        for label, o in zip((e["a"], e["on"], e["b"]), e.get("other_nonfinite") or ()):
            for k, (n, game) in o.items():
                out.setdefault(k, {})[label] = (n, game)
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


def report(run: Run, queue_logs: Path | None = None, pool="save"):
    """Prints the tables; returns the list of failed gates."""
    failed = []
    print(f"\n## {run.name} ({run.job.get('save', '?')}) - {run.log.name}")
    if run.drift:
        print(f"note: {run.drift}")
    if run.failed_drift():
        print("FAIL: plan drift, no gate evaluated")
        return ["plan drift"]
    print(f"phases {len(run.phases)}/{len(run.plan)}: " + ", ".join(p[0] for p in run.phases))

    print(f"\n### Energy: scene colour before post ({run.energy_fs} float mean luminance) / pre-exposure s1b13[0], two-sided against the prediction")
    rows = energy(run, queue_logs, pool)
    if rows and rows[0].get("sigma") is not None:
        r0 = next((r for r in rows if "band" in r), rows[0])
        if r0.get("sigma") is not None:
            print(f"shot noise sigma {100 * r0['sigma']:.3f}% from {r0['dof']} OFF-OFF pairs of {r0['pool_runs']} run(s); "
                  f"band +-{100 * r0['band']:.2f}% (t at {ALPHA_FW}/{2 * max(len(rows), 1)}, dof {r0['dof']}, x sqrt(3/2))")
    print("| comparison | OFF before | ON | OFF after | raw ON vs OFF | exposure ON / OFF | ON vs mean OFF | predicted | band | old OFF-pair rule | verdict |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for e in rows:
        if e["verdict"] == "INCOMPLETE":
            print(f"| {e['key']} | {e['values']} | | | | | | | | | INCOMPLETE |")
            failed.append(f"energy {e['key']}: INCOMPLETE")
            continue
        va, vo, vb = e["values"]
        old = "within" if abs(e["d"]) <= e["s"] else ("brighter: explain" if e["d"] > 0 else "DARKER")
        pp = "none" if e["p"] is None else f"{100 * e['p']:+.2f}%"
        band = "n/a" if e["band"] is None else f"{100 * e['band']:.2f}%"
        ex = "unknown (raw)" if e["exposure"] is None else f"{e['exposure']:.4f}"
        nf = e["nonfinite"]
        print(f"| {e['key']} vs {e['a']}/{e['b']} | {va:.5f} | {vo:.5f} | {vb:.5f} | {100 * e['raw_d']:+.2f}% | {ex} | {100 * e['d']:+.2f}% | {pp} | {band} | "
              f"{old} (spread {100 * e['s']:.2f}%) | {e['verdict']}{'' if not any(nf) else ', scene colour non-finite %d/%d/%d' % nf} |")
        if not e["ok"]:
            failed.append(f"energy {e['key']}: {e['verdict']}")
    for k, per in sorted(other_nonfinite([e for e in rows if e["verdict"] != "INCOMPLETE"]).items()):
        game = all(g for _, g in per.values())
        print(f"- other dump {k}: non-finite per trace "
              f"({'game-owned: in the OFF traces too' if game else 'NOT in the OFF traces: ours'}; not the measured buffer): "
              + ", ".join(f"{l} {n}" for l, (n, _) in per.items()))
    steps = [l for l in run.labels if l.startswith("shot-step-")]
    if steps:
        vals = {l: run.scene_colour(l) for l in ["shot-off-a"] + steps + ["shot-on"]}
        ok = [(l, v) for l, v in vals.items() if v and not isinstance(v[0], str)]
        norm = bool(ok) and all(v[2] for _, v in ok)   # exposure-normalised when every shot has its probe
        seq = [(l, v[0] / v[2] if norm else v[0]) for l, v in ok]
        if len(seq) > 1:
            print(f"step increments ({'exposure-normalised' if norm else 'raw'}; each cumulative step minus the previous one, relative): " + ", ".join(
                f"{b[0]} {100 * (b[1] - a[1]) / a[1]:+.2f}%" for a, b in zip(seq, seq[1:])))

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
        m = run.last_in(label, DIAG["rf hit gi"])
        if m:
            print(f"- rf hit gi: ratio L_b / L_a {m.group(5)} (without the GI term {m.group(6)}); tier a GI term {m.group(3)} vs the "
                  f"screen's {m.group(4)} ({m.group(1)} hits, {m.group(2)} from the screen's GI); tier b GI term {m.group(7)}; "
                  f"AO-ray screen source {m.group(10)}%")
        m = run.last_in(label, DIAG["hit emission"])
        if m:
            print(f"- hit emission: GI hidden lookups {m.group(1)} emissive {m.group(2)} added {m.group(4)}; rf tier b lookups {m.group(5)} "
                  f"emissive {m.group(6)} added {m.group(8)}; approximate uncalibrated skipped {m.group(9)}")
        m = run.last_in(label, DIAG["instanced rows"])
        if m:
            print(f"- instanced rows (key {m.group(1)}): wind {m.group(2)} rows {m.group(3)} tri, 2S {m.group(4)} rows {m.group(5)} tri")
        m = run.last_in(label, DIAG["instanced clip"])
        if m:
            print(f"- instanced clip: rejected {m.group(1)} = {m.group(2)}% of lookups (gate < 0.04% in every window: the job's expect_every)")

    print("\n### Capacity over the run (all 0 expected; above 0 fails)")
    for name, v, n in run.capacity():
        print(f"- {name}: {v if isinstance(v, str) else fmt(v, 2)} (max of {n} lines)")
        if isinstance(v, float) and v > 0:
            failed.append(f"capacity {name} {v:.2f}")
    print("\nGATES: " + ("all hold" if not failed else "FAIL: " + "; ".join(failed)))
    return failed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--batch", default="m4_promotion")
    ap.add_argument("--queue-logs", type=Path, default=main_checkout() / "captures" / "queue" / "logs")
    ap.add_argument("--captures-root", type=Path, default=None)
    ap.add_argument("--plan", type=Path, default=None, help="plan json (default: from the job of the log's name)")
    ap.add_argument("--pool", choices=("save", "run"), default="save", help="OFF noise from every lean run of the save, or this run only")
    ap.add_argument("--energy-fs", default=None, help="energy dump pass FS prefix instead of the job's energy_fs "
                    f"(e.g. {WATER_AFTER_FS}: re-read an old water run after its compose)")
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
    failed = 0
    for log in logs:
        run = Run(log, a.captures_root, a.plan)
        if a.energy_fs:
            run.energy_fs = a.energy_fs
        failed += bool(report(run, a.queue_logs, a.pool))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
