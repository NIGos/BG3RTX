"""Gate table of the M5 water promotion batch (tools/live_batches/m5_promotion) and of the m5_water runs.

    py -3 -m tools.m5_promotion_gate                      # latest log of every m5_promotion job
    py -3 -m tools.m5_promotion_gate <job>.<n>.<HHMMSS>.log [...]   # given queue logs (m5w2..m5w6 too)
    options: --batch m5_promotion  --queue-logs <dir>  --captures-root <dir>  --plan <plan.json>

Phases, windows and the GPU-cost rule are tools/m4_promotion_gate.py's (a periodic line counts for a
phase only when its whole window lies inside the phase). What is water-specific:

- Energy observable. The m4 gate's scene colour (pass 5c3fdabc) is the lighting's compose, BEFORE the
  forward water pass: water keys reach it only through the eye adaptation (m5w4 11-01: water shadows
  darkened the water, the exposure rose 2.7% and 5c3fdabc read "+2.2% brighter"). Here the measured
  image is the RR colour input (the trace's `00000000` dump, slot 0, render size: the frame after every
  forward and VFX pass, our compose included), divided by the pre-exposure the lighting and the water
  FS used at that frame (`[rrpost] exposure probe (frame N)` b0[0], N = the trace frame). With
  `qa_exposure_hold=1` that value is constant and the division changes nothing.
- Control. 5c3fdabc / b0[0] cannot change with a water key (water is in no TLAS the lighting traces):
  its ON-vs-OFF change is the error of the normalisation, measured in the same run.
- Prediction (two-sided). The compose logs its own accounting per 120-frame window: reflections
  `compose energy ... mean delta over owned`, sun `shadows ... mean removed over owned`, local lights
  `local shadows ... mean removed over owned`, refraction `refraction ... mean delta over owned`. The
  predicted change of the image mean is their sum times owned pixels per pass over the render size
  (`compose images WxH`), per complete window of the shot phase (or of the cost phase before it in the
  same state); relative to the OFF mean.
- Noise. The water's animated sun glints (up to ~1000 against an image mean of 0.18) move a single
  frame's mean by up to ~1-2% at S8, so a shot phase takes several traces. Tolerance = two standard
  errors of the ON-minus-OFF difference from the sample SD of all OFF frames (key-off repeats)
  + |control| + the prediction's spread over the windows.
  `matches` inside it; `BRIGHTER/DARKER than predicted` outside (a FAIL: the image does something the
  compose did not account for).
- Participation per key from every complete window of every phase where the key is on (min..max):
  the every-window behaviour the queue's expect_every gates; plus the water GPU lines.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import m4_promotion_gate as m4  # noqa: E402

REPO = m4.REPO
LUM = (0.2126, 0.7152, 0.0722)
RR_FS = "00000000"   # the RR evaluate's guide dumps (layer/ngx.cpp trace_guides): slot 0 = colour input
SCENE_FS = m4.SCENE_FS

PROBE = re.compile(r"\[rrpost\] exposure probe \(frame (\d+)\): 2cdd9f08 b0 = \[([0-9.eE+-]+) ")
TRACE_FRAME = re.compile(r"\[trace\] frame (\d+) written: .* -> (.+)$")
IMAGES = re.compile(r"\[water\] compose images (\d+)x(\d+)")
HELD = re.compile(r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV ([0-9.]+)")

# Window lines (one per rt_timing_frames). Groups are named.
W = {
    "capture": re.compile(r"\[water\] capture \((?P<f>\d+) frames\): supported draws (?P<sup>\d+), unknown (?P<unk>\d+), "
                          r"excluded (?P<exc>\d+) .*triangles (?P<tri>\d+), overflow (?P<ovf>\d+), failed (?P<fail>\d+)"),
    "families": re.compile(r"\[water\] families: (?P<txt>.*)$"),
    "reflections": re.compile(r"\[water\] reflections \((?P<f>\d+) frames, (?P<tr>\d+) traced\): water px [0-9.]+% of (?P<px>\d+) "
                              r"px/frame \(in front (?P<front>[0-9.]+)%.*rays (?P<rays>\d+)/frame: screen hit (?P<hit>[0-9.]+)%, "
                              r"hidden (?P<hid>[0-9.]+)%.*off screen (?P<off>[0-9.]+)%, miss (?P<miss>[0-9.]+)%, aborted (?P<ab>\d+), "
                              r"invalid (?P<inv>\d+)"),
    "compose": re.compile(r"\[water\] compose \((?P<f>\d+) frames, (?P<comp>\d+) composed, (?P<ppf>[0-9.]+) passes/frame\): "
                          r"records (?P<rec>[0-9.]+)/frame, owned (?P<own>[0-9.]+)% .*unresolved \(probe kept\) (?P<unres>[0-9.]+)% "
                          r"of owned; clamped (?P<cl>\d+);.*refused: no recent lighting (?P<nolight>\d+), no scene (?P<noscene>\d+)"),
    "energy": re.compile(r"\[water\] compose energy: mean C (?P<C>[0-9.]+) over owned; composed (?P<compown>[0-9.]+)% of owned: .*"
                         r"mean delta over owned (?P<d>[-+][0-9.]+) = (?P<dpct>[-+][0-9.]+)% of mean C"),
    "ratio": re.compile(r"\[water\] compose ratio A L / B over (?P<n>\d+) composed px: below 0\.5 (?P<lo>[0-9.]+)%, 0\.5\.\.2\.0 "
                        r"(?P<mid>[0-9.]+)%, above 2\.0 (?P<hi>[0-9.]+)%; ratio of the means (?P<r>[0-9.]+)"),
    "shadows": re.compile(r"\[water\] shadows \((?P<f>\d+) frames, (?P<p>\d+) passes, variant (?P<v>\w+)\): sun records (?P<rec>[0-9.]+)/frame "
                          r"= (?P<own>[0-9.]+)% of owned .*traced (?P<tr>[0-9.]+)% of sun records, occluded (?P<occ>[0-9.]+)% of "
                          r"traced .*unknown (?P<unk>\d+) .*removed (?P<rm>[0-9.]+) = (?P<rmD>[0-9.]+)% of D; mean removed over owned "
                          r"(?P<d>[0-9.]+) = (?P<dpct>[0-9.]+)% of mean C; clamped (?P<cl>\d+)"),
    "local": re.compile(r"\[water\] local shadows \((?P<f>\d+) frames, (?P<p>\d+) passes with the light list, (?P<nol>\d+) without\): "
                        r"local records (?P<rec>[0-9.]+)/frame = (?P<own>[0-9.]+)% of owned .*occluded (?P<occ>[0-9.]+)% of traced, "
                        r"unknown (?P<unk>\d+) .*sum check over (?P<chk>\d+) records: within [0-9.]+% (?P<within>[0-9.]+)%, .*"
                        r"ratio of the means sum / L (?P<ratio>[0-9.]+);.*mean removed over owned (?P<d>[0-9.]+) = "
                        r"(?P<dpct>[0-9.]+)% of mean C; clamped (?P<cl>\d+)"),
    "refraction": re.compile(r"\[water\] refraction \((?P<f>\d+) frames, (?P<p>\d+) passes with the colour copy, (?P<nocopy>\d+) without\): "
                             r"transmissive records (?P<rec>[0-9.]+)/frame = (?P<own>[0-9.]+)% of owned .*no usable copy (?P<nuc>\d+)\); "
                             r".*screen hit \(composed\) (?P<hit>[0-9.]+)%, hidden (?P<hid>[0-9.]+)%.*screen shift (?P<shift>[0-9.]+) px.*"
                             r"ratio of the means (?P<ratio>[0-9.]+)\); mean delta over owned (?P<d>[-+][0-9.]+) = (?P<dpct>[-+][0-9.]+)% "
                             r"of mean C; clamped (?P<cl>\d+)"),
    # M5.4c (rt_water_game_shadow): the sun light restored where the game's shadow map was replaced by a lit ray.
    "gsh": re.compile(r"\[water\] game shadow \((?P<f>\d+) frames, variant (?P<v>\w+)\): game-shadowed records "
                      r"(?P<rec>[0-9.]+)/frame = (?P<own>[0-9.]+)% of owned \(invalid (?P<inv>\d+)\), mean Vg (?P<vg>[0-9.]+); "
                      r"lit by the traced ray \(restored C \+ Dg\) (?P<rest>[0-9.]+)% of them; .*mean restored over owned "
                      r"(?P<d>[0-9.]+) = (?P<dpct>[0-9.]+)% of mean C"),
}

COST = {
    "frame time (pace median)": (re.compile(r"\[pace\] rendered frame time over (\d+) frames: median ([0-9.]+) ms"), 2),
    "gi: whole GI": (re.compile(r"gi: GPU cost ([0-9.]+) ms/frame \(average of (\d+) frames\)"), 1),
    "rt passes": (re.compile(r"rt passes: GPU cost ([0-9.]+) ms/frame"), 1),
    "water: build": (re.compile(r"\[water\] build GPU ([0-9.]+) ms median"), 1),
    "water: reflections (primary + mirror)": (re.compile(r"\[water\] reflections GPU ([0-9.]+) ms median"), 1),
    "water: compose (copy + dispatches)": (re.compile(r"\[water\] compose GPU ([0-9.]+) ms median"), 1),
    "rt scene build (static)": (re.compile(r"\[rt\] scene build GPU \(split, static, \d+ samples\): .*total median ([0-9.]+) ms"), 1),
}


def complete_windows(run, label, rx):
    """Matches of rx whose whole window lies inside the phase (the m4 rule): the previous line of the
    same kind is at or after the phase's settings read, the line itself before the phase ends."""
    p = run.phase_of(label)
    if not p:
        return []
    _, s, e = p
    prev, out = None, []
    for t, txt in run.lines:
        m = rx.search(txt)
        if not m:
            continue
        if prev is not None and prev >= s and t <= e:
            out.append(m)
        prev = t
    return out


