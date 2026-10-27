"""One game run per scene: the phase plan, live toggles and capture collection."""
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CAPS = ROOT / "captures"

# Everything an all-on trace has; each phase below changes keys relative to the
# previous one (live_phases applies them cumulatively to captures/auto.ini).
ALL_ON = {"rt": "1", "gi": "1", "rt_reflections": "1", "rt_shadows": "1", "rt_local_shadows": "1",
          "rt_debug": "0", "rt_backlit": "1", "rt_sun_replace": "1"}
ALL_OFF = {"rt": "0", "gi": "0", "rt_reflections": "0", "rt_shadows": "0", "rt_local_shadows": "0",
           "rt_debug": "0"}

# Effect -> (trace with the effect, trace without it). D-RF needs gi=1, so gi=0
# also removes the reflections: the GI's own share is refl_off minus gi_off.
EFFECTS = {
    "gi": ("refl_off", "gi_off"),
    "rt_reflections": ("on0", "refl_off"),
    "rt_shadows": ("on0", "sun_off"),
    "rt_local_shadows": ("on0", "local_off"),
    "all_rt": ("on0", "off0"),
}
# --sharc (plan below): the radiance cache against the same settings without it, in all and
# with the reflections off (the GI's own share, against gi_off like "gi").
# The light-off floors of the hard_dark energy bound (docs/QA.md "Thresholds and their
# derivation"): effect -> the trace with that effect's light removed (qa_zero), everything else
# as in the effect's own trace. An effect that only removes its own light can never take a
# pixel below that floor (beyond the noise): a darker pixel is light removed that was not its.
#   sun_zero  all on, the sun mask written 0 (qa_zero bit 0): no direct sun;
#   ls_zero   all on, every D-LS light's visibility 0 (bit 1);
#   amb_zero  GI on, reflections off, the AO written 0 (bit 2): no diffuse or specular
#             ambient, and the game's AO-on-direct factor halves the direct light, so this
#             floor sits lower than "all but the ambient" (a weaker, still valid bound);
#   all_zero  all on, bits 0-2.
FLOORS = {"rt_shadows": "sun_zero", "rt_local_shadows": "ls_zero", "gi": "amb_zero",
          "rt_reflections": "amb_zero", "all_rt": "all_zero"}
SHARC_EFFECTS = {
    "sharc": ("sharc_on", "on0"),
    "gi_sharc": ("sharc_refl_off", "gi_off"),
}


def scenes():
    """Scene id -> save substring, from scripts/scene-sweep.ps1 (the single list)."""
    text = (ROOT / "scripts" / "scene-sweep.ps1").read_text(encoding="utf-8")
    return dict(re.findall(r'(S\d+) = "([^"]+)"', text))


# --temporal-gi: the all-on set again with the GI's image contribution removed (no bounce,
# the game's own AO), so the flicker of the GI is measured against everything else on.
GI_NEUTRAL = {"gi_strength": "0", "gi_ao": "0"}


