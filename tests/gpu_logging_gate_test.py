"""Offline policy guards for log-only Vulkan timing (no GPU fixture required)."""
from pathlib import Path
import re
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
            "core/gi.cpp": ["timing_init", "timestamp", "timing_collect", "rt_timestamp", "rt_timing_collect", "trace_timestamp", "trace_timing_collect", "rf_timestamp", "rf_timing_collect", "ls_timestamp", "ls_timing_collect"],
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

    def test_startup_setting_and_fault_memory_survive_quiet_mode(self):
        util = body("layer/util.cpp", "cfg_read_files")
        self.assertIn('next["detailed_logging"] = startup.at("detailed_logging")', util)
        self.assertIn('[cfg] detailed logging: %d', util)
        self.assertIn('int(logging::stats_enabled(cfg_int))', util)
        fault = body("layer/devfault.cpp", "fault_track")
        self.assertIn('log_stats("[fault] range of ours:', fault)
        self.assertIn('g_live.push_back(std::move(r))', fault)
        self.assertNotRegex(fault, r"stats_enabled.*return")


if __name__ == "__main__":
    unittest.main()