def cost(run, label):
    out = {}
    for name, (rx, g) in COST.items():
        vals = [float(m.group(g)) for m in complete_windows(run, label, rx)]
        if vals:
            out[name] = (statistics.median(vals), len(vals))
    return out


def lum(mean):
    return sum(c * w for c, w in zip(mean[:3], LUM)) if len(mean) >= 3 else float(mean[0])


def traces_of(run, label):
    """[(frame, capture dir)] of every trace written in the phase (a shot phase may take several:
    the water's animated sun glints move the image mean from frame to frame)."""
    p = run.phase_of(label)
    if not p:
        return []
    out = []
    for t, txt in run.lines:
        if p[1] <= t <= p[2] + 2.0:
            m = TRACE_FRAME.search(txt)
            if m:
                cap = Path(m.group(2).strip())
                if run.captures_root:
                    cap = run.captures_root / cap.name
                out.append((int(m.group(1)), cap))
    return out


def exposure_at(run, frame):
    """b0[0] of the exposure probe at the trace frame (the probe logs every traced frame)."""
    best = None
    for _, txt in run.lines:
        m = PROBE.search(txt)
        if m:
            f = int(m.group(1))
            if f == frame:
                return float(m.group(2))
            if f < frame:
                best = float(m.group(2))
    return best


