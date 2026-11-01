"""Every-window gates (scripts/queue_gates.py): one good window must not hide bad ones.

    py -3 tests/queue_gates_test.py

The synthetic run below follows the m4i 093913 log: clip lines every ~4.4 s while the key is
on; the ON window shows 2.63%, 3.14% and 0.00% (the old "expect" passed on the 0.00% line).
"""
import json
import subprocess
import sys
import tempfile
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
              all(re.compile(g["select"]) and re.compile(g["match"]) and set(g.get("windows") or []) <= labels for g in ee))
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

    if fails:
        print(f"{fails} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
