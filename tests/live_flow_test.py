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
from tools.live_batches import fps_windows as fw


def phase(label, seconds=12, values="rr=0"):
    return dict(label=label, seconds=seconds, kind="static", set=values)


def fixture(name="one"):
    job, plan = dict(name=name, save="same", window_mode="visible_unfocused_fg", set="preset=ultra,rr=0",
                args=["-Move", "-MovePlan", "unused.json"], expect=[r"scene ready"], patterns=["gpu"],
                forbid=[r"exception 0x"], expect_every=[dict(select=r"\[rrpost\] gpu:", match="good", min=1,
                                                          windows=["cost-off", "cost-on"])],
                expect_runner=[r"\[motion-plan\] complete phases=4\b"]), [
                    phase("warm-off"), phase("warm-on", values="rr=1"),
                    phase("cost-off"), phase("cost-on", values="rr=1")]
    for row in plan[1:]:
        row["wait_log"] = ["rr pipeline ready"]
    f.pipeline_readiness.apply(job, plan)
    return job, plan


class PipelineReadinessTests(unittest.TestCase):
    def test_each_changed_key_requires_its_completion_even_after_prefetch(self):
        j = dict(set="rt_water_surface_fresnel=0", live_flow={})
        p = [phase("warm"), phase("on", values="rt_water_surface_fresnel=1"),
             phase("off", values="rt_water_surface_fresnel=0")]
        self.assertEqual(len(f.pipeline_readiness.problems(j, p)), 2)
        f.pipeline_readiness.apply(j, p)
        self.assertFalse(f.pipeline_readiness.problems(j, p))
        self.assertIn("enabled 1,", p[1]["wait_log"][0])
        self.assertIn("enabled 0,", p[2]["wait_log"][0])
        self.assertEqual(set(p[1]["wait_log"]), set(p[1]["wait_log_fresh"]))
        before = deepcopy(p)
        f.pipeline_readiness.apply(j, p)
        self.assertEqual(p, before)

    def test_unknown_mapping_fails_closed_despite_quiet_driver(self):
        j = dict(set="new_shading_key=0")
        p = [phase("before"), phase("on", values="new_shading_key=1")]
        f.pipeline_readiness.apply(j, p)
        self.assertIn("unknown pipeline mapping", f.pipeline_readiness.problems(j, p)[0])
        p[1]["wait_log"].append("new_shading_key pipeline ready")
        self.assertFalse(f.pipeline_readiness.problems(j, p))

    def test_control_changes_need_no_pipeline_wait(self):
        j = dict(set="rt_stats=0,qa_exposure_hold=0")
        p = [phase("before", values="rt_stats=0"), phase("diag", values="rt_stats=1,qa_exposure_hold=1")]
        f.pipeline_readiness.apply(j, p)
        self.assertFalse(any(row.get("wait_log") for row in p))

    def test_temporal_wait_uses_cached_pipeline_and_fresh_quiescence(self):
        j = dict(set="rr_post_temporal=0")
        p = [phase("before"), phase("on", values="rr_post_temporal=1")]
        f.pipeline_readiness.apply(j, p)
        self.assertIn("temporal", p[1]["wait_log"][0])
        self.assertEqual(p[1]["wait_log_fresh"], [f.pipeline_readiness.QUIET])


class FlowTests(unittest.TestCase):
    def test_trace_export_quantile_and_previous_phase_serialization(self):
        entry = dict(runner='00:00:01.000 [motion-trace]\n00:00:02.000 [motion-trace]',
                     lines=['00:00:01.500 [trace] frame 1 written:',
                            '00:00:07.000 [trace] frame 2 written:'])
        with patch.dict(f.TRACE_EXPORT, {}, clear=True):
            f.configure_trace_exports([entry])
            self.assertEqual(f.TRACE_EXPORT['seconds'], 5)
            self.assertEqual(f.TRACE_EXPORT['samples'], 2)
            j, _ = fixture()
            j['live_flow'] = dict(trace_stall_s=0, trace_dispatch_s=1)
            p = [phase('previous', 2), phase('event', 8)]
            for row in p: row['trace_at'] = [1]
            j['expect_every'] = [dict(select=r'\[trace\] frame', match='written',
                                     event=True, min=1, windows=['event'])]
            errors, budgets = f.window_problems(j, p)
            # Previous finishes at 7; next at 13 absolute / 11 local.
            self.assertTrue(errors)
            self.assertEqual(budgets[0]['minimum_seconds_exclusive'], 11)
            p[1]['seconds'] = 11.1
            self.assertFalse(f.window_problems(j, p)[0])

    def test_trace_dispatch_stall_rejects_old_energy_window(self):
        j, _ = fixture()
        j['live_flow'] = dict(trace_stall_s=1, trace_dispatch_s=4,
                              trace_export=dict(seconds=.319))
        p = [phase('energy', 2.1)]
        p[0]['trace_at'] = [1]
        j['expect_every'] = [dict(select=r'\[trace\] frame', match='written',
                                 event=True, min=1, windows=['energy'])]
        self.assertTrue(f.window_problems(j, p)[0])
        f.tighten(j, p)
        self.assertGreater(p[0]['seconds'], 6)
        self.assertFalse(f.window_problems(j, p)[0])

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


