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
    # The traced sun replacing the game's mask where trusted, against min(game, traced).
    "rt_sun_replace": ("rep_on", "sun_min"),
    "rt_local_shadows": ("on0", "local_off"),
    "all_rt": ("on0", "off0"),
}


def scenes():
    """Scene id -> save substring, from scripts/scene-sweep.ps1 (the single list)."""
    text = (ROOT / "scripts" / "scene-sweep.ps1").read_text(encoding="utf-8")
    return dict(re.findall(r'(S\d+) = "([^"]+)"', text))


def plan(temporal=3, reference=False):
    """The ordered traces of one run: (name, settings applied before it)."""
    p = [("on0", {})]
    p += [(f"on{i}", {}) for i in range(1, temporal)]
    p += [("gi_off", {"gi": "0", "rt_reflections": "0"}),
          ("refl_off", {"gi": "1", "rt_reflections": "0"}),
          ("sun_off", {"rt_reflections": "1", "rt_shadows": "0"}),
          ("local_off", {"rt_shadows": "1", "rt_local_shadows": "0"}),
          # rt_sun_replace against min(game, traced): three consecutive traces late in the
          # run (the first traces can still see streaming and a settling camera), compared
          # with each other rather than with on0 (analyze.py).
          ("rep_on", {"rt_local_shadows": "1"}),
          ("sun_min", {"rt_sun_replace": "0"}),
          # rt_debug=2: game mask in red, traced sun visibility in green, the visibility
          # written into the mask (rt_sun_replace) in blue, over the lit diffuse.
          # rt_backlit=0 leaves untraced (back-facing) pixels at visibility 0, so
          # "traced lit" always means front-facing and really unoccluded.
          ("sun_dbg", {"rt_sun_replace": "1", "rt_debug": "2", "rt_backlit": "0"}),
          # rt_debug=6: the trust of rt_sun_replace (red roof volume, magenta far from the
          # game's lit mask, blue off-screen, green trusted).
          ("sun_trust", {"rt_debug": "6", "rt_backlit": "1"})]
    if reference:
        # More samples for the stochastic terms, same denoiser (RR) and view.
        p += [("ref", {"rt_debug": "0", "rt_backlit": "1", "gi_rr_rays": "8", "ls_samples": "2"})]
    first_off = dict(ALL_OFF, rt_backlit="1", gi_rr_rays="1", ls_samples="0") if reference else dict(ALL_OFF, rt_backlit="1")
    p += [("off0", first_off)]
    p += [(f"off{i}", {}) for i in range(1, temporal)]
    return p


def run_scene(scene_id, save, out_dir: Path, full_res=False, temporal=3, reference=False,
              interval=240, timeout=900, raw=False, log=print):
    """Runs the game once and moves each trace's dumps.json + img/ to out_dir/raw/<i>_<name>/."""
    phases = plan(temporal, reference)
    out_dir.mkdir(parents=True, exist_ok=True)
    spec = {"start": r"\[trace\] frame \d+ written", "phases": [], "timeout": timeout + 3600,
            "after_trace": [{"name": n, "set": s} for n, s in phases[1:]]}
    spec_path = out_dir / "phases.json"
    spec_path.write_text(json.dumps(spec, indent=1))
    marks = CAPS / "phases.log"
    marks.unlink(missing_ok=True)

    sets = [f"{k}={v}" for k, v in ALL_ON.items()]
    sets += [f"auto_interval={interval}", f"dump_png_width={3840 if full_res else 1280}"]
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
        log(f"[{scene_id}] game run: {len(phases)} traces ({', '.join(n for n, _ in phases)})")
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        (out_dir / "auto-run.out").write_text(r.stdout + r.stderr)
    finally:
        lp.terminate()
    ok = "Run ended: done" in r.stdout
    caps = sorted(p for p in CAPS.iterdir() if re.fullmatch(r"\d{8}-\d{6}", p.name) and p.stat().st_mtime >= t0 - 1
                  and (p / "done.txt").exists())
    if marks.exists():
        shutil.copy(marks, out_dir / "phases.log")
    log(f"[{scene_id}] run {'done' if ok else 'INCOMPLETE'} in {time.time() - t0:.0f} s, {len(caps)} traces")
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
            "log_tail": _log_lines()}
    (out_dir / "run.json").write_text(json.dumps(meta, indent=1))
    return meta


def _log_lines():
    """Warnings from the layer log worth keeping next to the report."""
    log = ROOT / "bg3rtx.log"
    if not log.exists():
        return []
    keep = []
    for line in log.read_text(errors="replace").splitlines():
        if re.search(r"WARNING|error|non-finite|\[geo\] RT capture|alpha|\[phase\]|sun stats|^\S+\s+sun:|rt passes|trace pass", line, re.I):
            keep.append(line.strip()[:300])
    return keep[-60:]
