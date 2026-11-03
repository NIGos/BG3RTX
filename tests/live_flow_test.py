"""Offline regression tests for job design and same-save composition/extraction."""
from copy import deepcopy
import json
import re
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches import live_flow as f
from tools.live_batches import combine as c


def phase(label, seconds=12, values="rr=0"):
    return dict(label=label, seconds=seconds, kind="static", set=values)


def fixture(name="one"):
    return dict(name=name, save="same", window_mode="visible_unfocused_fg", set="preset=ultra,rr=0",
                args=["-Move", "-MovePlan", "unused.json"], expect=[r"scene ready"], patterns=["gpu"],
                forbid=[r"exception 0x"], expect_every=[dict(select=r"\[rrpost\] gpu:", match="good", min=1,
                                                          windows=["cost-off", "cost-on"])],
                expect_runner=[r"\[motion-plan\] complete phases=4\b"]), [
                    phase("warm-off"), phase("warm-on", values="rr=1"),
                    phase("cost-off"), phase("cost-on", values="rr=1")]


class FlowTests(unittest.TestCase):
    def test_pool_producer_requires_stats_in_each_observe_window(self):
        j, p = fixture()
        j["set"] += ",rt_stats=0"
        j["expect_every"] = [dict(select=r"\[hit material paks join\]", match="ok", windows=["cost-off", "cost-on"])]
        self.assertEqual(len(f.producer_problems(j, p)), 2)
        p[2]["set"] += ",rt_stats=1"
        self.assertFalse(f.producer_problems(j, p))  # inherited by the next window
        p[3]["set"] += ",rt_stats=0"
        self.assertEqual(len(f.producer_problems(j, p)), 1)
        j["expect_every"][0]["min"] = 0
        self.assertFalse(f.producer_problems(j, p))  # absence gate
        j["expect"] = [r"\[hit material paks\] loaded 40983 candidates"]
        self.assertFalse(f.producer_problems(j, p))  # startup is unconditional
        j["expect"] = [r"\[hit material pool basis\] frame"]
        j["expect_every"] = []
        p[2]["set"] += ",rt_stats=0"
        self.assertTrue(f.producer_problems(j, p))

    def test_environment_checks_scalars_not_file_backed_job_and_counts_utf16(self):
        j, _ = fixture()
        j["expect"] = ["x" * 40000]
        j["set"] += ",unused=" + "x" * 40000
        self.assertFalse(f.environment_problems(j))
        j["args"] += ["-Tag", "x" * 32768]
        self.assertTrue(any("BG3RTX_RUN_TAG" in e for e in f.environment_problems(j)))
        j["args"][-1] = "\U0001f600" * 16384
        self.assertTrue(any("32768 UTF-16" in e for e in f.environment_problems(j)))
        j["args"] = ["-UserIni", "x" * 32768]
        self.assertTrue(any("BG3RTX_USER_INI" in e for e in f.environment_problems(j)))
        j["args"] = []
        j["save"] = "x" * 32766
        self.assertFalse(f.environment_problems(j))
        j["save"] += "x"
        self.assertTrue(any("BG3RTX_SAVE_ONLY" in e for e in f.environment_problems(j)))

    def test_regex_forbid_quotes_normal_line_and_is_case_insensitive(self):
        evidence = [dict(path="valid.log", lines=["[water] nonfinite 0", "[crash] exception diagnostics active"])]
        errors, coverage = f.regex_problems(dict(forbid=["NONFINITE", r"\[crash\] exception"], expect=["future effect"]), evidence)
        self.assertEqual(len(errors), 2)
        self.assertIn(repr(evidence[0]["lines"][0]), errors[0])
        self.assertEqual(coverage["expect: future effect"], 0)

    def test_regex_precise_patterns_and_bad_regex(self):
        self.assertFalse(f.regex_problems(dict(forbid=[r"exception 0x", r"nonfinite [1-9]\d*"]), [dict(path="p", lines=["exception diagnostics active", "nonfinite 0"])])[0])
        self.assertTrue(f.regex_problems(dict(expect=["["]), [])[0])

    def test_corpus_newest_per_save_valid_only_and_cache_invalidation(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            rows = []
            for i in range(4):
                p = t / f"{i}.log"
                p.write_text(f"line{i}")
                rows.append(dict(valid=i != 3, save="a" if i < 2 else "b", log=str(p), start=str(i)))
            results, cache = t / "results.jsonl", t / "cache.json"
            results.write_text("\n".join(map(json.dumps, rows)))
            index = f.corpus(results, cache, newest=1, inventory=t)
            self.assertEqual(index["saves"], {"a": 1, "b": 1})
            self.assertEqual({x["lines"][0] for x in index["entries"]}, {"line1", "line2"})
            stamp = cache.stat().st_mtime_ns
            self.assertEqual(f.corpus(results, cache, 1, t), index)
            self.assertEqual(cache.stat().st_mtime_ns, stamp)
            (t / "1.log").write_text("changed")
            self.assertIn("changed", [e["lines"][0] for e in f.corpus(results, cache, 1, t)["entries"]])

    def test_corpus_missing_or_unknown_save_is_not_fabricated(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            result = t / "results.jsonl"
            result.write_text(json.dumps(dict(valid=True, job="unknown", log="unknown")))
            self.assertEqual(f.corpus(result, t / "cache", inventory=t)["entries"], [])

    def test_300_frame_window_needs_two_lines_and_latency(self):
        j, p = fixture()
        j["expect_every"] = [dict(select="rt passes: GPU cost", match="ok", windows=["cost-off"], min=1)]
        errors, budgets = f.window_problems(j, p)
        self.assertEqual(budgets[0]["minimum_seconds_exclusive"], 21)
        self.assertTrue(errors)
        p[2]["seconds"] = 21
        self.assertTrue(f.window_problems(j, p)[0])
        p[2]["seconds"] = 21.1
        self.assertFalse(f.window_problems(j, p)[0])

    def test_multiple_lines_trace_stall_and_custom_cadence(self):
        j, p = fixture()
        j["live_flow"] = dict(trace_stall_s=3)
        j["expect_every"] = [dict(select="custom", match="ok", windows=["cost-off"], min=2, period_frames=60)]
        p[2].update(seconds=11, trace_at=[1, 2])
        err, budgets = f.window_problems(j, p)
        self.assertEqual(budgets[0]["minimum_seconds_exclusive"], 13)
        self.assertTrue(err)
        p[2]["seconds"] = 13.1
        self.assertFalse(f.window_problems(j, p)[0])

    def test_event_period_still_needs_enough_time(self):
        j, p = fixture()
        j["expect_every"] = [dict(select=r"\[sl\] DLSS-G frame", match="ok", event=True, settle_s=1, windows=["cost-off"])]
        p[2]["seconds"] = 3
        self.assertTrue(f.window_problems(j, p)[0])
        p[2]["seconds"] = 5
        self.assertFalse(f.window_problems(j, p)[0])

    def test_cadence_unknown_missing_label_foreground_and_min_zero(self):
        j, p = fixture()
        j["expect_every"] = [dict(select="new statistic", match="ok", windows=["cost-off"])]
        self.assertIn("unknown cadence", f.window_problems(j, p)[0][0])
        j["expect_every"][0]["min"] = 0
        self.assertFalse(f.window_problems(j, p)[0])
        j["expect_every"][0]["windows"] = ["missing"]
        self.assertTrue(f.window_problems(j, p)[0])
        j.pop("window_mode")
        self.assertIn("expected_fps", f.window_problems(j, p)[0][0])

    def test_warmup_both_values_required_before_first_measurement(self):
        j, p = fixture()
        self.assertFalse(f.warmup_problems(j, p))
        p[1]["set"] = "rr=0"
        self.assertIn("rr values 1", f.warmup_problems(j, p)[0])
        p.insert(3, phase("warm-too-late", values="rr=1"))
        self.assertTrue(f.warmup_problems(j, p))

    def test_startup_only_is_not_a_toggle_loophole(self):
        j, p = fixture()
        j["live_flow"] = dict(startup_only=["rr"])
        self.assertIn("startup-only", f.warmup_problems(j, p)[0])
        for step in p:
            step["set"] = "rr=1"
        self.assertFalse(f.warmup_problems(j, p))

    def test_restore_and_capture_commands_do_not_need_warming(self):
        j, p = fixture()
        p.append(phase("restore", values="rr=7,fg_snap=0"))
        p[2]["set"] += ",fg_snap=8"
        self.assertFalse(f.warmup_problems(j, p))

    def test_known_repairs_preserve_failure_detection_after_load(self):
        j, p = fixture()
        j["forbid"] += [r"\[bvh\] mesh pool full at", "nonfinite", r"\[crash\] exception"]
        f.repair_known_forbids(j, p)
        self.assertFalse(f.regex_problems(j, [dict(path="valid", lines=["[bvh] mesh pool full at 42", "nonfinite 0", "kCsNonFinite", "[crash] exception diagnostics active"])])[0])
        scoped = j["expect_every"][-1]
        self.assertEqual(scoped["min"], 0)
        self.assertTrue(re.search(scoped["select"], "[bvh] mesh pool full at 42"))
        self.assertTrue(f.regex_problems(j, [dict(path="bad", lines=["non-finite terms 1"])])[0])

    def test_preset_reset_uses_slider_scale_and_all_linked_keys(self):
        state = c.defaults("ultra")
        self.assertEqual(state["gi_strength"], "1")
        self.assertEqual(state["ls_strength"], "1")
        self.assertEqual(state["rf_strength"], "1")

    def test_combine_resets_settings_labels_gates_and_trace_expectations(self):
        a, p = fixture()
        b, q = fixture("two")
        b["set"] += ",gi_strength=2"
        p[-1]["set"] += ",gi_strength=3"
        b["expect_runner"].append(r"\[motion-trace\] phase=2 label=cost-off repeat=0")
        j, phases, manifest = c.combine_group([("a", a, p), ("b", b, q)], "combined.json")
        self.assertEqual(len(phases), 9)
        self.assertIn("gi_strength=2", phases[4]["set"])
        self.assertEqual(manifest["slices"][1]["offset"], 5)
        self.assertEqual(len({s["label"] for s in phases}), len(phases))
        self.assertTrue(all(g["windows"][0].startswith("b") for g in j["expect_every"]))
        self.assertTrue(any(re.search(rx, "[motion-trace] phase=7 label=b2-two--cost-off repeat=0") for rx in j["expect_runner"]))
        self.assertFalse(f.window_problems(j, phases)[0])
        self.assertFalse(f.warmup_problems(j, phases))

    def test_combine_rejects_incompatible_mode_startup_and_repeat(self):
        for change in (dict(window_mode="visible_unfocused"), dict(repeat=2), dict(tree="other")):
            a, p = fixture()
            b, q = fixture("two")
            b.update(change)
            with self.assertRaises(ValueError):
                c.combine_group([("a", a, p), ("b", b, q)], "plan")

    def test_combine_restores_each_real_preset_and_rejects_startup_only_changes(self):
        a, p = fixture()
        b, q = fixture("two")
        b["set"] = "preset=high,rr=0"
        j, phases, manifest = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        reset = c.settings(phases[4]["set"])
        self.assertEqual(reset["preset"], "high")
        ultra, high = c.defaults("ultra"), c.defaults("high")
        changed = [k for k in ultra.keys() & high.keys() if ultra[k] != high[k]]
        self.assertTrue(changed)
        for key in changed:
            self.assertEqual(reset[key], high[key])
        b["live_flow"] = dict(startup_only=["preset"])
        with self.assertRaises(ValueError):
            c.combine_group([("a", a, p), ("b", b, q)], "plan")

    def test_combine_cli_writes_one_job_per_save_and_dryruns(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            paths = []
            for name in ("one", "two", "three"):
                job, phases = fixture(name)
                if name == "three":
                    job["save"] = "another"
                job["args"][-1] = str(t / name / "plans/plan.json")
                jf = t / name / "jobs/job.json"
                f.write(jf, job)
                f.write(t / name / "plans/plan.json", phases)
                paths.append(str(jf))
            with patch.object(f, "corpus", return_value=dict(entries=[dict(path="valid", lines=["scene ready"])])):
                self.assertEqual(c.main(paths + ["--out", str(t / "combined")]), 0)
            self.assertEqual(len(list((t / "combined/jobs").glob("*.json"))), 2)

    def test_duplicate_labels_and_negative_stall_are_rejected(self):
        j, p = fixture()
        p.append(deepcopy(p[0]))
        self.assertTrue(f.window_problems(j, p)[0])
        p.pop()
        j["live_flow"] = dict(trace_stall_s=-1)
        self.assertTrue(f.window_problems(j, p)[0])

    def test_extract_rebases_plan_numbers_excludes_neighbor_and_checks_original_gates(self):
        a, p = fixture()
        b, q = fixture("two")
        a.update(post='analyzer "{log}" --runner "{runner}"', post_gate=True)
        a["expect_every"] = b["expect_every"] = []
        j, phases, manifest = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        manifest["plan"] = phases
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            logs = ["00:00:00.000 scene ready"]
            runner = []
            for i, step in enumerate(phases):
                runner.append(f"00:00:{i + 1:02}.000 [pan-plan] window {i} set={step['set']}")
                logs.append(f"00:00:{i + 1:02}.100 [cfg] settings read")
                logs.append(f"00:00:{i + 1:02}.200 batch-{i}")
            runner += ["00:00:10.000 [motion-plan] complete phases=9", "Run ended: done"]
            log, run = t / "combined.1.000000.log", t / "run.txt"
            log.write_text("\n".join(logs)); run.write_text("\n".join(runner))
            results = c.extract(manifest, log, run, t / "out")
            self.assertTrue(all(r["valid"] for r in results), results)
            second = Path(results[1]["log"])
            self.assertNotIn("batch-0", second.read_text())
            self.assertIn("batch-5", second.read_text())
            self.assertIn("window 0", second.with_suffix(".runner.txt").read_text())
            with patch.object(c.subprocess, "run") as post:
                post.return_value.returncode = 7
                post.return_value.stdout = "gate failed"
                post.return_value.stderr = ""
                rerun = c.extract(manifest, log, run, t / "with-post", run_post=True)
                self.assertFalse(rerun[0]["valid"])
                self.assertTrue(rerun[1]["valid"])
                self.assertIn(rerun[0]["log"], post.call_args.args[0])
                self.assertNotIn(str(log), post.call_args.args[0])
            run.write_text("\n".join(runner[:-2]))
            with self.assertRaises(ValueError):
                c.extract(manifest, log, run, t / "incomplete")


if __name__ == "__main__":
    unittest.main()
