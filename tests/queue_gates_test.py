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

    for batch, job in (("m4_instanced", "m4i-instanced-s14-fg2.json"), ("m5_water", "m5w3-water-compose-s8-fg2.json")):
        spec = json.loads((ROOT / "tools" / "live_batches" / batch / "jobs" / job).read_text(encoding="utf-8"))
        plan = json.loads((ROOT / "tools" / "live_batches" / batch / "plans" /
                           Path(spec["args"][spec["args"].index("-MovePlan") + 1]).name).read_text(encoding="utf-8"))
        labels = {p["label"] for p in plan}
        ee = spec.get("expect_every") or []
        check(f"{job}: expect_every present, regexes compile, windows are plan labels",
              bool(ee) and all(re.compile(g["select"]) and re.compile(g["match"]) and set(g["windows"]) <= labels for g in ee))

    if fails:
        print(f"{fails} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
