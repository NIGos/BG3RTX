"""Offline policy guards for log-only Vulkan timing (no GPU fixture required)."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def body(path, name):
    text = source(path)
    match = re.search(r"\b" + name + r"\([^;{}]*\)\s*\{", text)
    assert match, (path, name)
    start = match.end()
    depth = 1
    for end in range(start, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if not depth:
            return text[start:end]
    raise AssertionError((path, name, "unclosed function"))


class GpuLoggingGateTest(unittest.TestCase):
    def test_nr_retained_pool_stops_when_statistics_stop(self):
        evaluate = body("layer/dlss5.cpp", "dlss5_evaluate")
        self.assertIn("const bool measured=collect_stats && s.query!=VK_NULL_HANDLE", evaluate)
        self.assertIn("if(measured)device->vk.CmdWriteTimestamp", evaluate)
        self.assertIn("if(measured){device->vk.CmdResetQueryPool", evaluate)
        harvest = body("layer/dlss5.cpp", "harvest")
        self.assertIn("!collect_stats", harvest.split(";", 1)[0])
        self.assertNotIn("VK_QUERY_RESULT_WAIT_BIT", harvest)

    def test_timestamp_helpers_require_statistics(self):
        helpers = {
            "core/gi.cpp": ["timing_init", "timestamp", "timing_collect", "rt_timestamp", "rt_timing_collect", "trace_timestamp", "trace_timing_collect", "rf_timestamp", "rf_timing_collect", "ls_timestamp", "ls_timing_collect", "hair_fibre_timestamp", "hair_fibre_timing_collect"],
            "core/sharc.cpp": ["timing_collect", "sharc_timestamp"],
            "layer/ngx.cpp": ["ts_init", "ts_begin"],
            "layer/emissive.cpp": ["ts_pair"],
            "layer/hair_primary.cpp": ["timing_before_gbuffer"],
            "layer/water.cpp": ["surface_begin", "surface_harvest"],
            "layer/rtscene.cpp": ["timestamp"],
        }
        for path, names in helpers.items():
            for name in names:
                with self.subTest(path=path, name=name):
                    prefix = body(path, name).split(";", 1)[0]
                    self.assertIn("stats_enabled", prefix)
                    self.assertIn("return", prefix)

    def test_gi_subpass_timers_are_detailed_only_and_nonblocking(self):
        for name in ["pass_timestamp", "pass_timing_collect"]:
            code = body("core/gi.cpp", name)
            self.assertIn('if (!host->cfg_int("detailed_logging", 0)) return', code.split(";", 1)[0])
            self.assertNotIn("VK_QUERY_RESULT_WAIT_BIT", code)
            self.assertNotIn("DeviceWaitIdle", code)
        # A failed query-pool creation at the start must never leave an end-only pair.
        self.assertIn("if (edge != 0) return", body("core/gi.cpp", "pass_timestamp"))
        self.assertIn("edge == 1 && dispatched", body("core/gi.cpp", "pass_timestamp"))
        for name in ["hair_timing_update", "gi_on_frame", "gi_release"]:
            self.assertIn("pass_timing_reset()", body("core/gi.cpp", name))
        release = body("core/gi.cpp", "gi_release")
        self.assertIn("defer_destroy(BG3_DEFER_QUERY_POOL, (uint64_t)g_pass_queries)", release)

    def test_render_slots_do_not_imply_valid_timestamps(self):
        water = source("core/water.cpp")
        self.assertRegex(water, r"g_timed\[slot\] = g_qp && .*stats_enabled")
        self.assertRegex(water, r"g_ctimed\[slot\] = g_cqp && .*stats_enabled")
        self.assertIn("g_timed[slot] && g_qp", body("core/water.cpp", "collect"))
        self.assertIn("g_ctimed[slot] && g_cqp", body("core/water.cpp", "collect_compose"))
        sl = source("layer/streamline.cpp")
        self.assertIn("if (slot.timed)", sl)
        self.assertIn("slot.timed = time_copy && g_mv_copy_queries", sl)

    def test_resource_cleanup_never_depends_on_logging(self):
        for path in ["core/water.cpp", "layer/water.cpp", "layer/streamline.cpp"]:
            for line in source(path).splitlines():
                if "DestroyQueryPool" in line or "defer_destroy(BG3_DEFER_QUERY_POOL" in line:
                    self.assertNotIn("stats_enabled", line)
                    self.assertNotIn("time_copy", line)

    def test_quiet_present_does_not_collect_pacing_samples(self):
        present = body("layer/layer.cpp", "QueuePresentKHR")
        gate = present.index("if (logging::stats_enabled(cfg_int))")
        self.assertGreater(present.index("ft.push_back"), gate)
        self.assertGreater(present.index("std::sort"), gate)
        self.assertIn("ft.clear(); last = {};", present)
        memory = body("layer/layer.cpp", "log_memory_budget")
        self.assertLess(memory.index("now - last_summary < 60000"), memory.index("query_memory_budget"))

    def test_fallback_literal_validation_is_developer_only_before_formatting(self):
        for kind, name, formatting in [("int", "cfg_int", "std::to_string"), ("float", "cfg_float", "snprintf")]:
            code = source("layer/util.cpp").split(f"\n{kind} {name}(", 1)[1].split("\n}\n", 1)[0]
            self.assertLess(code.index("g_cfg_literal_checks.load"), code.index(formatting))
        self.assertIn("g_cfg_literal_checks.store", body("layer/util.cpp", "cfg_read_files"))

    def test_fg_statistics_are_quiet_without_weakening_consumption_safety(self):
        present = body("layer/streamline.cpp", "sl_present")
        cadence_gate = present.index("if (sample_stats && g_sl.DLSSGGetState")
        self.assertLess(present.index("state_result=g_sl.DLSSGGetState"), cadence_gate)
        self.assertLess(present.index("use.second.publish"), cadence_gate)
        self.assertGreater(present.index('[sl] FG cadence'), cadence_gate)
        self.assertIn('if (sample_stats) g_present_cpu.add', present)
        self.assertIn('if (sample_stats) g_state_cpu.add', present)
        self.assertIn('if (sample_stats) sdk_stats.add', present)
        raw = body("layer/streamline.cpp", "raw_present")
        self.assertLess(raw.index('if (!sample_stats) return'), raw.index('fg_cpu_stamp()'))
        reflex = body("layer/streamline.cpp", "new_frame")
        self.assertIn('sample_stats ? fg_cpu_stamp() : 0', reflex)
        self.assertIn('if (sample_stats) g_reflex_cpu.add', reflex)
        reset = present.split('if ((sample_stats && stats_window::reset(cfg_int,stats_reset_token))', 1)[1].split('\n  }', 1)[0]
        for required in ['sdk_stats={}', 'g_present_cpu={}', 'g_raw_present_host.take()',
                         'g_prev_frame=frame', 'g_prev_displayed=g_displayed.load()', 'prev_batched=g_batched.load()']:
            self.assertIn(required, reset)
        inputs = body("layer/streamline.cpp", "sl_note_draw")
        self.assertIn('log_stats("[sl] hudless = image', inputs)
        self.assertIn('log_stats("[sl] UI color+alpha = image', inputs)
        self.assertNotIn('log("[sl] hudless = image', inputs)
        self.assertNotIn('log("[sl] UI color+alpha = image', inputs)
        # Normal mode still retains failures and SDK interpolation transitions.
        self.assertLess(present.index('[sl] DLSS-G status'), cadence_gate)
        self.assertIn('if (!transition && type != sl::LogType::eError', body("layer/streamline.cpp", "sl_logcb"))

    def test_scene_wait_and_handout_lines_start_at_the_stats_reset(self):
        # claude/handout-stats: final-s8-ultra-high 050126, the first High handout line after the reset counted 60
        # frames of warm-up. Both places that count a frame start the window, and the handout counters reset with it.
        reset = body("layer/rtscene.cpp", "wait_stats_window")
        self.assertIn("stats_window::reset(cfg_int, reset_token)", reset)
        for required in ["g_wait = WaitStats{}", "g_wait_handed = 0", "g_ho.n = {}",
                         "g_ho_omm_accelerated = g_ho_omm_downgraded = 0", "g_ho_query_built = g_ho_query_failed = 0",
                         "g_handout_downgrades = 0"]:
            self.assertIn(required, reset)
        self.assertLess(reset.index("stats_window::reset"), reset.index("g_handout_mu"))   # lock order g_wait_mu -> g_handout_mu
        wait = body("layer/rtscene.cpp", "wait_for_frame_scene")
        self.assertLess(wait.index("wait_stats_window();"), wait.index("++g_wait.calls"))
        handout = body("layer/rtscene.cpp", "rt_scene_handout")
        self.assertLess(handout.index("wait_stats_window();"), handout.index("++g_wait.calls"))
        self.assertEqual(source("layer/rtscene.cpp").count("++g_wait.calls"), 2)   # no third, unreset counting site
        # A counted OMM downgrade needs an accelerated commit: pending micro-maps commit the reference, counted in neither.
        commit = source("layer/rtscene.cpp")
        self.assertIn("if (reserved && accelerated) {", commit)

    def test_lifetime_statistics_do_not_time_quiet_submissions(self):
        submit=body("layer/tracker.cpp", "life_on_submit")
        self.assertIn('if (sample_stats) QueryPerformanceCounter', submit)
        self.assertIn('if (!enabled) return', submit)
        self.assertIn('if (sample_stats) g_life_noted', submit)
        # Sequence/ref tracking remains unconditional lifetime safety.
        self.assertLess(submit.index('++g_submit_seq'), submit.index('stats_enabled'))
        self.assertIn('g_life[h] = LifeRef', submit)
        present=body("layer/tracker.cpp", "life_on_present")
        self.assertLess(present.index('if (logging::stats_enabled(cfg_int))'), present.index('ULONGLONG now'))
        destroy=body("layer/tracker.cpp", "life_before_destroy")
        self.assertIn('if (sample_stats) QueryPerformanceCounter', destroy)
        self.assertIn('if (wait && st == VK_TIMEOUT)', destroy)
        self.assertIn('fault_on_result(dd, res', destroy)

    @unittest.skipUnless(shutil.which("cl.exe"), "MSVC environment required for extracted CPU fixture")
    def test_hot_cache_refreshes_only_on_actual_changes(self):
        poll = "void cfg_poll() {" + body("layer/util.cpp", "cfg_poll") + "}"
        fixture = r"""
