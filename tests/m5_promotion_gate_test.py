"""Offline test of tools/m5_promotion_gate.py on a synthetic queue run (no game).

    py -3 tests/m5_promotion_gate_test.py

Checks: the measured image is the RR colour input (the 00000000 slot-0 dump of the scene colour's
format, not the RGBA8 guide of the same size) divided by the exposure probe at the trace frame, so an
eye-adaptation change alone moves neither the measurement nor the 5c3fdabc control (the m4 observable
shows it); several traces per shot phase; the prediction from the compose's own window lines (a
straddling window dropped; a shot phase without a complete window falls back to the cost phase before
it in the same state); matches / DARKER than predicted; non-finite pixels flagged; participation and
motion verdicts.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import m4_promotion_gate as m4  # noqa: E402
from tools import m5_promotion_gate as g  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


def hms(t):
    t %= 86400
    return "%02d:%02d:%06.3f" % (t // 3600, (t % 3600) // 60, t % 60)


ON = "rt_water_geometry=1,rt_water_reflections=1,rt_water_compose=1,rt_water_shadows=1"
OFF = "rt_water_geometry=0,rt_water_reflections=0,rt_water_compose=0,rt_water_shadows=0"
STEP = "rt_water_geometry=1,rt_water_reflections=1,rt_water_compose=0,rt_water_shadows=0"


def compose_lines(t, rec, own, comp, d_sun, clamped=0, d_rf=0.0):
    return [
        f"{hms(t)}  [core] [water] compose (120 frames, {comp} composed, 1.00 passes/frame): records {rec}/frame, owned {own:.2f}% (not owned 0.00%), invalid 0, kept: rough 10.00%, below the surface 0.00%; rays 100/frame: screen hit (composed) 10.0%, hidden 1.0% (skin 0), off screen 1.0%, miss 88.0%, aborted 0, invalid 0; unresolved (probe kept) 90.00% of owned; clamped 0; skipped frames: no record buffer 0, layout 0, size 0; passes on this frame's lighting 0, on the previous lighting 120, refused: no recent lighting 0, no scene 0; newer scene taken 0",
        f"{hms(t)}  [core] [water] compose energy: mean C 0.40000 over owned; composed 10.00% of owned: probe removed (B) 0.01000, traced added (A L) 0.00500, delta -0.00500 (+0.00100 / -0.00600) per composed pixel; mean delta over owned {d_rf:+.6f} = -0.100% of mean C; probe term B over owned 0.08000",
        f"{hms(t)}  [core] [water] compose ratio A L / B over 1000 composed px: below 0.5 30.0%, 0.5..2.0 60.0%, above 2.0 10.0%; ratio of the means 0.400",
        f"{hms(t)}  [core] [water] compose GPU 0.100 ms median, p90 0.110 (120 timed frames: the lighting copy + the compose dispatches; 0 passes beyond 4 untimed); copy 0.020 ms median, p90 0.020; compose dispatches 0.080 ms median, p90 0.090",
        f"{hms(t)}  [core] [water] shadows (120 frames, 120 passes, variant sun): sun records {rec}/frame = 99.00% of owned (no sun term 1.00%, invalid 0); traced 100.00% of sun records, occluded 60.00% of traced (traced shadow share 60.00% of sun records), unknown 0 (kept); sun term D 0.20000 per sun record, removed 0.16000 = 80.00% of D; mean removed over owned {d_sun:.6f} = 40.000% of mean C; clamped {clamped}; GPU 0.100 ms median, p90 0.110 (the compose dispatches with the sun ray + the lighting copy); skipped: records without the sun words 0",
    ]


def main():
    tmp = Path(tempfile.mkdtemp(prefix="m5pg-"))
    # (label, seconds, set, kind, traces (seconds into the phase))
    phases = [("cost-off-a", 20, OFF, "static", []), ("shot-off-a", 10, OFF, "static", [2, 5, 8]),
              ("shot-step-rf", 8, STEP, "static", [5]), ("cost-on", 20, ON, "static", []),
              ("shot-on", 6, ON, "static", [1, 3, 5]), ("cost-off-b", 20, OFF, "static", []),
              ("shot-off-b", 10, OFF, "static", [2, 5, 8]), ("move-on-q", 12, ON, "pan", [])]
    plan = [{"label": l, "seconds": s, "set": st, "kind": k} for l, s, st, k, _ in phases]
    (tmp / "plan.json").write_text(json.dumps(plan))
    W_, H_ = 100, 50                 # render size: 5000 px
    REC, OWN = 1000, 50.0            # 500 owned px per frame
    D_SUN = 0.2                      # mean removed over owned -> -0.2 * 500 / 5000 = -0.02 (pre-exposed)
    E_OFF, E_ON = 1.0e-3, 1.03e-3    # the eye adaptation re-exposes the darker ON frames by +3%
    POST_OFF = [0.2000, 0.2010, 0.1990, 0.2005, 0.1995, 0.2000]   # raw post-water means of the 6 OFF frames
    pred_rel = (-0.02 / E_ON) / (sum(POST_OFF) / 6 / E_OFF)
    post_on = (1 + pred_rel) * (sum(POST_OFF) / 6 / E_OFF) * E_ON  # exactly the prediction
    t0 = 86400 - 40.0                # crosses midnight
    runner = []
    log = [f"{hms(t0 - 30)}  [core] [water] compose images {W_}x{H_} (radiance RGBA16F, depth R32F: 1 MB)"]
    t, frame, k_off = t0, 1000, 0
    for i, (label, sec, st, kind, traces) in enumerate(phases):
        runner.append(f"{hms(t)} [pan-plan] window {i} serial 0 repeat_ms 0 kind={kind} set=x")
        log.append(f"{hms(t + 0.01)}  [cfg] settings read: bg3rtx.ini (1 keys) -> 1 keys")
        on = st == ON
        # a window line every 3 s; the first one straddles the toggle (its previous line is before the phase)
        for w in range(int(sec // 3)):
            tl = t + 0.5 + 3 * w
            if on and (label != "shot-on" or w == 0):   # shot-on: only the straddling line, no complete window
                log += compose_lines(tl, REC, OWN, 120 if label != "move-on-q" else 118, D_SUN if w else 9.9)
            if label != "shot-on" or w == 0:
                log.append(f"{hms(tl)}  [water] build GPU 0.280 ms median, p90 0.290 (120 timed of 120 builds; 2.0 geometries, 100 triangles per build; busy 0, failed 0; soup 32 MB)")
        for ts in traces:
            frame += 50
            cap = tmp / f"cap{frame}"
            cap.mkdir()
            if label.startswith("shot-off"):
                post, e = POST_OFF[k_off], E_OFF
                k_off += 1
            elif label == "shot-on":
                post, e = post_on, E_ON
            else:   # step-rf: the image 5% darker, but no compose window predicts any change
                post, e = 0.19 * E_OFF / E_OFF, E_OFF
            pre = 0.25 * e / E_OFF            # the lighting before the water: only the exposure moves it
            dumps = {"d1": {"pass_fs": "5c3fdabc477f316a", "slot": 0, "fmt": "R11G11B10F", "w": W_, "h": H_, "mean": [pre] * 3, "nonfinite": 0},
                     "d2": {"pass_fs": "0000000000000000", "slot": 0, "fmt": "RGBA8", "w": W_, "h": H_, "mean": [0.9, 0.9, 0.9, 1], "nonfinite": 0},
                     "d3": {"pass_fs": "0000000000000000", "slot": 0, "fmt": "R11G11B10F", "w": W_, "h": H_, "mean": [post] * 3,
                            "nonfinite": 7 if label == "shot-step-rf" else 0}}
            (cap / "dumps.json").write_text(json.dumps(dumps))
            log.append(f"{hms(t + ts)}  [rrpost] exposure probe (frame {frame}): 2cdd9f08 b0 = [{e:g} 1 2 3 4 5], b1 = [1 2 3 4 5 6], lighting s1b13 = [1 2 3 4]; applied k = 1; 0.8333/2^b0[2] = 1, 0.8333/2^b1[2] = 1")
            log.append(f"{hms(t + ts + 0.1)}  [trace] frame {frame} written: 4 submits, 10 images -> {cap}")
        t += sec
    log.sort(key=lambda s: (g.m4.secs(s[:12]) + (86400 if s[:2] == "00" else 0)))
    (tmp / "m5pt-x.1.235920.runner.txt").write_text("\n".join(runner) + "\n")
    logf = tmp / "m5pt-x.1.235920.log"
    logf.write_text("\n".join(log) + "\n")

    run = m4.Run(logf, None, tmp / "plan.json")
    check("8 phases timed across midnight", len(run.phases) == 8)
    v = g.shot(run, "shot-off-a")
    check("three traces in shot-off-a, RR colour input (not the RGBA8 guide) / exposure = 200, 201, 199",
          isinstance(v, list) and len(v) == 3 and all(abs(x["post"] - p / E_OFF) < 1e-6 for x, p in zip(v, POST_OFF[:3])))
    preds = g.predicted_raw(run, "cost-on")
    check("cost-on: the straddling window (removed 9.9) is dropped; 5 windows predict -0.02",
          len(preds) == 5 and all(abs(p + 0.02) < 1e-12 for p in preds))
    check("shot-on (no complete window) falls back to cost-on's windows", len(g.predicted_raw(run, "shot-on")) == 5)
    rows = {e["x"]: e for e in (g.energy_row(run, *tr) for tr in g.shot_triples(run.labels))}
    on = rows["shot-on"]
    check(f"shot-on: measured {100 * on['meas']:+.2f}% = predicted {100 * pred_rel:+.2f}%, matches",
          abs(on["meas"] - pred_rel) < 1e-9 and abs(on["pred"] - pred_rel) < 1e-9 and on["verdict"] == "matches")
    check("exposure +3% alone: the 5c3fdabc control is 0, the m4 observable reads +3%",
          abs(on["control"]) < 1e-9 and abs(on["raw_5c3fdabc"] - 0.03) < 1e-9)
    sd = (sum((p / E_OFF - sum(POST_OFF) / 6 / E_OFF) ** 2 for p in POST_OFF) / 5) ** 0.5
    check("noise = 2 SE from the six OFF frames: 2 sd sqrt(1/3 + 1/6) / mean",
          abs(on["noise"] - 2 * sd * (1 / 3 + 1 / 6) ** 0.5 / (sum(POST_OFF) / 6 / E_OFF)) < 1e-12 and on["n"] == (3, 3, 3))
    st = rows["shot-step-rf"]
    check("step-rf: -5% with no compose window -> DARKER than predicted, NONFINITE flagged",
          st["pred"] == 0.0 and st["verdict"].startswith("DARKER than predicted") and "NONFINITE 7 > OFF 0" in st["verdict"])
    parts = {(r[0], r[1].split(" ")[0]): r for r in reversed(g.participation(run))}
    check("participation: compose and shadows rows pass", parts[("rt_water_compose", "composed")][4] == "pass"
          and parts[("rt_water_shadows", "occluded")][4] == "pass")
    mv = g.motion(run)
    check("motion: move-on-q composes 118 of 120 in every window -> pass", len(mv) == 1 and mv[0][-1] == "pass")
    c = g.cost(run, "cost-on")
    check("cost: water compose 0.100 ms in cost-on, build 0.280", abs(c["water: compose (copy + dispatches)"][0] - 0.1) < 1e-9
          and abs(c["water: build"][0] - 0.28) < 1e-9)
    # A clamped window and a starved compose fail the every-window rows.
    log2 = [l.replace("clamped 0; GPU 0.100 ms median, p90 0.110 (the compose dispatches with the sun ray",
                      "clamped 500; GPU 0.100 ms median, p90 0.110 (the compose dispatches with the sun ray")
            .replace("(120 frames, 120 composed", "(120 frames, 90 composed") for l in log]
    logf2 = tmp / "m5pt-y.1.235920.log"
    logf2.write_text("\n".join(log2) + "\n")
    (tmp / "m5pt-y.1.235920.runner.txt").write_text("\n".join(runner) + "\n")
    run2 = m4.Run(logf2, None, tmp / "plan.json")
    parts2 = {(r[0], r[1].split(" ")[0]): r for r in reversed(g.participation(run2))}
    check("clamped 500 of 120 frames and 90 composed: FAIL rows", parts2[("rt_water_shadows", "occluded")][4] == "FAIL"
          and parts2[("rt_water_compose", "composed")][4] == "FAIL")
    g.report(run)   # must not raise
    print("all checks passed" if not fails else f"{fails} check(s) failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