def shot(run, label):
    """Exposure-normalised values of every trace of a shot phase (a list), or a string (what is missing)."""
    tr = traces_of(run, label)
    if not tr:
        return "no trace"
    vals = [frame_values(run, f, c) for f, c in tr]
    bad = [v for v in vals if isinstance(v, str)]
    return bad[0] if bad else vals


def frame_values(run, frame, cap):
    """{'post', 'pre' (both divided by the exposure), 'e', 'nonfinite'} of one trace, or a string."""
    f = cap / "dumps.json"
    if not f.exists():
        return f"missing {f}"
    dumps = json.loads(f.read_text(encoding="utf-8"))
    pre = [(int(k[1:]), d) for k, d in dumps.items()
           if str(d.get("pass_fs", "")).startswith(SCENE_FS) and d.get("slot") == 0 and d.get("mean")]
    if not pre:
        return "no 5c3fdabc dump"
    _, pre_d = max(pre, key=lambda c: c[0])
    post = [(int(k[1:]), d) for k, d in dumps.items()
            if str(d.get("pass_fs", "")).startswith(RR_FS) and d.get("slot") == 0 and d.get("mean")
            and d.get("fmt") == pre_d.get("fmt") and d.get("w") == pre_d.get("w") and d.get("h") == pre_d.get("h")]
    if not post:
        return "no RR colour input dump (rr=1 needed)"
    _, post_d = max(post, key=lambda c: c[0])
    e = exposure_at(run, frame)
    if not e:
        return "no exposure probe"
    nonfinite = sum(int(x.get("nonfinite", 0) or 0) for x in dumps.values())
    return {"post": lum(post_d["mean"]) / e, "pre": lum(pre_d["mean"]) / e, "e": e, "frame": frame, "nonfinite": nonfinite}


