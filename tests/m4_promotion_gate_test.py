"""Offline test of tools/m4_promotion_gate.py on a synthetic queue run (no game).

    py -3 tests/m4_promotion_gate_test.py

The run crosses midnight. Checks: a periodic line counts for a phase only when its whole window is
inside the phase (the line straddling a toggle is dropped); scene colour comes from the 5c3fdabc slot 0
dump (not from other passes); energy verdicts within / brighter / darker; the emission triple; capacity
ignores the load-time window.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import m4_promotion_gate as g  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


def hms(t):
    t %= 86400
    return "%02d:%02d:%06.3f" % (t // 3600, (t % 3600) // 60, t % 60)


def main():
    tmp = Path(tempfile.mkdtemp(prefix="m4pg-"))
    labels = [("cost-off-a", 20), ("cost-on", 20), ("cost-off-b", 20), ("shot-off-a", 6), ("shot-on", 6), ("shot-off-b", 6),
              ("cost-em-off-a", 20), ("shot-em-off-a", 6), ("shot-em-off-b", 6), ("cost-em-off-b", 20), ("diag-on", 20)]
    plan = [{"label": l, "seconds": s, "set": ""} for l, s in labels]
    (tmp / "plan.json").write_text(json.dumps(plan))
    t0 = 86400 - 30.0   # 23:59:30: the run crosses midnight
    runner, log = [], [f"{hms(t0 - 60)}  [geo] RT capture, last 120 frames: alpha region 64 MB (peak demand 200.0 MB), opaque peak 100.0 MB of a 1024 MB buffer; per frame: alpha draws made opaque (region full) 0.60, opaque draws dropped (buffer full) 0.00"]
    trace_ms = {"cost-off-a": 0.30, "cost-on": 0.80, "cost-off-b": 0.32, "cost-em-off-a": 0.70, "cost-em-off-b": 0.72}
    lum = {"shot-off-a": 0.2000, "shot-on": 0.2020, "shot-off-b": 0.2004, "shot-em-off-a": 0.2010, "shot-em-off-b": 0.2030}
    t = t0
    for i, (label, sec) in enumerate(labels):
        runner.append(f"{hms(t)} [pan-plan] window {i} serial 0 repeat_ms 0 kind=static set=x")
        log.append(f"{hms(t + 0.01)}  [cfg] settings read: bg3rtx.ini (1 keys) -> 1 keys")
        if label in trace_ms:   # a 300-frame line every 3 s, the first one straddles the toggle (prev = 1000 ms)
            for k in range(sec // 3):
                tl = t + 1.0 + 3 * k
                v = 1000.0 if k == 0 else trace_ms[label]
                log.append(f"{hms(tl)}  [core] gi: trace pass (ray-traced, gi_rt=1, 2 rays/sample, full res) GPU median {v:.3f} ms, p10 0, p90 0 (300 frames)")
        if label in lum:
            cap = tmp / f"cap{i}"
            cap.mkdir()
            dumps = {"d1": {"pass_fs": "5c3fdabc477f316a", "slot": 0, "mean": [lum[label] * 9, 0, 0], "nonfinite": 0},
                     "d2": {"pass_fs": "5c3fdabc477f316a", "slot": 0, "mean": [lum[label], lum[label], lum[label]], "nonfinite": 0},
                     "d3": {"pass_fs": "3434e561aaaaaaaa", "slot": 0, "mean": [9, 9, 9], "nonfinite": 0}}
            (cap / "dumps.json").write_text(json.dumps(dumps))
            log.append(f"{hms(t + 4)}  [trace] frame {i} written: 4 submits, 10 images -> {cap}")
        if label == "diag-on":
            log.append(f"{hms(t + 15)}  [core] rf hit shade (300 frames): tier b hidden hits 100, shaded 24 (24.0%), unknown material 1, unsupported lighting 1, shaded with local probes listed 0, shaded mean luminance 0.1; tier a check: 50 hits (5 unsupported), mean L_b 0.11000, mean L_a 0.10000, mean (L_b - L_a) per frame 0.01000, standard error 0.00100 (300 frames), mean |L_b - L_a| 0.02; L_b terms direct 0.01, sun 0.02, ambient 0.08; clipped 0")
        log.append(f"{hms(t + sec - 1)}  [geo] RT capture, last 120 frames: alpha region 300 MB (peak demand 210.0 MB), opaque peak 120.0 MB of a 1024 MB buffer; per frame: alpha draws made opaque (region full) 0.00, opaque draws dropped (buffer full) 0.00")
        t += sec
    (tmp / "m4pt-x.1.235930.runner.txt").write_text("\n".join(runner) + "\n")
    logf = tmp / "m4pt-x.1.235930.log"
    logf.write_text("\n".join(log) + "\n")

    run = g.Run(logf, None, tmp / "plan.json")
    check("11 phases timed across midnight, in order", len(run.phases) == 11 and all(
        run.phases[i][1] < run.phases[i + 1][1] for i in range(10)))
    c = run.cost("cost-on")
    check("straddling line dropped: cost-on trace median 0.800 from 5 windows", abs(c["gi: trace pass"][0] - 0.80) < 1e-9 and c["gi: trace pass"][1] == 5)
    check("cost-off-a 0.300", abs(run.cost("cost-off-a")["gi: trace pass"][0] - 0.30) < 1e-9)
    check("scene colour = last 5c3fdabc slot 0 dump (lum 0.2000)", abs(run.scene_colour("shot-off-a")[0] - 0.2000) < 1e-9)
    rows = {(e["a"], e["on"]): e for e in g.energy(run)}
    bundle = rows[("shot-off-a", "shot-on")]
    check("bundle +0.90% vs spread 0.20%: brighter, explain", abs(bundle["d"] - 0.0018 / 0.2002) < 1e-9 and abs(bundle["s"] - 0.0004 / 0.2002) < 1e-9 and bundle["verdict"] == "brighter: explain")
    em = rows.get(("shot-em-off-a", "shot-on"))
    check("emission triple found; ON 0.2020 vs 0.2020 mean within spread 0.99%", em is not None and em["verdict"] == "within")
    check("energy_verdict darker", g.energy_verdict(-0.02, 0.01) == "DARKER: energy rule" and g.energy_verdict(0.005, 0.01) == "within")
    cost_triples = g.triples(run.labels, "cost")
    check("cost triples: bundle and emission", ("cost-off-a", "cost-on", "cost-off-b") in cost_triples and ("cost-em-off-a", "cost-on", "cost-em-off-b") in cost_triples and len(cost_triples) == 2)
    m = run.last_in("diag-on", g.DIAG["rf hit shade"])
    check("rf hit shade line parsed (L_b 0.11, SE 0.001)", m is not None and m.group(5) == "0.11000" and m.group(8) == "0.00100")
    cap = dict((n, v) for n, v, _ in run.capacity())
    check("capacity ignores the load-time overflow (0.60 before the plan)", cap["alpha made opaque /frame"] == 0.0)
    g.report(run)   # must not raise
    print("all checks passed" if not fails else f"{fails} check(s) failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