class WindowFpsTests(unittest.TestCase):
    def setUp(self):
        fw.configure([])

    def tearDown(self):
        fw.configure([])

    @staticmethod
    def job(save="camp", stats=1):
        return dict(name="fixture", save=save, window_mode="visible_unfocused_fg",
                    set=f"rt_stats={stats},rt_primary_check=0,rt_omm=0,rt_omm_reference=0",
                    expect=[], expect_every=[])

    @staticmethod
    def entry(path="valid", save="camp", stats=1, frames=60):
        return dict(path=path, save=save, job=WindowFpsTests.job(save, stats),
                    runner="00:00:00.000 [pan-plan] window 0 set=rt_stats=" + str(stats) +
                           "\n00:00:20.000 [motion-plan] complete",
                    lines=["00:00:00.100 [cfg] settings read",
                           "00:00:01.000 [sl] DLSS-G frame 100: displayed/rendered 3.00",
                           f"00:00:11.000 [sl] DLSS-G frame {100+frames}: displayed/rendered 3.00"])

    def test_same_save_diagnostic_state_uses_slow_p10_and_app_frames(self):
        fw.configure([self.entry(frames=60), self.entry("fast", frames=180),
                      self.entry("off", stats=0, frames=300),
                      self.entry("other-save", save="s8", frames=10)])
        slow = fw.expected(self.job(), f.settings(self.job()["set"]))
        self.assertEqual(slow["fps"], 6)  # Never multiply by the displayed FG ratio.
        self.assertEqual(slow["source"], "VALID p10")
        off = self.job(stats=0)
        self.assertEqual(fw.expected(off, f.settings(off["set"]))["fps"], 30)

    def test_counter_pairs_do_not_cross_settings_boundary_or_read_latency(self):
        entry = self.entry()
        entry["runner"] = ("00:00:00.000 [pan-plan] window 0 set=rt_stats=0\n"
                           "00:00:10.000 [pan-plan] window 1 set=rt_stats=1\n"
                           "00:00:20.000 [motion-plan] complete")
        entry["lines"] = ["00:00:09.000 [sl] input copy frame 100:",
                          "00:00:10.100 [sl] input copy frame 200:",
                          "00:00:11.000 [cfg] settings read",
                          "00:00:12.000 [sl] input copy frame 210:"]
        self.assertEqual(fw.window_samples(entry), [])

    def test_timestamp_rollover_keeps_rendered_counter_rate(self):
        entry = self.entry()
        entry["runner"] = ("23:59:58.000 [pan-plan] window 0 set=rt_stats=1\n"
                           "00:00:12.000 [motion-plan] complete")
        entry["lines"] = ["23:59:58.100 [cfg] settings read",
                          "23:59:59.000 [sl] input copy frame 100:",
                          "00:00:09.000 [sl] input copy frame 200:"]
        self.assertEqual(fw.window_samples(entry)[0][1], 10)

    def test_missing_app_counter_never_learns_presented_frame_throughput(self):
        entry = self.entry()
        entry["lines"] = ["00:00:01.000 displayed frame 100", "00:00:11.000 displayed frame 1000"]
        fw.configure([entry])
        result = fw.expected(self.job(), f.settings(self.job()["set"]))
        self.assertEqual(result["source"], "per-key cost fallback")
        self.assertEqual(result["cost_ms"], 100)

    def test_per_key_cost_fallback_adds_enabled_diagnostic_costs(self):
        j = self.job()
        j["args"] = ["validate_sync=true,gpuav_enable=true"]
        state = f.settings(j["set"] + ",rt_primary_check=1,rt_omm=1,rt_omm_reference=1")
        result = fw.expected(j, state)
        self.assertEqual(result["cost_ms"], 100 + 3 * 80 + 200)
        self.assertAlmostEqual(result["fps"], 1000 / (1000 / 30 + 540))

    def test_retime_grows_short_window_without_weakening_gate(self):
        fw.configure([self.entry()])
        j = self.job()
        gate = dict(select=r"\[hit material paks join\]", match="draws 1", windows=["observe"], min=1)
        j["expect_every"] = [gate]
        p = [phase("observe", 9.1, j["set"])]
        self.assertTrue(f.window_problems(j, p)[0])
        before_gate = deepcopy(gate)
        result = f.tighten(j, p)
        self.assertEqual(p[0]["seconds"], 41.1)  # Apply + skipped interval + complete interval; exclusive end.
        self.assertGreater(result["after_seconds"], result["before_seconds"])
        self.assertEqual(j["expect_every"], [before_gate])
        self.assertFalse(f.window_problems(j, p)[0])

    def test_replay_reports_absent_expect_and_missing_same_state(self):
        j = self.job()
        j["expect"] = ["new producer"]
        j["expect_every"] = [dict(select="stats", match="good", windows=["observe"])]
        result = fw.replay(j, [phase("observe", 40, j["set"])], [self.entry(stats=0)])
        self.assertTrue(any("expect missing" in problem for problem in result["problems"]))
        self.assertTrue(any("no historical window" in problem for problem in result["problems"]))
        self.assertIn("unproven", result["note"])

    def test_guard_and_lint_use_persisted_same_state_fps(self):
        from scripts.live_guard import evaluate_session, producer_period
        j = self.job()
        j["live_flow"] = dict(fps_windows=[dict(state=[1, 0, 0, 0, 0], fps=6)])
        pattern = r"\[hit material paks join\].*draws 1"
        j["expect"] = [pattern]
        j["expect_every"] = [dict(select=r"\[hit material paks join\]", match=pattern, windows=["observe"])]
        state = f.settings(j["set"])
        _, windows = f.window_problems(j, [phase("observe", 41.1, j["set"])])
        cadence = producer_period(pattern, state, fw.expected(j, state)["fps"], j)
        self.assertEqual(windows[0]["period_s"], cadence)
        self.assertIsNone(evaluate_session(j, "", cadence + 1.99))
        self.assertIn("producer silent", evaluate_session(j, "", cadence + 2)["check"])

    def test_replay_keeps_real_samples_and_does_not_borrow_after_state_change(self):
        j = self.job()
        j["expect_every"] = [dict(select="stats", match="good", windows=["observe"])]
        entry = self.entry()
        entry["runner"] = ("00:00:00.000 [pan-plan] window 0 set=rt_stats=1\n"
                           "00:00:12.000 [pan-plan] window 1 set=rt_stats=0\n"
                           "00:00:30.000 [motion-plan] complete")
        # The new 41.1 s duration cannot borrow the second, now-OFF sample.
        entry["lines"] += ["00:00:05.000 stats good", "00:00:15.000 stats good"]
        result = fw.replay(j, [phase("observe", 41.1, j["set"])], [entry])
        self.assertTrue(any("0 complete line" in problem for problem in result["problems"]), result)
        fw.configure([])
        entry["lines"][-1] = "00:00:10.000 stats bad"
        result = fw.replay(j, [phase("observe", 41.1, j["set"])], [entry])
        self.assertTrue(any("line(s) fail" in problem for problem in result["problems"]), result)