def render_pixels(run):
    for _, txt in run.lines:
        m = IMAGES.search(txt)
        if m:
            return int(m.group(1)) * int(m.group(2))
    return None


def same_state_windows(run, label, kind):
    """Complete windows of the phase; when it has none (a short shot phase), those of the phases right
    before it with the same plan set (a shot follows its cost window in the same state)."""
    ms = complete_windows(run, label, W[kind])
    if ms or label not in run.labels:
        return ms
    i = run.labels.index(label)
    while i > 0 and run.plan[i - 1].get("set") == run.plan[i].get("set"):
        i -= 1
        ms = complete_windows(run, run.labels[i], W[kind])
        if ms:
            return ms
    return []


def predicted_raw(run, label):
    """Per complete window of the phase (or of its same-state predecessor): the compose's own accounting
    of the change of the image mean, in target units (the pre-exposed colour). [] without a window."""
    px = render_pixels(run)
    comps = same_state_windows(run, label, "compose")
    if not px or not comps:
        return []
    groups = {k: same_state_windows(run, label, k) for k in ("energy", "shadows", "local", "refraction", "gsh")}
    out = []
    for i, c in enumerate(comps):
        own_px = float(c.group("rec")) * float(c.group("own")) / 100.0 / max(float(c.group("ppf")), 1.0)
        s = 0.0
        if i < len(groups["energy"]):
            s += float(groups["energy"][i].group("d"))
        if i < len(groups["shadows"]):
            s -= float(groups["shadows"][i].group("d"))
        if i < len(groups["local"]):
            s -= float(groups["local"][i].group("d"))
        if i < len(groups["refraction"]):
            s += float(groups["refraction"][i].group("d"))
        if i < len(groups["gsh"]):   # M5.4c: the sun light the game's shadow removed, restored where the ray is lit
            s += float(groups["gsh"][i].group("d"))
        out.append(s * own_px / px)
    return out


def energy_row(run, a, x, b):
    """Two-sided energy gate of step x against every OFF frame of phases a and b."""
    va, vx, vb = (shot(run, l) for l in (a, x, b))
    if any(isinstance(v, str) for v in (va, vx, vb)):
        return {"a": a, "x": x, "b": b, "verdict": "INCOMPLETE", "why": [v for v in (va, vx, vb) if isinstance(v, str)]}
    off = va + vb
    posts = [v["post"] for v in off]
    off_post = statistics.fmean(posts)
    # The noise is the key-off repeats' frame-to-frame spread (the water's animated sun glints, up to
    # ~1000 against a mean of 0.18, move the image mean): sample SD of the OFF frames.
    sd = statistics.stdev(posts)
    x_post = statistics.fmean(v["post"] for v in vx)
    meas = (x_post - off_post) / off_post
    noise = 2.0 * sd * (1.0 / len(vx) + 1.0 / len(off)) ** 0.5 / off_post   # two standard errors of the difference
    drift = (statistics.fmean(v["post"] for v in vb) - statistics.fmean(v["post"] for v in va)) / off_post
    off_pre = statistics.fmean(v["pre"] for v in off)
    control = (statistics.fmean(v["pre"] for v in vx) - off_pre) / off_pre
    preds = predicted_raw(run, x)
    e_x = statistics.fmean(v["e"] for v in vx)
    if preds:
        pr = [p / e_x / off_post for p in preds]
        pred, pspread = statistics.median(pr), max(pr) - min(pr)
    else:
        pred, pspread = 0.0, 0.0   # no compose window: the key changes no water pixel (identity expected)
    tol = noise + abs(control) + pspread
    dev = meas - pred
    verdict = "matches" if abs(dev) <= tol else ("BRIGHTER than predicted" if dev > 0 else "DARKER than predicted")
    # Non-finite pixels over every dump of a frame: a step may not add any (the m4 bundle failed on NaN).
    nf_off_max = max(v["nonfinite"] for v in off)
    nf_x = max(v["nonfinite"] for v in vx)
    if nf_x > nf_off_max:
        verdict += f"; NONFINITE {nf_x} > OFF {nf_off_max}"
    raw = lambda vs: statistics.fmean(v["pre"] * v["e"] for v in vs)   # noqa: E731  (the m4 gate's observable)
    raw_old = (raw(vx) - (raw(va) + raw(vb)) / 2) / ((raw(va) + raw(vb)) / 2)
    return {"a": a, "x": x, "b": b, "meas": meas, "pred": pred, "windows": len(preds), "noise": noise, "control": control,
            "pspread": pspread, "tol": tol, "verdict": verdict, "drift": drift, "n": (len(va), len(vx), len(vb)),
            "nonfinite": (nf_off_max, nf_x), "raw_5c3fdabc": raw_old}