def plan(temporal=3, reference=False, split_gi=False, gi_restore=None, split_sun=False, sharc=False, floors=True):
    """The ordered traces of one run: (name, settings applied before it).
    gi_restore: the gi_strength/gi_ao values to go back to after the nogi set.
    split_sun (--temporal-sun): a nosun set, all on but the RT sun shadows (the game's mask
    and its contact shadows), for the flicker of the RT sun alone.
    sharc (--sharc): after the sun traces, the SHaRC radiance cache (sharc=1, DESIGN_SOTA_NEXT
    item 2) with everything on (sharc_on), with the reflections off (sharc_refl_off: the GI's
    share), and its roof mask (sharc_roof, sharc_debug=5) for the leak gate roofed_brightening;
    then sharc=0 again. Each comes interval presents after the switch, so the cache has converged."""
    p = [("on0", {})]
    p += [(f"on{i}", {}) for i in range(1, temporal)]
    restore = {}
    if split_gi:
        p += [("nogi0", dict(GI_NEUTRAL))]
        p += [(f"nogi{i}", {}) for i in range(1, temporal)]
        restore = dict({"gi_strength": "1.0", "gi_ao": "1.0"}, **(gi_restore or {}))
    if split_sun:
        p += [("nosun0", dict(restore, rt_shadows="0"))]
        p += [(f"nosun{i}", {}) for i in range(1, temporal)]
        restore = {"rt_shadows": "1"}
    p += [("gi_off", {"gi": "0", "rt_reflections": "0", **restore}),
          ("refl_off", {"gi": "1", "rt_reflections": "0"}),
          ("sun_off", {"rt_reflections": "1", "rt_shadows": "0"}),
          ("local_off", {"rt_shadows": "1", "rt_local_shadows": "0"}),
          # rt_sun_replace is measured in-frame (sun_check below), not across traces:
          # animation (water, wind in foliage) defeats a trace-to-trace A/B.
          ("sun_dbg", {"rt_local_shadows": "1", "rt_sun_replace": "1", "rt_debug": "2", "rt_backlit": "0"}),
          # rt_debug=7: the in-frame A/B of rt_sun_replace, the written mask against
          # min(game, traced) of the same pixel and frame (yellow brightened at the game's
          # shadow edge, red brightened farther from it, blue darker).
          ("sun_check", {"rt_debug": "7", "rt_backlit": "1"}),
          # rt_debug=6: the trust of rt_sun_replace (red roof volume, magenta far from the
          # game's lit mask, blue off-screen, green trusted).
          ("sun_trust", {"rt_debug": "6", "rt_backlit": "1"})]
    # rt_debug=8: the TLAS hit in front of the depth buffer, in tolerances (detectors.tlas_front,
    # the geometric gate for alpha-tested geometry the trace keeps solid). Repeated, so its
    # per-pixel spread (jitter, stochastic alpha) is measured: max(2, temporal) traces.
    p += [("tlas0", {"rt_debug": "8"})]
    p += [(f"tlas{i}", {}) for i in range(1, max(2, temporal))]
    back = {"rt_debug": "0"}
    if floors:   # FLOORS above; the next phase restores qa_zero=0 and the reflections
        p += [("sun_zero", {"rt_debug": "0", "rt_backlit": "1", "qa_zero": "1"}),
              ("ls_zero", {"qa_zero": "2"}),
              ("all_zero", {"qa_zero": "7"}),
              ("amb_zero", {"qa_zero": "4", "rt_reflections": "0"})]
        back = {"qa_zero": "0", "rt_reflections": "1", "rt_debug": "0"}
    if sharc:
        p += [("sharc_on", {**back, "rt_debug": "0", "rt_backlit": "1", "sharc": "1"}),
              ("sharc_refl_off", {"rt_reflections": "0"}),
              ("sharc_roof", {"rt_reflections": "1", "sharc_debug": "5"})]
    if reference:
        # More samples for the stochastic terms, same denoiser (RR) and view.
        p += [("ref", {**(back if not sharc else {}), "rt_debug": "0", "rt_backlit": "1", "gi_rr_rays": "8",
                       "ls_samples": "2"})]
    first_off = dict(ALL_OFF, rt_backlit="1", gi_rr_rays="1", ls_samples="0") if reference else dict(ALL_OFF, rt_backlit="1")
    if sharc:
        first_off.update(sharc="0", sharc_debug="0")
    first_off.setdefault("qa_zero", "0")
    p += [("off0", first_off)]
    p += [(f"off{i}", {}) for i in range(1, temporal)]
    return p