#include <atomic>
#include <mutex>
std::atomic<bool> g_cfg_hot_ready{false};
std::mutex g_cfg_poll_mu;
bool hotkey=false, files=false;
int source_value=7, cached_value=0, refreshes=0;
bool ab_poll_hotkey() { bool changed=hotkey; hotkey=false; return changed; }
bool cfg_read_files() { bool changed=files; files=false; return changed; }
void cfg_hot_refresh() { cached_value=source_value; ++refreshes; g_cfg_hot_ready=true; }
""" + poll + r"""
int main() {
  cfg_poll(); if (refreshes!=1 || cached_value!=7) return 1;
  for (int i=0;i<100;++i) cfg_poll();
  if (refreshes!=1) return 2;
  files=true; source_value=11; cfg_poll();
  if (refreshes!=2 || cached_value!=11) return 3;
  // Master RT A/B, experimental A/B and NR bypass are all hotkey changes.
  for (int i=0;i<3;++i) {
    hotkey=true; source_value=20+i; cfg_poll();
    if (refreshes!=3+i || cached_value!=20+i) return 4;
  }
  hotkey=files=true; source_value=33; cfg_poll();
  if (refreshes!=6 || cached_value!=33) return 5;
  cfg_poll(); return refreshes==6 ? 0 : 6;
}
"""
        with tempfile.TemporaryDirectory(prefix="cfg-hot-refresh-") as temp:
            path = Path(temp)
            (path / "test.cpp").write_text(fixture, encoding="utf-8")
            result = subprocess.run([shutil.which("cl.exe"), "/nologo", "/EHsc", "/std:c++20", "/MT",
                                     "test.cpp", "/Fe:test.exe"], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_startup_setting_and_fault_memory_survive_quiet_mode(self):
        util = body("layer/util.cpp", "cfg_read_files")
        self.assertIn('next["detailed_logging"] = startup.at("detailed_logging")', util)
        self.assertLess(util.index('next["detailed_logging"] = startup.at("detailed_logging")'), util.index("enforce_release_policy(next)"))
        self.assertIn('[cfg] detailed logging: %d', util)
        self.assertIn('int(logging::stats_enabled(cfg_int))', util)
        fault = body("layer/devfault.cpp", "fault_track")
        self.assertIn('log_stats("[fault] range of ours:', fault)
        self.assertIn('g_live.push_back(std::move(r))', fault)
        self.assertNotRegex(fault, r"stats_enabled.*return")


if __name__ == "__main__":
    unittest.main()
