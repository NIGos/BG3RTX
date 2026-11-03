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
    def test_readiness_does_not_replace_scene_settle_or_failure_latency(self):
        j, p = fixture()
        p[0].update(seconds=45, wait_log=["pipelines ready"])
        p += [phase("toggle-1", 5), phase("survive-native-1", 20), phase("validation-after-reload", 26.1)]
        # Prefetch readiness does not cover scene/exposure/BVH settling. The
        # post-reload bound covers the observed 25 s failure plus apply latency.
        f.tighten(j, p)
        self.assertEqual(p[0]["seconds"], 45)
        # The source's 5 s toggle observation covers the reported 2 s enable
        # failure plus 1 s apply; 1.1 s would disable hair before that failure.
        self.assertEqual(p[-3]["seconds"], 5)
        self.assertEqual(p[-2]["seconds"], 20)
        self.assertEqual(p[-1]["seconds"], 26.1)

    def test_reload_bisect_observes_failure_latency_without_resets(self):
        from tools.live_batches.hair_validation.make_job import make_bisect, STAGES
        j, _ = fixture()
        plan = make_bisect()
        f.tighten(j, plan)
        # The reported 2 s enable / 25 s reload failures need continuous time,
        # not one producer cadence or a sum across separate reloads.
        for name, _ in STAGES:
            start = next(i for i,p in enumerate(plan) if p["label"] == "survive-"+name+"-1")
            windows = plan[start:start+3]
            # The primary stage's windows hold a whole 300-frame strand line at 30 fps (its strand-draw gate).
            from tools.live_batches.hair_validation.make_job import STRAND_STAGES
            want = 25 if name in STRAND_STAGES else 20
            self.assertEqual([p["seconds"] for p in windows], [want, want, want])
            self.assertEqual(len({p["set"] for p in windows}), 1)
            self.assertFalse(any(p.get("reload_selected") for p in windows))
            self.assertGreater(sum(p["seconds"] for p in windows), 25+1)

    def test_duration_ignores_absence_gates_and_uses_period_counts(self):
        j, p = fixture()
        j["expect_every"].append(dict(select="failure", match="(?!)", event=True, min=0, windows=["cost-off"]))
        p[2]["seconds"] = 60
        budget = f.duration_budget(j, p)
        cost = next(w for w in budget["windows"] if w["window"] == "cost-off")
        self.assertEqual(cost["minimum_seconds"], 11.1)  # 1s apply + 2 x 5s cadence
        self.assertTrue(budget["findings"])
        f.tighten(j, p)
        self.assertEqual(p[2]["seconds"], 11.1)
        self.assertFalse(f.window_problems(j, p)[0])

    def test_duration_trace_uses_segment_stall_and_exclusive_endpoint(self):
        j, p = fixture()
        p[2].update(seconds=30, trace_at=[1, 6])
        j["live_flow"] = dict(trace_stall_s=1, segments=[dict(offset=2,count=1,live_flow=dict(trace_stall_s=4))])
        j["expect_every"] = [dict(select=r"\[trace\] frame", match="ok", event=True,min=2,windows=["cost-off"])]
        result = f.duration_budget(j, p)
        shot = next(w for w in result["windows"] if w["window"] == "cost-off")
        self.assertAlmostEqual(shot["minimum_seconds"], 10.1)

    def test_extract_skipped_slice_preserves_later_and_final_failure_preserves_earlier(self):
        a, p = fixture()
        b, q = fixture("two")
        for j in (a, b):
            for field in ("expect", "expect_every", "expect_runner", "forbid"):
                j[field] = []
        _, plan, manifest = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        manifest["plan"] = plan
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            log, run = folder / "fake.log", folder / "fake.runner.txt"
            log.write_text("00:00:00.000 preamble\n00:00:02.000 scene\n")
            offset = manifest["slices"][1]["offset"]
            for failed_first in (True, False):
                indices = [0] + list(range(offset-1, len(plan))) if failed_first else list(range(offset+1))
                runner = []
                for t, index in enumerate(indices, 1):
                    runner.append(f"00:00:{t:02}.000 [pan-plan] window {index} set={plan[index]['set']}")
                    if index == (0 if failed_first else offset):
                        runner.append(f"00:00:{t:02}.500 [fail-fast] phase={index} slice={0 if failed_first else offset} check=forbidden test")
                if failed_first:
                    runner.append(f"00:00:20.000 [motion-plan] complete phases={len(plan)}")
                runner.append("Run ended: fail-fast")
                run.write_text("\n".join(runner))
                result = c.extract(manifest, log, run, folder / str(failed_first))
                self.assertEqual([r["valid"] for r in result], [False, True] if failed_first else [True, False])
                failed = result[0 if failed_first else 1]
                self.assertFalse(failed["completed"])
                self.assertEqual(failed["failed_check"], "forbidden test")
                self.assertNotIn("[motion-plan] complete", Path(failed["log"]).with_suffix(".runner.txt").read_text())

    def test_preflight_uses_only_actual_tree_plan_and_rejects_long_launch(self):
        job, plan = fixture()
        job["args"] = ["-MovePlan", "actual/only.json"]
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            f.write(folder / "job.json", job)
            f.write(folder / "actual/only.json", plan)
            index = dict(entries=[dict(path="fake",lines=[])],saves=[],logs=[],matches={})
            with patch.object(f, "corpus", return_value=index):
                self.assertEqual(f.main([str(folder / "job.json"), "--preflight", "--tree", str(folder), "--cache", str(folder / "cache")]), 0)
                plan[0]["seconds"] = 901
                f.write(folder / "actual/only.json", plan)
                self.assertEqual(f.main([str(folder / "job.json"), "--preflight", "--tree", str(folder), "--cache", str(folder / "cache")]), 1)

    def test_startup_producer_uses_on_warmup_and_preserves_trace_indices(self):
        job, plan = fixture()
        pattern = r"\[hair primary\] draws [1-9]\d*"
        job["expect"] = [pattern, r"\[hit material paks\] loaded 40983 candidates"]
        job["expect_every"] = [dict(select=r"\[hair primary\]", match=pattern, period_frames=300, windows=["cost-on"])]
        job["expect_runner"].append(r"phase=3 label=cost-on")
        f.startup_producers(job, plan)
        self.assertEqual(plan[2]["label"], "producer-sanity-warm-on")
        self.assertEqual(plan[2]["wait_log"], [pattern])
        self.assertIn("rr=1", plan[2]["set"])
        self.assertEqual(plan[2]["wait_log_timeout"], 15)
        self.assertIn(r"phase=4 label=cost-on", job["expect_runner"])
        self.assertIsNone(f.period(dict(select=r"\[hit material paks\] loaded 40983 candidates"), {}, 30))
        old = deepcopy(plan)
        f.startup_producers(job, plan)
        self.assertEqual(plan, old)

    def test_single_measured_value_needs_warming_and_startup_match(self):
        j, p = fixture()
        p[1]["set"] = "rr=0"
        p[2]["set"] = p[3]["set"] = "rr=1"
        self.assertTrue(f.warmup_problems(j, p))
        j["live_flow"] = dict(startup_only=["rr"])
        self.assertIn("startup-only", f.warmup_problems(j, p)[0])
        j["set"] += ",rt_stats=0"
        j["live_flow"] = dict(startup_only=["rt_stats"])
        p[2]["set"] += ",rt_stats=1"
        p[3]["set"] += ",rt_stats=1"
        self.assertTrue(any("startup-only key rt_stats" in e for e in f.warmup_problems(j, p)))

    def test_plan_fields_allow_analysis_metadata_reject_typo(self):
        self.assertFalse(f.plan_field_problems([dict(registration_policy="per-frame-exclude-unregistered")]))
        self.assertIn("Unknown camera plan field: trace_att", f.plan_field_problems([dict(trace_att=[1])])[0])

    def test_combine_preserves_external_prerequisites_rejects_internal(self):
        a, p = fixture()
        b, q = fixture("two")
        a["requires_success_of"] = ["external"]
        b["requires_success_of"] = ["external", "other"]
        job, _, _ = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        self.assertEqual(job["requires_success_of"], ["external", "other"])
        for dependent, prerequisite in ((a, "two"), (b, "one")):
            dependent["requires_success_of"].append(prerequisite)
            with self.assertRaisesRegex(ValueError, "depend on each other"):
                c.combine_group([("a", a, p), ("b", b, q)], "plan")
            dependent["requires_success_of"].pop()

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

    def test_trace_events_need_scheduled_requests_and_serial_write_time(self):
        j, p = fixture()
        j["live_flow"] = dict(trace_stall_s=4)
        j["expect_every"] = [dict(select=r"\[trace\] frame \d+ written", match="written",
                                  event=True, min=3, settle_s=1, windows=["cost-off"])]
        p[2].update(seconds=5, trace_at=[1, 2, 3])
        errors, budgets = f.window_problems(j, p)
        self.assertTrue(errors)
        self.assertEqual(budgets[0]["complete"], 0)
        p[2].update(seconds=16, trace_at=[1, 6, 11])
        self.assertFalse(f.window_problems(j, p)[0])
        p[2].pop("trace_at")
        self.assertIn("0 complete events", f.window_problems(j, p)[0][0])

    def test_scene_wait_shot_window_needs_complete_periods_even_as_event(self):
        j, p = fixture()
        j["set"] += ",rt_scene_wait_log_frames=120"
        j["live_flow"] = dict(trace_stall_s=4)
        j["expect_every"] = [dict(select=r"\[rt\] scene wait: \d+ frames", match="0 timeouts",
                                  windows=["cost-off"])]
        p[2].update(seconds=12, trace_at=[1, 6])
        self.assertTrue(f.window_problems(j, p)[0])
        p[2]["seconds"] = 18
        self.assertFalse(f.window_problems(j, p)[0])
        j["expect_every"][0]["event"] = True
        p[2]["seconds"] = 12
        self.assertTrue(f.window_problems(j, p)[0])

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
        j["set"] = "rr=1"
        self.assertFalse(f.warmup_problems(j, p))

    def test_restore_and_capture_commands_do_not_need_warming(self):
        j, p = fixture()
        p.append(phase("restore", values="rr=7,fg_snap=0"))
        p[2]["set"] += ",fg_snap=8,fg_snap_any=1,replay_pack=1,rt_mesh_audit_frames=120,rt_mesh_audits=8"
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
        p[1]["set"] += ",gi_strength=3"
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

    def test_combined_trace_stall_budgets_remain_local_to_source_windows(self):
        a, p = fixture()
        b, q = fixture("two")
        for job, plan, stall in ((a, p, 1), (b, q, 4)):
            job["live_flow"] = dict(trace_stall_s=stall)
            job["expect_every"] = [dict(select=r"\[trace\] frame \d+ written", event=True,
                                        min=3, windows=["cost-off"])]
            plan[2].update(seconds=5, trace_at=[1, 2, 3])
        combined, plan, _ = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        errors, _ = f.window_problems(combined, plan)
        self.assertEqual(len(errors), 1)
        self.assertIn("b2-two--cost-off", errors[0])

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
        for record in ('motion-trace', 'rotation-input', 'motion-ready', 'motion-recenter'):
            b["expect_runner"].append(rf"\[{record}\] phase=0 label=warm-off")
        j, phases, manifest = c.combine_group([("a", a, p), ("b", b, q)], "plan")
        manifest["plan"] = phases
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            logs = ["00:00:00.000 scene ready"]
            runner = []
            for i, step in enumerate(phases):
                runner.append(f"00:00:{i + 1:02}.000 [pan-plan] window {i} set={step['set']}")
                for record in ('motion-trace', 'rotation-input', 'motion-ready', 'motion-recenter'):
                    runner.append(f"00:00:{i + 1:02}.050 [{record}] phase={i} label={step['label']}")
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
            slice_runner = second.with_suffix(".runner.txt").read_text()
            self.assertIn("window 0", slice_runner)
            self.assertNotIn("b1-", slice_runner)
            self.assertNotIn("b2-", slice_runner)
            for record in ('motion-trace', 'rotation-input', 'motion-ready', 'motion-recenter'):
                self.assertIn(f"[{record}] phase=0 label=warm-off", slice_runner)
                self.assertNotIn(f"[{record}] phase=4", slice_runner)
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
