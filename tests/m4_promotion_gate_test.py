"""Offline test of tools/m4_promotion_gate.py on a synthetic queue run (no game).

    py -3 tests/m4_promotion_gate_test.py

The run crosses midnight. Checks: a periodic line counts for a phase only when its whole window is
inside the phase (the line straddling a toggle is dropped); scene colour comes from the 5c3fdabc slot 0
dump (not from other passes); energy verdicts within / brighter / darker; the emission triple; capacity
ignores the load-time window. exposure_case: the verdict on scene colour / pre-exposure (the gi_far S8
numbers of 2026-11-01), the probe-frame match, and the gi_far stage windows (view pairs, ls give-back).
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


def exposure_case():
    """gi_far S8 2026-11-01 (gf2-s8-fg2): the eye adaptation raised the pre-exposure s1b13[0] by 3.2% in the ON
    shot, so the raw scene colour rose +2.87% while the lit image itself got 0.32% darker. The probe line of
    the trace frame (or one within 2 frames) gives s1b13[0]; one farther away does not count."""
    tmp = Path(tempfile.mkdtemp(prefix="m4pg-exp-"))
    labels = [("shot-off-a", 6), ("shot-on", 6), ("shot-off-b", 6), ("shot-far", 6), ("shot-off-c", 6),
              ("diag-ls-off", 10), ("diag-ls-on", 10), ("view-vis-off", 6), ("view-vis-on", 6)]
    (tmp / "plan.json").write_text(json.dumps([{"label": l, "seconds": s, "set": ""} for l, s in labels]))
    lum = {"shot-off-a": 0.15354, "shot-on": 0.15814, "shot-off-b": 0.15390, "shot-far": 0.2, "shot-off-c": 0.2,
           "view-vis-off": 0.176, "view-vis-on": 0.1668}
    pre = {"shot-off-a": 0.000305704, "shot-on": 0.00031553, "shot-off-b": 0.000305773, "shot-far": 0.0003, "shot-off-c": 0.0003,
           "view-vis-off": 0.0003, "view-vis-on": 0.0003}
    probe_df = {"shot-off-a": 0, "shot-on": 1, "shot-off-b": -2, "shot-far": 3}   # probe frame - trace frame
    giveback = {"diag-ls-off": 0.00100, "diag-ls-on": 0.00138}
    runner, log, t = [], [], 3600.0
    for i, (label, sec) in enumerate(labels):
        runner.append(f"{hms(t)} [pan-plan] window {i} serial 0 repeat_ms 0 kind=static set=x")
        log.append(f"{hms(t + 0.01)}  [cfg] settings read: bg3rtx.ini (1 keys) -> 1 keys")
        frame = 5000 + 1000 * i
        if label in giveback:
            log.append(f"{hms(t + 8)}  [core] ls stats (120 frames): ao_direct 1 gives back {giveback[label]:.5f} per px (1.00% of the lit luminance); the sun as recomputed: 0.00010 per px")
            t += sec
            continue
        p = pre[label]
        log.append(f"{hms(t + 3.9)}  [rrpost] exposure probe (frame {frame + probe_df.get(label, 0)}): 2cdd9f08 b0 = [{p} 1 11.4 -8 26 0.04], "
                   f"b1 = [{p} 1 11.4 -8 26 0.04], lighting s1b13 = [{p} 1 11.4 -8]; applied k = b0[1]*b1[0] = 1; "
                   f"0.8333/2^b0[2] = {p}, 0.8333/2^b1[2] = {p}")
        cap = tmp / f"cap{i}"
        cap.mkdir()
        (cap / "dumps.json").write_text(json.dumps({"d1": {"pass_fs": "5c3fdabc477f316a", "slot": 0, "mean": [lum[label]] * 3, "nonfinite": 0},
                                                    "d2": {"pass_fs": "d1669194aaaaaaaa", "slot": 0, "mean": [lum[label] / 2] * 3, "nonfinite": 0}}))
        log.append(f"{hms(t + 4)}  [trace] frame {frame} written: 4 submits, 10 images -> {cap}")
        t += sec
    (tmp / "gfx-x.1.010000.runner.txt").write_text("\n".join(runner) + "\n")
    logf = tmp / "gfx-x.1.010000.log"
    logf.write_text("\n".join(log) + "\n")
    run = g.Run(logf, None, tmp / "plan.json")
    check("pre-exposure from the probe at the trace frame (+1 and -2 frames count)",
          run.scene_colour("shot-on")[2] == 0.00031553 and run.scene_colour("shot-off-b")[2] == 0.000305773)
    check("a probe 3 frames away does not count", run.scene_colour("shot-far")[2] is None)
    rows = {(e["a"], e["on"]): e for e in g.energy(run)}
    e = rows[("shot-off-a", "shot-on")]
    exp_ratio = 0.00031553 / ((0.000305704 + 0.000305773) / 2)
    want = (0.15814 / 0.00031553) / ((0.15354 / 0.000305704 + 0.15390 / 0.000305773) / 2) - 1
    check("gf2 S8: raw +2.87%, exposure 1.0320, normalised -0.32% darker",
          abs(e["raw_d"] - 0.0287) < 5e-4 and abs(e["exposure"] - exp_ratio) < 1e-12 and abs(e["exposure"] - 1.0320) < 5e-4
          and abs(e["d"] - want) < 1e-12 and abs(e["d"] + 0.0032) < 5e-4 and e["verdict"] == "DARKER: energy rule")
    check("normalised OFF spread 0.21%", abs(e["s"] - 0.0021) < 2e-4)
    st = {(k, n): (off, on) for k, n, off, on in g.stages(run)}
    vis = st.get(("view", "vis"))
    check("view-vis pair: diffuse (d1669194) means 0.088 / 0.0834", vis is not None and abs(vis[0] - 0.088) < 1e-12 and abs(vis[1] - 0.0834) < 1e-12)
    gb = st.get(("giveback", "ls"))
    check("diag-ls pair: ao_direct give-back 0.00100 / 0.00138", gb == (0.001, 0.00138))
    g.report(run)   # must not raise


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
    check("no exposure probe lines: verdict on the raw change, marked", bundle["exposure"] is None and bundle["d"] == bundle["raw_d"])
    g.report(run)   # must not raise
    exposure_case()
    print("all checks passed" if not fails else f"{fails} check(s) failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
