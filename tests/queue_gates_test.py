"""Every-window gates (scripts/queue_gates.py): one good window must not hide bad ones.

    py -3 tests/queue_gates_test.py

The synthetic run below follows the m4i 093913 log: clip lines every ~4.4 s while the key is
on; the ON window shows 2.63%, 3.14% and 0.00% (the old "expect" passed on the 0.00% line).
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import queue_gates  # noqa: E402

PLAN = [{"label": "warmup-off"}, {"label": "diag-on"}, {"label": "diag-off-b"}, {"label": "cost-on"}]
RUNNER = "\n".join([
    "Game pid 1",
    "[motion-plan] pointer centred in owned foreground game",
    "09:40:35.337 [pan-plan] window 0 serial 0 set=key=0",
    "09:40:50.338 [pan-plan] window 1 serial 0 set=key=1",
    "120s [foreground: bg3]: 09:41:00.000  [watch] presents 120",
    "09:41:05.342 [pan-plan] window 2 serial 0 set=key=0",
    "23:59:59.000 [pan-plan] window 3 serial 0 set=key=1",
    "00:00:30.000 [motion-plan] complete phases=4",
    "Run ended: done",
])
SEL = r"instanced clip \(\d+ frames\)"
OK = r"rejected \d+ = 0\.0[0-3]\d\d%"


def clip(t, pct):
    return f"{t}  [core] hit material instanced clip (120 frames): wind: checked 9; rejected 5 = {pct}% of all lookups"


def log(on_values, cost_values=()):
    lines = ["09:39:18.795  [cfg] settings read: 231 keys", clip("09:40:46.000", "0.0000"),
             "09:40:50.365  [cfg] settings read: 231 keys"]
    t = 52.971
    for v in on_values:
        lines.append(clip(f"09:40:{t:06.3f}" if t < 60 else f"09:41:{t - 60:06.3f}", v))
        t += 4.4
    lines.append("09:41:05.369  [cfg] settings read: 231 keys")
    lines.append(clip("09:41:06.168", "0.3928"))  # straddles diag-on / diag-off-b: never judged
    lines.append("23:59:59.030  [cfg] settings read: 231 keys")
    stamps = ["23:59:59.900", "00:00:03.000", "00:00:06.000"]
    for s, v in zip(stamps, cost_values):
        lines.append(clip(s, v))
    return "\n".join(lines)


def gate(**kw):
    g = {"select": SEL, "match": OK}
    g.update(kw)
    return [g]


def main():
    fails = 0

    def check(what, ok):
        nonlocal fails
        print(("PASS " if ok else "FAIL ") + what)
        fails += 0 if ok else 1

    bad_run = log(["2.6286", "3.1362", "0.0000"])
    import re
    wf9 = json.loads((ROOT / "tools/live_batches/water_flicker_wf9/jobs/wf9-s8-compose-shift.json").read_text())
    static_camera_gate = next(g for g in wf9["expect_every"] if g["name"] == "static build-camera delta < 0.01 m")
    compose_cpu_suffix = ("; CPU hit shade selected spec 0/1/2 passes 120/0/0, flag 64 passes 0; "
                          "refused light-binding/input-lifetime/scene-material/frame-shift 120/120/0/0 "
                          "(overlapping, first light mismatch index 2, bindings 22..28)")
    check("wf9 checks both static ON windows", static_camera_gate["windows"] == ["energy-static-1", "energy-static-3"])
    for delta, samples, expected in [("0.000000", 120, True), ("0.009999", 120, True),
                                      ("0.010000", 120, False), ("294.800000", 120, False), ("0.000000", 0, False)]:
        line = f"[core] [water] compose (120 frames): build-camera delta avg/max {delta}/294.800000 m ({samples} last-pass samples)"
        for suffix in ("", compose_cpu_suffix):
            check(f"wf9 static delta {delta}, samples {samples}, CPU fields {bool(suffix)}",
                  bool(re.search(static_camera_gate["select"], line + suffix)) and
                  bool(re.search(static_camera_gate["match"], line + suffix)) == expected)
    wo1 = json.loads((ROOT / "tools/live_batches/water_offscreen/jobs/wo1-s8-hit-shade.json").read_text())
    hit_line = ("[core] [water] compose (120 frames): hit shade attempted 100, hidden 80, offscreen 20, "
                "rejected material/light/sun/invalid 0/0/0/0; world truncated 0" + compose_cpu_suffix)
    for g in wo1["expect_every"]:
        if "hit shade attempted" in g["match"] or "world truncated" in g["match"]:
            if re.search(g["select"], hit_line):
                check("wo1 appended CPU fields: " + g["name"], bool(re.search(g["match"], hit_line)))
    check("the old any-line expect passes the bad run (the bug)", bool(re.search(OK, bad_run)))
    f = queue_gates.check(gate(windows=["diag-on"]), bad_run, RUNNER, PLAN)
    check("every-window gate fails the bad run", len(f) == 1 and "1 of 2 line(s) fail" in f[0] and "3.1362" in f[0])
    good = log(["2.6286", "0.0100", "0.0000"])
    check("first ON line straddles the toggle and is skipped, later good lines pass",
          queue_gates.check(gate(windows=["diag-on"]), good, RUNNER, PLAN) == [])
    check("the straddling 0.39% line in diag-off-b is not judged for diag-off-b either",
          queue_gates.check(gate(windows=["diag-off-b"], min=0), good, RUNNER, PLAN) == [])
    f = queue_gates.check(gate(windows=["diag-on"], min=3), good, RUNNER, PLAN)
    check("min counts complete lines per window", len(f) == 1 and "has 2 complete line(s), need 3" in f[0])
    f = queue_gates.check(gate(windows=["no-such"]), good, RUNNER, PLAN)
    check("an unknown window label fails", len(f) == 1 and "not found" in f[0])
    f = queue_gates.check(gate(windows=["diag-on"]), good, "Run ended: done", PLAN)
    check("a runner log without [pan-plan] lines fails (no windows)", len(f) == 1 and "not found" in f[0])
    across = log(["0.0000", "0.0000"], ["0.0000", "0.0000", "1.5000"])
    f = queue_gates.check(gate(windows=["cost-on"]), across, RUNNER, PLAN)
    check("a window across midnight is judged (rollover)", len(f) == 1 and "1 of 2 line(s) fail" in f[0])
    f = queue_gates.check(gate(), bad_run, RUNNER, PLAN)
    check("without windows every selected line of the run is judged", len(f) == 1 and "3 of 5 line(s) fail" in f[0])
    f = queue_gates.check(gate(select="never printed"), bad_run, RUNNER, PLAN)
    check("without windows a missing line fails", len(f) == 1 and "0 line(s) in the run" in f[0])

    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        # With a BOM, as Windows PowerShell 5 (test-queue.ps1) writes it.
        (d / "g.json").write_text(json.dumps(gate(windows=["diag-on"])), encoding="utf-8-sig")
        (d / "run.log").write_text(bad_run, encoding="utf-8")
        (d / "runner.txt").write_text(RUNNER, encoding="utf-8")
        (d / "plan.json").write_text(json.dumps(PLAN), encoding="utf-8")
        base = [sys.executable, str(ROOT / "scripts" / "queue_gates.py"), "--gates", str(d / "g.json"),
                "--runner", str(d / "runner.txt"), "--plan", str(d / "plan.json"), "--log"]
        p = subprocess.run(base + [str(d / "run.log")], capture_output=True, text=True)
        check("CLI exits 1 on a failing gate", p.returncode == 1 and "every-window" in p.stdout)
        (d / "run.log").write_text(good, encoding="utf-8")
        p = subprocess.run(base + [str(d / "run.log")], capture_output=True, text=True)
        check("CLI exits 0 when every window holds", p.returncode == 0 and "ok" in p.stdout)

    # Events: one-off lines (a trace per shot window) are counted from the applied settings to the
    # window's end, without the straddle rule (the first and only trace line of a window counts).
    ev_plan = [{"label": "shot-off-a"}, {"label": "shot-on"}, {"label": "shot-off-b"}]
    ev_runner = "\n".join(["10:00:00.000 [pan-plan] window 0 serial 0 set=k=0", "10:00:06.000 [pan-plan] window 1 serial 0 set=k=1",
                           "10:00:12.000 [pan-plan] window 2 serial 0 set=k=0", "10:00:18.000 [motion-plan] complete phases=3"])
    ev_log = "\n".join(["10:00:00.020  [cfg] settings read", "10:00:04.100  [trace] frame 1 written: 4 submits -> a",
                        "10:00:06.020  [cfg] settings read", "10:00:12.020  [cfg] settings read",
                        "10:00:16.100  [trace] frame 3 written: 4 submits -> c"])
    tr = {"select": r"\[trace\] frame \d+ written: ", "match": r"\[trace\] frame \d+ written: ", "event": True,
          "windows": ["shot-off-a", "shot-on", "shot-off-b"]}
    f = queue_gates.check([tr], ev_log, ev_runner, ev_plan)
    check("event gate: the shot window without a trace fails, the others count their single line",
          len(f) == 1 and "'shot-on'" in f[0] and "0 event line(s)" in f[0])
    f = queue_gates.check([dict(tr, event=False)], ev_log, ev_runner, ev_plan)
    check("without event, the straddle rule drops a window's only line (why trace gates need event)", len(f) == 3)
    # settle_s: a snapshot printed 28 ms after the read (hig1 09:54 frame 1679, a query recorded before it) is skipped.
    sn_log = "\n".join(["10:00:00.020  [cfg] settings read", "10:00:00.048  snap rows 5", "10:00:04.100  snap rows 0",
                        "10:00:06.020  [cfg] settings read", "10:00:06.050  snap rows 0", "10:00:12.020  [cfg] settings read",
                        "10:00:15.000  snap rows 0"])
    sn = {"select": r"snap rows", "match": r"rows 0", "event": True, "windows": ["shot-off-a", "shot-on", "shot-off-b"]}
    f = queue_gates.check([sn], sn_log, ev_runner, ev_plan)
    check("event gate without settle_s judges the stale snapshot (fails)", any("'shot-off-a'" in x and "fail" in x for x in f))
    f = queue_gates.check([dict(sn, settle_s=1.0)], sn_log, ev_runner, ev_plan)
    check("settle_s skips the snapshots within 1 s of the read: shot-on keeps none, the others pass",
          len(f) == 1 and "'shot-on'" in f[0] and "0 event line(s)" in f[0])

    # Plan drift: the runner says which keys each window applied; a plan edited after the run fails.
    drift_plan = [{"label": "shot-off-a", "set": "k=0"}, {"label": "shot-on", "set": "k=1,hit_ao_rays=2"}, {"label": "shot-off-b", "set": "k=0"}]
    f = queue_gates.check([tr], ev_log, ev_runner, drift_plan)
    check("plan drift fails the windowed gates instead of judging shifted windows", len(f) == 1 and f[0].startswith("plan drift: 1 window"))
    same_plan = [{"label": "shot-off-a", "set": "k=0"}, {"label": "shot-on", "set": "k=1"}, {"label": "shot-off-b", "set": "k=0"}]
    f = queue_gates.check([tr], ev_log, ev_runner, same_plan)
    check("matching sets: no drift", len(f) == 1 and "plan drift" not in f[0])

    jobs = sorted((ROOT / "tools" / "live_batches").glob("*/jobs/*.json"))
    with_ee = 0
    for jf in jobs:
        spec = json.loads(jf.read_text(encoding="utf-8"))
        ee = spec.get("expect_every") or []
        if not ee:
            continue
        with_ee += 1
        plan = json.loads((jf.parent.parent / "plans" /
                           Path(spec["args"][spec["args"].index("-MovePlan") + 1]).name).read_text(encoding="utf-8"))
        labels = {p["label"] for p in plan}
        check(f"{jf.name}: expect_every regexes compile, windows are plan labels",
              all(re.compile(g["select"]) and (re.compile(g["value"]) if g.get("trend") else re.compile(g["match"]))
                  and set(g.get("windows") or []) | set(g.get("baseline") or []) | set(g.get("tail") or []) <= labels
                  for g in ee))
    check(f"jobs with expect_every: {with_ee} (m4i, m4p1-4, m5w3-6 at least)", with_ee >= 9)

    # Exposure hold lint: an energy-gated plan (ON shot between two OFF shots) must hold the eye adaptation.
    def ph(label, hold=None):
        return {"label": label, "set": "k=1" + ("" if hold is None else f",qa_exposure_hold={hold}")}
    held_job = {"expect": [r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+"]}
    good = [ph("warmup", 0), ph("cost-off-a", 1), ph("shot-off-a", 1), ph("shot-on", 1), ph("cost-off-b", 1), ph("shot-off-b", 1), ph("restore", 0)]
    check("hold lint: held from cost-off-a to the last shot, released in restore: ok", queue_gates.exposure_hold_problems(good, held_job) == [])
    unheld = [ph("warmup"), ph("shot-off-a"), ph("shot-on"), ph("shot-off-b"), ph("restore")]
    p = queue_gates.exposure_hold_problems(unheld, held_job)
    check("hold lint: unheld OFF/ON/OFF shots flagged (gf2 S8 2026-11-01: +2.87% raw was -0.32%)", len(p) == 1 and "shot-off-a, shot-on, shot-off-b" in p[0])
    gap = [dict(x) for x in good]
    gap[4] = ph("cost-off-b", 0)   # a release and a new latch between the shots holds another EV
    p = queue_gates.exposure_hold_problems(gap, held_job)
    check("hold lint: a release between the compared shots is flagged", len(p) == 1 and "cost-off-b" in p[0])
    first = [ph("shot-off-a", 1), ph("shot-on", 1), ph("shot-off-b", 1)]
    check("hold lint: a hold in phase 0 (no warm-up exposure to latch) is flagged",
          any("phase 0" in x for x in queue_gates.exposure_hold_problems(first, held_job)))
    check("hold lint: held plan but no 'exposure held' expect line is flagged",
          any("expect" in x for x in queue_gates.exposure_hold_problems(good, {"expect": []})))
    single = [ph("warmup"), ph("shot-rt-off"), ph("shot-rt-on"), ph("restore")]
    check("hold lint: one OFF shot (lean_regression RT pair) is no energy gate", queue_gates.exposure_hold_problems(single, {}) == [])
    bad = queue_gates.lint_batches(ROOT / "tools" / "live_batches")
    check("hold lint: every energy-gated job under tools/live_batches holds the exposure"
          + ("" if not bad else ": " + "; ".join(f"{Path(k).name}: {v[0][:90]}" for k, v in bad.items())), not bad)

    # Scene-wait lint: every lean job gates "[rt] scene wait" with 0 timeouts in its traced cost windows.
    sw_plan = [{"label": "warmup", "seconds": 20}, {"label": "cost-off-a", "seconds": 20, "set": "k=0"},
               {"label": "cost-on", "seconds": 20, "set": "k=1"}, {"label": "cost-rt-off", "seconds": 20, "set": "rt=0"},
               {"label": "shot-on", "seconds": 8, "set": "rt=1"}, {"label": "restore", "seconds": 5}]
    sw_gate = dict(queue_gates.SCENE_WAIT_GATE, windows=["cost-off-a", "cost-on"])
    sw_job = {"set": "rt=1,rt_scene_wait_log_frames=120", "expect": [r"\[rt\] scene wait: 120 frames"], "expect_every": [sw_gate]}
    check("scene wait lint: traced cost windows only (rt=0 and shot windows excluded)",
          queue_gates.scene_wait_windows(sw_job, sw_plan) == ["cost-off-a", "cost-on"])
    check("scene wait lint: a cost label with float traces is excluded",
          queue_gates.scene_wait_windows(sw_job, sw_plan + [dict(label="cost-capture", trace_at=[1], set="rt=1")])
          == ["cost-off-a", "cost-on"])
    check("capacity lint: cost windows with float traces still need capacity gates",
          bool(queue_gates.capacity_problems(sw_job, [dict(label="cost-capture", trace_at=[1], seconds=20)])))
    from tools.live_batches.pipeline_readiness import CONTROL
    for key in sorted(k for k in CONTROL if k.endswith(('_stats', '_check', '_debug'))):
        diagnostic_plan = [dict(label='cost-diag', seconds=20),
                           dict(label='cost-clean', seconds=20, set=key + '=0')]
        diagnostic_job = dict(sw_job, set=sw_job['set'] + ',' + key + '=1')
        check('scene wait lint: inherited ' + key + ' excluded until disabled',
              queue_gates.scene_wait_windows(diagnostic_job, diagnostic_plan) == ['cost-clean'])
        check('capacity lint: ' + key + ' does not exclude capacity gates',
              queue_gates.scene_wait_windows(diagnostic_job, diagnostic_plan, include_traces=True)
              == ['cost-diag', 'cost-clean'])
    check("scene wait lint: the canonical gate over both cost windows: ok", queue_gates.scene_wait_problems(sw_job, sw_plan) == [])
    p = queue_gates.scene_wait_problems(dict(sw_job, expect_every=[]), sw_plan)
    check("scene wait lint: a lean job without the gate is flagged", len(p) == 1 and "no expect_every entry" in p[0])
    p = queue_gates.scene_wait_problems(dict(sw_job, expect_every=[dict(sw_gate, windows=["cost-on"])]), sw_plan)
    check("scene wait lint: a gate missing a cost window is flagged", len(p) == 1 and "cost-off-a" in p[0])
    loose = dict(sw_gate, match=r"got this frame's scene")
    check("scene wait lint: a match that passes a line with timeouts is no gate",
          any("no expect_every entry" in x for x in queue_gates.scene_wait_problems(dict(sw_job, expect_every=[loose]), sw_plan)))
    check("scene wait lint: a gate without windows (every line of the run) covers all",
          queue_gates.scene_wait_problems(dict(sw_job, expect_every=[dict(queue_gates.SCENE_WAIT_GATE)]), sw_plan) == [])
    p = queue_gates.scene_wait_problems(dict(sw_job, set="rt=1", expect=[r"\[rt\] scene wait: 600 frames"]), sw_plan)
    check("scene wait lint: 600-frame lines cannot fill a 20 s cost window at 30 fps (two lines + settle = 41 s)",
          len(p) == 1 and "rt_scene_wait_log_frames=120" in p[0])
    p = queue_gates.scene_wait_problems(dict(sw_job, expect=[r"\[rt\] scene wait: 600 frames"]), sw_plan)
    check("scene wait lint: an expect naming another cadence is flagged", len(p) == 1 and "600 frames" in p[0])
    check("scene wait lint: a plan without cost windows is no lean job", queue_gates.scene_wait_problems({"set": "rt=1"}, [{"label": "diag-a", "seconds": 15}]) == [])
    # The gate itself on a run: 120-frame lines (~2 s at 60 fps), straddle rule as for any stats line.
    sw_runner = "\n".join(["10:00:00.000 [pan-plan] window 0 serial 0", "10:00:20.000 [pan-plan] window 1 serial 0 set=k=0",
                           "10:00:40.000 [pan-plan] window 2 serial 0 set=k=1", "10:01:00.000 [pan-plan] window 3 serial 0 set=rt=0",
                           "10:01:20.000 [motion-plan] complete phases=4"])
    def sw(t, timeouts):
        return (f"{t}  [rt] scene wait: 120 frames, 120 waited ({120 - timeouts} got this frame's scene, {timeouts} timeouts, "
                f"0 skipped), wait mean 2.0 ms, max 6.0 ms")
    def sw_log(on_timeouts):
        lines = ["10:00:01.000  [cfg] settings read", sw("10:00:10.000", 14), "10:00:20.100  [cfg] settings read"]
        lines += [sw(f"10:00:{s:02d}.000", 0) for s in range(22, 40, 2)]
        lines += ["10:00:40.100  [cfg] settings read", sw("10:00:41.000", 3)]   # straddles the toggle: not judged
        lines += [sw(f"10:00:{s:02d}.000", t) for s, t in zip(range(43, 60, 2), on_timeouts)]
        return "\n".join(lines)
    gates = [dict(queue_gates.SCENE_WAIT_GATE, windows=["cost-off-a", "cost-on"])]
    plan4 = [{"label": "warmup"}, {"label": "cost-off-a", "set": "k=0"}, {"label": "cost-on", "set": "k=1"}, {"label": "cost-rt-off", "set": "rt=0"}]
    check("scene wait gate: 0 timeouts in every judged line of both windows passes (the load and toggle lines are not judged)",
          queue_gates.check(gates, sw_log([0] * 9), sw_runner, plan4) == [])
    f = queue_gates.check(gates, sw_log([0, 0, 0, 0, 2, 0, 0, 0, 0]), sw_runner, plan4)
    check("scene wait gate: one line with 2 timeouts in cost-on fails it", len(f) == 1 and "cost-on" in f[0] and "1 of " in f[0])
    bad = queue_gates.lint_scene_wait(ROOT / "tools" / "live_batches")
    check("scene wait lint: every lean job under tools/live_batches gates the scene wait"
          + ("" if not bad else ": " + "; ".join(f"{Path(k).name}: {v[0][:90]}" for k, v in bad.items())), not bad)

    # Capacity lint: the RT capture and (with the casters on) the cascade buffer gates over the traced cost windows.
    cap = [g for g, _, _, _ in queue_gates.CAPACITY_GATES]
    for g, _, good, badline in queue_gates.CAPACITY_GATES:
        check(f"capacity gate '{g['select']}': selects both sample lines, passes the good one, fails the bad one",
              bool(re.search(g["select"], good) and re.search(g["select"], badline) and re.search(g["match"], good)
                   and not re.search(g["match"], badline)))
    cj = {"set": "rt=1,rt_cascade_casters=0", "expect_every": [dict(cap[0], windows=["cost-off-a", "cost-on"])]}
    check("capacity lint: casters off: the RT capture gate alone over both cost windows is ok", queue_gates.capacity_problems(cj, sw_plan) == [])
    p = queue_gates.capacity_problems(dict(cj, set="rt=1,rt_cascade_casters=1"), sw_plan)
    check("capacity lint: casters on without the cascade gate is flagged", len(p) == 1 and "cascade" in p[0])
    p = queue_gates.capacity_problems(dict(cj, set="rt=1,rt_cascade_casters=1", expect_every=[
        dict(cap[0], windows=["cost-off-a", "cost-on"]),
        {k: v for k, v in cap[1].items() if k != "when_present"} | {"windows": ["cost-off-a", "cost-on"]}]), sw_plan)
    check("capacity lint: a cascade gate without when_present is flagged (an interior has no cascade line)",
          len(p) == 1 and "cascade" in p[0])
    # when_present (m5p2 Flymm 11-01): a window without the pass's line passes; with it, the gate judges as usual.
    absent = gate(select=r"cascade\] casters: nothing", windows=["diag-on"])
    check("when_present: a window without any selected line fails without the flag ...",
          len(queue_gates.check(absent, bad_run, RUNNER, PLAN)) == 1)
    check("... and passes with it", queue_gates.check([dict(absent[0], when_present=True)], bad_run, RUNNER, PLAN) == [])
    f = queue_gates.check(gate(windows=["diag-on"], when_present=True), bad_run, RUNNER, PLAN)
    check("when_present: present lines are still judged (the bad window fails)", len(f) == 1 and "3.1362" in f[0])
    p = queue_gates.capacity_problems(dict(cj, expect_every=[]), sw_plan)
    check("capacity lint: no gate is flagged", len(p) == 1 and "cost-off-a, cost-on" in p[0])
    p = queue_gates.capacity_problems(dict(cj, expect_every=[dict(cap[0], match="opaque draws dropped", windows=["cost-on", "cost-off-a"])]), sw_plan)
    check("capacity lint: a match that passes a full buffer is no gate", len(p) == 1)
    check("capacity lint: a plan without cost windows is no lean job", queue_gates.capacity_problems({"set": "rt=1"}, [{"label": "diag-a"}]) == [])
    bad = queue_gates.lint_scene_wait(ROOT / "tools" / "live_batches", queue_gates.capacity_problems)
    check("capacity lint: every lean job under tools/live_batches gates the RT capture (and the cascade with the casters on)"
          + ("" if not bad else ": " + "; ".join(f"{Path(k).name}: {v[0][:90]}" for k, v in bad.items())), not bad)

    # Trend gates (long sessions): early windows against late ones, noise = the baseline's own spread.
    tplan = [{"label": f"cost-c{i}"} for i in range(1, 5)]
    trunner = "\n".join(["Game pid 1"] + [f"10:0{i}:00.000 [pan-plan] window {i} serial 0" for i in range(4)]
                        + ["10:04:00.000 [motion-plan] complete phases=4", "Run ended: done"])

    def tlog(values):   # 3 [mem] lines per window
        lines = []
        for i in range(4):
            lines.append(f"10:0{i}:00.100  [cfg] settings read: 1 keys")
            for j in range(3):
                lines.append(f"10:0{i}:{10 + 15 * j:02d}.000  [mem] device-local VRAM of this process: used {values[i][j]} MB, budget 30000 MB")
        return "\n".join(lines)

    tg = [{"name": "vram", "trend": True, "select": r"\[mem\] device-local", "value": r"used ([0-9.]+) MB",
           "baseline": ["cost-c1", "cost-c2"], "tail": ["cost-c3", "cost-c4"], "abs": 1}]
    flat = tlog([[9000, 9010, 9005], [9008, 9002, 9012], [9011, 9004, 9009], [9003, 9010, 9007]])
    check("trend: a flat series within its own noise passes", queue_gates.check(tg, flat, trunner, tplan) == [])
    grow = tlog([[9000, 9010, 9005], [9050, 9100, 9150], [9200, 9250, 9300], [9350, 9400, 9450]])
    f = queue_gates.check(tg, grow, trunner, tplan)
    check("trend: a growing series fails with its numbers", len(f) == 1 and "grew" in f[0] and "9325" in f[0])
    f = queue_gates.check([dict(tg[0], tail=["cost-c9"])], flat, trunner, tplan)
    check("trend: a missing window is reported, not passed", len(f) >= 1 and "not in the runner log" in f[0])
    f = queue_gates.check([dict(tg[0], min=7)], flat, trunner, tplan)
    check("trend: too few lines on a side is a failure", len(f) == 2 and "need 7" in f[0])
    check("trend: rel allowance scales with the baseline",
          queue_gates.check([dict(tg[0], abs=0, rel=0.1)], grow, trunner, tplan) == [])

    if fails:
        print(f"{fails} check(s) failed")
        return 1
    print("all checks passed")
    return 0


class MidnightTests(unittest.TestCase):
    def test_shared_origin_with_different_first_stamps(self):
        # Both stream orderings around midnight share one day origin. Runner preamble
        # stamps count even when the first phase starts after midnight.
        for runner_text, game_text in [
            ("23:59:58.000 launch\n00:00:01.000 [pan-plan] window 0\n00:00:06.000 [motion-plan] complete",
             "00:00:02.000 [cfg] settings read\n00:00:03.000 event ok"),
            ("00:00:01.000 [pan-plan] window 0\n00:00:06.000 [motion-plan] complete",
             "23:59:59.000 preamble\n00:00:02.000 [cfg] settings read\n00:00:03.000 event ok"),
            ("23:59:59.000 [pan-plan] window 0\n00:00:06.000 [motion-plan] complete",
             "00:00:02.000 [cfg] settings read\n00:00:03.000 event ok"),
        ]:
            self.assertFalse(queue_gates.check([dict(select="event", match="ok", event=True, windows=["cost"])],
                                         game_text, runner_text, [dict(label="cost")]))




class ResetPeriodTests(unittest.TestCase):
    def setUp(self):
        self.plan = [dict(label='on', stats_reset=True, wait_log=['ready'])]
        self.runner = ('10:00:00.000 [pan-plan] window 0\n'
                       '10:00:02.100 [motion-ready] phase=0 stats_reset=7 frame=100\n'
                       '10:00:05.000 [motion-plan] complete')
        self.log = ('10:00:00.100 [cfg] settings read\n'
                    '10:00:02.000 [stats] reset token=7 frame=100\n'
                    '10:00:04.000 coverage swaps 1\n'
                    '10:00:04.001 cost good')
        self.gate = dict(select='cost ', match='good', windows=['on'])

    def test_first_complete_period_after_ack_counts(self):
        self.assertEqual(queue_gates.check([self.gate], self.log, self.runner, self.plan), [])
        self.assertTrue(queue_gates.check([dict(self.gate, min=2)], self.log, self.runner, self.plan))
        self.assertTrue(queue_gates.check([self.gate], self.log.replace('cost good','cost bad'), self.runner, self.plan))

    def test_runner_poll_delay_does_not_drop_acknowledged_complete_period(self):
        late_runner=self.runner.replace('10:00:02.100 [motion-ready]', '10:00:04.500 [motion-ready]')
        self.assertEqual(queue_gates.check([self.gate], self.log, late_runner, self.plan), [])

    def test_missing_stale_or_wrong_ack_cannot_pass(self):
        for log in [self.log.replace('token=7','token=8'),
                    self.log.replace('10:00:02.000 [stats]', '09:59:59.000 [stats]')]:
            self.assertTrue(queue_gates.check([self.gate], log, self.runner, self.plan))
        legacy = [dict(label='on', wait_log=['ready'])]
        self.assertTrue(queue_gates.check([self.gate], self.log, self.runner.replace(' stats_reset=7 frame=100',''), legacy))

    def test_first_complete_trigger_after_reset_is_required(self):
        gate = dict(self.gate, required_if=dict(select='coverage ',match='swaps 1'))
        self.assertTrue(queue_gates.check([gate], self.log.replace('cost good','cost bad'), self.runner, self.plan))


class NumericItemsTests(unittest.TestCase):
    def test_all_items_and_safe_fallback(self):
        gate = dict(match='coverage', items=dict(pattern=r'x:(?P<value>[0-9.]+),(?P<active>\d+)', bound=r'bound ([0-9.]+)'))
        for line, expected in [('coverage x:0.1,0 x:0.9,20 bound 0.8', True),
                               ('coverage x:0.9,20 x:0.1,1 bound 0.8', False),
                               ('coverage x:0.8,1 bound 0.8', True),
                               ('coverage bound 0.8', False)]:
            self.assertEqual(queue_gates.line_matches(gate,line), expected)

    def test_items_upper_bound(self):
        gate = dict(match='coverage', items=dict(pattern=r'x:(?P<value>[0-9.]+),(?P<active>\d+)', bound=r'bound ([0-9.]+),',
                                                 upper=r'upper ([0-9.]+),'))
        for line, expected in [('coverage x:1.0,3 bound 0.9, upper 1.1,', True),
                               ('coverage x:2.55,150 bound 0.9, upper 1.1,', False),
                               ('coverage x:2.55,0 bound 0.9, upper 1.1,', True),
                               ('coverage x:1.0,3 bound 0.9,', False)]:
            self.assertEqual(queue_gates.line_matches(gate, line), expected, line)

    def test_required_if_uses_complete_trigger_not_straddle(self):
        plan=[dict(label='on')]
        runner='10:00:00.000 [pan-plan] window 0\n10:00:30.000 [motion-plan] complete'
        gate=dict(select='cost', match='cost ok', min=1, windows=['on'], required_if=dict(select='coverage',match='swaps 1'))
        log='09:59:59.000 coverage swaps 1\n10:00:00.100 [cfg] settings read\n10:00:02.000 coverage swaps 0'
        self.assertFalse(queue_gates.check([gate],log,runner,plan))
        self.assertTrue(queue_gates.check([gate],log+'\n10:00:12.000 coverage swaps 1',runner,plan))
        log+='\n10:00:12.000 coverage swaps 1\n10:00:13.000 cost ok\n10:00:23.000 cost ok'
        self.assertFalse(queue_gates.check([gate],log,runner,plan))


if __name__ == "__main__":
    code = main()
    if code:
        sys.exit(code)
    unittest.main()