class ReadinessBudgetTests(unittest.TestCase):
    def tearDown(self):
        f.readiness_budget.configure([])

    def test_same_save_changed_key_median_and_fresh_pattern(self):
        entries = []
        for delay in (2, 4, 30):
            entries.append(dict(save="same", runner=(
                "12:00:00.000 [pan-plan] window 0 set=rt=0\n"
                "12:00:10.000 [pan-plan] window 1 set=rt=1\n"
                "12:01:00.000 [motion-plan] complete phases=2"), lines=[
                "12:00:01.000 [watch] ready", f"12:00:{10+delay:02d}.000 [watch] ready"]))
        entries.append(dict(save="other", runner=entries[0]["runner"], lines=["12:00:50.000 [watch] ready"]))
        f.readiness_budget.configure(entries)
        step = dict(wait_log=[r"\[watch\] ready"], wait_log_fresh=[r"\[watch\] ready"])
        result = f.readiness_budget.estimate(dict(save="same"), step, {"rt": "1"})
        self.assertEqual(result["seconds"], 4)
        self.assertEqual(result["samples"], 3)
        step.pop("wait_log_fresh")
        self.assertEqual(f.readiness_budget.estimate(dict(save="same"), step, {"rt": "1"})["seconds"], 0)
        self.assertIn("unproven", f.readiness_budget.estimate(dict(save="unknown"), step, {"rt": "1"})["source"])

    def test_expected_wait_cap_and_full_allowance_deadline_are_distinct(self):
        j, p = fixture()
        p = [phase("warm", 102.9)]
        p[0].update(wait_log=["ready"], wait_log_timeout=800.9)
        with patch.object(f.readiness_budget, "estimate", return_value=dict(seconds=7, samples=3, source="VALID median")):
            budget = f.duration_budget(j, p)
        self.assertEqual(budget["expected_readiness_seconds"], 7)
        self.assertEqual(budget["readiness_timeout_seconds"], 800.9)
        self.assertEqual(f.launch_problems(j, budget), [])
        self.assertTrue(f.launch_problems(dict(max_minutes=1), budget))
        j["timeout"] = 900
        self.assertIn("duration: timeout does not cover plan + readiness allowance", f.lint(j, p, [dict(path="synthetic", lines=[])])["problems"])

if __name__ == "__main__":
    unittest.main()