def shot_triples(labels):
    a, b = "shot-off-a", "shot-off-b"
    if a not in labels or b not in labels:
        return []
    return [(a, l, b) for l in labels if l.startswith("shot-") and l not in (a, b)]


def rng(ms, g, fmt="%.2f"):
    vals = [float(m.group(g)) for m in ms]
    if not vals:
        return "n/a"
    lo, hi = min(vals), max(vals)
    return (fmt % lo) if lo == hi else f"{fmt % lo}..{fmt % hi}"


def on_phases(run, rx):
    """Phases (in plan order) with at least one complete window of rx."""
    return [l for l in run.labels if complete_windows(run, l, rx)]


def participation(run):
    """Rows (key, observable, value over every complete ON window, gate, verdict)."""
    rows = []

    def every(kind):
        ms = []
        for l in on_phases(run, W[kind]):
            if l.startswith(("warmup", "restore")):
                continue
            ms += complete_windows(run, l, W[kind])
        return ms

    cap = every("capture")
    if cap:
        ok = all(int(m.group("sup")) > 0 and int(m.group("ovf")) == 0 and int(m.group("fail")) == 0 for m in cap)
        rows.append(("rt_water_geometry", "supported draws / overflow / failed", f"{rng(cap, 'sup', '%.0f')} / {rng(cap, 'ovf', '%.0f')} / {rng(cap, 'fail', '%.0f')}",
                     "> 0 / 0 / 0 every window", "pass" if ok else "FAIL"))
    rf = every("reflections")
    if rf:
        ok = all(float(m.group("front")) > 0 and int(m.group("ab")) == 0 and int(m.group("inv")) == 0 for m in rf)
        rows.append(("rt_water_reflections", "in front % / screen hit % / hidden % / miss %",
                     f"{rng(rf, 'front')} / {rng(rf, 'hit', '%.1f')} / {rng(rf, 'hid', '%.1f')} / {rng(rf, 'miss', '%.1f')}",
                     "in front > 0, aborted 0, invalid 0", "pass" if ok else "FAIL"))
    co = every("compose")
    if co:
        ok = all(int(m.group("comp")) >= int(m.group("f")) * 11 // 12 and float(m.group("own")) > 0 for m in co)
        rows.append(("rt_water_compose", "composed frames of 120 / owned % / unresolved %",
                     f"{rng(co, 'comp', '%.0f')} / {rng(co, 'own')} / {rng(co, 'unres')}", ">= 110 of 120, owned > 0",
                     "pass" if ok else "FAIL"))
    en = [m for m in every("energy") if float(m.group("compown")) > 0]
    if en:
        rows.append(("rt_water_compose", "composed % of owned / delta % of mean C", f"{rng(en, 'compown')} / {rng(en, 'dpct', '%+.3f')}",
                     "reported (the probe's error is corrected)", "info"))
    ra = [m for m in every("ratio") if int(m.group("n")) > 0]
    if ra:
        rows.append(("rt_water_compose", "A L / B bins <0.5 / 0.5-2 / >2 %, ratio of the means",
                     f"{rng(ra, 'lo', '%.1f')} / {rng(ra, 'mid', '%.1f')} / {rng(ra, 'hi', '%.1f')}, {rng(ra, 'r', '%.3f')}",
                     "a spread (one bin = unit error)", "pass" if all(float(m.group("mid")) < 99.0 for m in ra) else "FAIL"))
    sh = [m for m in every("shadows") if float(m.group("tr")) > 0]
    if sh:
        ok = all(float(m.group("occ")) > 0 and int(m.group("cl")) < int(m.group("f")) for m in sh)
        rows.append(("rt_water_shadows", "occluded % of traced / removed % of mean C / clamped",
                     f"{rng(sh, 'occ')} / {rng(sh, 'dpct', '%.3f')} / {rng(sh, 'cl', '%.0f')}", "occluded > 0, clamped < 1 per frame",
                     "pass" if ok else "FAIL"))
    lo = [m for m in every("local") if int(m.group("chk")) > 0]
    if lo:
        ok = all(float(m.group("within")) >= 99.0 and abs(float(m.group("ratio")) - 1) <= 1e-3 and int(m.group("cl")) < int(m.group("f")) for m in lo)
        rows.append(("rt_water_ls_shadows", "local records % / sum check within % / ratio / occluded % / clamped",
                     f"{rng(lo, 'own')} / {rng(lo, 'within')} / {rng(lo, 'ratio', '%.5f')} / {rng(lo, 'occ')} / {rng(lo, 'cl', '%.0f')}",
                     "within >= 99%, ratio 1 +- 0.001, clamped < 1 per frame", "pass" if ok else "FAIL"))
    rr = [m for m in every("refraction") if float(m.group("rec")) > 0]
    if rr:
        ok = all(float(m.group("hit")) > 0 and int(m.group("nuc")) == 0 and int(m.group("cl")) < int(m.group("f")) for m in rr)
        rows.append(("rt_water_refraction", "transmissive % / screen hit % / shift px / ratio of the means / clamped",
                     f"{rng(rr, 'own')} / {rng(rr, 'hit', '%.1f')} / {rng(rr, 'shift', '%.1f')} / {rng(rr, 'ratio', '%.4f')} / {rng(rr, 'cl', '%.0f')}",
                     "screen hit > 0, no usable copy 0, clamped < 1 per frame", "pass" if ok else "FAIL"))
    return rows


def motion(run):
    """Pan phases: the compose keeps composing while the camera moves (previous-frame lighting)."""
    out = []
    for l in run.labels:
        if not l.startswith("move-"):
            continue
        co = complete_windows(run, l, W["compose"])
        sh = complete_windows(run, l, W["shadows"])
        out.append((l, len(co), rng(co, "comp", "%.0f"), rng(co, "nolight", "%.0f"), rng(sh, "cl", "%.0f"),
                    "pass" if co and all(int(m.group("comp")) >= int(m.group("f")) * 11 // 12 for m in co)
                    and all(int(m.group("cl")) < int(m.group("f")) for m in sh) else ("no window" if not co else "FAIL")))
    return out


def report(run):
    print(f"\n## {run.name} ({run.job.get('save', '?')}) - {run.log.name}")
    print(f"phases {len(run.phases)}/{len(run.plan)}: " + ", ".join(p[0] for p in run.phases))
    held = [m.group(1) for _, txt in run.lines for m in [HELD.search(txt)] if m]
    print(f"exposure hold: {'EV ' + held[0] if held else 'off (normalised by the probe b0[0])'}")

    print("\n### Energy: RR colour input (after the water) / exposure, two-sided against the compose's prediction")
    print("| step vs OFF-a/OFF-b | frames a/x/b | measured | predicted (windows) | deviation | tolerance = 2 SE + control + pred spread | verdict | OFF drift b - a | 5c3fdabc raw (m4 gate) |")
    print("|---|---|---|---|---|---|---|---|---|")
    rows = [energy_row(run, *t) for t in shot_triples(run.labels)]
    for e in rows:
        if e["verdict"] == "INCOMPLETE":
            print(f"| {e['x']} | | | | | | INCOMPLETE: {'; '.join(e['why'])} | | |")
            continue
        nf = "" if e["nonfinite"] == (0, 0) or "NONFINITE" in e["verdict"] else f", nonfinite OFF {e['nonfinite'][0]} / x {e['nonfinite'][1]}"
        print(f"| {e['x']} | {'/'.join(str(k) for k in e['n'])} | {100 * e['meas']:+.2f}% | {100 * e['pred']:+.2f}% ({e['windows']}) | "
              f"{100 * (e['meas'] - e['pred']):+.2f}% | {100 * e['tol']:.2f}% = {100 * e['noise']:.2f} + {100 * abs(e['control']):.2f} + "
              f"{100 * e['pspread']:.2f} | {e['verdict']}{nf} | {100 * e['drift']:+.2f}% | {100 * e['raw_5c3fdabc']:+.2f}% |")

    print("\n### Cost (GPU ms, medians of the windows fully inside each phase; n = windows)")
    for a, on, b in m4.triples(run.labels, "cost"):
        ca, co, cb = cost(run, a), cost(run, on), cost(run, b)
        print(f"\n{on} vs {a}/{b}")
        print("| line | OFF-a | ON | OFF-b | ON - mean OFF | OFF spread | n |")
        print("|---|---|---|---|---|---|---|")
        for name in COST:
            if name not in co and name not in ca:
                continue
            xa, xo, xb = (c.get(name, (None, 0)) for c in (ca, co, cb))
            if None in (xa[0], xb[0]) and xo[0] is not None:   # a water line exists only with its key on
                print(f"| {name} | - | {xo[0]:.3f} | - | {xo[0]:+.3f} | | {xa[1]}/{xo[1]}/{xb[1]} |")
                continue
            if None in (xa[0], xo[0], xb[0]):
                print(f"| {name} | {m4.fmt(xa[0])} | {m4.fmt(xo[0])} | {m4.fmt(xb[0])} | | | {xa[1]}/{xo[1]}/{xb[1]} |")
                continue
            off = (xa[0] + xb[0]) / 2
            print(f"| {name} | {xa[0]:.3f} | {xo[0]:.3f} | {xb[0]:.3f} | {xo[0] - off:+.3f} | {abs(xb[0] - xa[0]):.3f} | {xa[1]}/{xo[1]}/{xb[1]} |")
        chain = [co[k][0] for k in COST if k.startswith("water:") and k in co]
        if chain:
            print(f"\nwater chain GPU in {on} (the water lines' medians summed): {sum(chain):.3f} ms")

    print("\n### Participation, every complete window with the key on (min..max)")
    print("| key | observable | value | gate | verdict |")
    print("|---|---|---|---|---|")
    for r in participation(run):
        print("| " + " | ".join(r) + " |")
    fam = None
    for _, txt in run.lines:
        m = W["families"].search(txt)
        if m:
            fam = m.group("txt")
    if fam:
        print(f"\nfamilies (last window): {fam[:400]}")

    mv = motion(run)
    if mv:
        print("\n### Motion (pan windows, chain on)")
        print("| phase | windows | composed of 120 | refused: no recent lighting | sun clamped | verdict |")
        print("|---|---|---|---|---|---|")
        for r in mv:
            print("| " + " | ".join(str(x) for x in r) + " |")

    print("\n### Capacity over the run (all 0 expected)")
    for name, v, n in run.capacity():
        print(f"- {name}: {v if isinstance(v, str) else m4.fmt(v, 2)} (max of {n} lines)")
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--batch", default="m5_promotion")
    ap.add_argument("--queue-logs", type=Path, default=m4.main_checkout() / "captures" / "queue" / "logs")
    ap.add_argument("--captures-root", type=Path, default=None)
    ap.add_argument("--plan", type=Path, default=None, help="plan json (default: from the job of the log's name)")
    a = ap.parse_args(argv)
    logs = list(a.logs)
    if not logs:
        for f in sorted((REPO / "tools" / "live_batches" / a.batch / "jobs").glob("*.json")):
            found = sorted(a.queue_logs.glob(f"{f.stem}.*.log"), key=lambda p: p.stat().st_mtime)
            if found:
                logs.append(found[-1])
            else:
                print(f"{f.stem}: no log in {a.queue_logs}")
    if not logs:
        return 1
    for log in logs:
        report(m4.Run(log, a.captures_root, a.plan))
    return 0


if __name__ == "__main__":
    sys.exit(main())