def run_scene(scene_id, save, out_dir: Path, full_res=False, temporal=3, reference=False,
              interval=240, timeout=900, raw=False, log=print, tag="", exclusive=False, shared=False,
              extra=(), split_gi=False, split_sun=False, sharc=False, floors=True):
    """Runs the game once and moves each trace's dumps.json + img/ to out_dir/raw/<i>_<name>/.

    tag: auto-run.ps1 -Tag, so two scenes can run at once from this checkout (one tag per
    lane). shared: auto-run.ps1 -Shared (another game may share the GPU; run.json then
    says "exclusive": false); otherwise the run is exclusive and run_s is a timing."""
    keep = dict(x.split("=", 1) for x in extra if "=" in x)
    phases = plan(temporal, reference, split_gi, {k: keep[k].strip() for k in GI_NEUTRAL if k in keep},
                  split_sun, sharc, floors)
    out_dir.mkdir(parents=True, exist_ok=True)
    spec = {"start": r"\[trace\] frame \d+ written", "phases": [], "timeout": timeout + 3600, "tag": tag,
            "after_trace": [{"name": n, "set": s} for n, s in phases[1:]]}
    spec_path = out_dir / "phases.json"
    spec_path.write_text(json.dumps(spec, indent=1))
    marks = CAPS / (f"phases.{tag}.log" if tag else "phases.log")
    marks.unlink(missing_ok=True)

    sets = [f"{k}={v}" for k, v in ALL_ON.items()]
    sets += [f"auto_interval={interval}", f"dump_png_width={3840 if full_res else 1280}"]
    if sharc:
        sets += ["sharc=0", "sharc_debug=0", "sharc_stats=1"]   # on0 is the cache-off baseline
    sets += list(extra)   # --set: keys for every trace of the run (e.g. rt_sun_top=0)
    if raw:
        # Exact float32 of the final frame (and RR/rr_post images): about 1 GB per trace
        # at 3840x1600, and slower traces. The 8-bit PQ previews suffice for the detectors.
        sets += ["dump_raw=1"]
    t0 = time.time()
    lp = subprocess.Popen([sys.executable, str(ROOT / "scripts" / "live_phases.py"), str(spec_path)],
                          stdout=open(out_dir / "live_phases.out", "w"), stderr=subprocess.STDOUT)
    try:
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "scripts" / "auto-run.ps1"),
               "-NoBuild", "-NoAnalyze", "-Background", "-Save", save, "-Traces", str(len(phases)),
               "-TimeoutSec", str(timeout), "-Set", ",".join(sets)]
        if tag:
            cmd += ["-Tag", tag]
        if exclusive:
            cmd += ["-Exclusive"]
        elif shared:
            cmd += ["-Shared"]
        log(f"[{scene_id}] game run: {len(phases)} traces ({', '.join(n for n, _ in phases)})")
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        (out_dir / "auto-run.out").write_text(r.stdout + r.stderr)
    finally:
        lp.terminate()
    ok = "Run ended: done" in r.stdout
    m = re.search(r"Run mode: (\w+)", r.stdout)
    mode = m.group(1) if m else "?"
    cap_re = rf"\d{{8}}-\d{{6}}-{re.escape(tag)}" if tag else r"\d{8}-\d{6}"
    caps = sorted(p for p in CAPS.iterdir() if re.fullmatch(cap_re, p.name) and p.stat().st_mtime >= t0 - 1
                  and (p / "done.txt").exists())
    if marks.exists():
        shutil.copy(marks, out_dir / "phases.log")
    log(f"[{scene_id}] run {'done' if ok else 'INCOMPLETE'} in {time.time() - t0:.0f} s, {len(caps)} traces ({mode})")
    traces = {}
    raw = out_dir / "raw"
    raw.mkdir(exist_ok=True)
    for i, (cap, (name, _)) in enumerate(zip(caps, phases)):
        dst = raw / f"{i:02d}_{name}"
        dst.mkdir(exist_ok=True)
        shutil.move(str(cap / "dumps.json"), dst / "dumps.json")
        shutil.move(str(cap / "img"), dst / "img")
        if name == "on0" and (cap / "frame.json").exists():   # small; draws and shaders for root-causing
            shutil.move(str(cap / "frame.json"), out_dir / "frame_on0.json")
        shutil.rmtree(cap, ignore_errors=True)   # frame.json, shaders: not needed
        traces[name] = dst.name
    for cap in caps[len(phases):]:
        shutil.rmtree(cap, ignore_errors=True)
    meta = {"scene": scene_id, "save": save, "ok": ok and len(caps) == len(phases), "full_res": full_res,
            "phases": [[n, s] for n, s in phases], "traces": traces, "run_s": round(time.time() - t0, 1),
            "run_mode": mode, "exclusive": mode == "exclusive", "log_tail": _log_lines(tag)}
    (out_dir / "run.json").write_text(json.dumps(meta, indent=1))
    return meta


def _log_lines(tag=""):
    """Warnings from the layer log worth keeping next to the report."""
    log = ROOT / (f"bg3rtx.{tag}.log" if tag else "bg3rtx.log")
    if not log.exists():
        return []
    keep = []
    for line in log.read_text(errors="replace").splitlines():
        if re.search(r"WARNING|error|non-finite|\[geo\] RT capture|alpha|\[phase\]|sun stats|^\S+\s+sun:|rt passes|trace pass|sharc",
                     line, re.I):
            keep.append(line.strip()[:300])
    return keep[-60:]
