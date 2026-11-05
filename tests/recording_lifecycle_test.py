"""Integration guards for the layer recording lifetime hooks.

The state transitions themselves run in fg_copy_lease_test.
"""
from pathlib import Path
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]


def body(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r"\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    if not match:
        raise AssertionError(name + " definition missing")
    start = match.end() - 1
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RecordingLifecycle(unittest.TestCase):
    def test_guide_reader_and_writers_share_lock(self):
        for function in ("ngx_diffuse_albedo", "ensure_guides", "guides_init_barrier"):
            self.assertIn("lock(g_guide_mu)", body("layer/ngx.cpp", function))

    def test_discard_paths_cancel_pending_uploads_and_copies(self):
        for function in ("cmd_forget", "discard_recording", "discard_recordings", "BeginCommandBuffer"):
            code = body("layer/cmd.cpp", function)
            self.assertIn("alpha_discard_recording", code)
            self.assertIn("post_discard_recording", code)
            self.assertIn("sl_discard_recording", code)

    def test_only_successful_submissions_publish_uploads(self):
        source = (ROOT / "layer/layer.cpp").read_text()
        for function in ("QueueSubmit", "QueueSubmit2"):
            code = body("layer/layer.cpp", function)
            self.assertIn("if (r == VK_SUCCESS) {", code)
            self.assertGreater(code.index("alpha_submit_recordings(cmds)"), code.index("VkResult r ="))
            self.assertGreater(code.index("post_submit_recordings(cmds)"), code.index("if (r == VK_SUCCESS) {"))
            self.assertGreater(code.index("sl_note_submit(cmds)"), code.index("VkResult r ="))
        self.assertEqual(source.count("alpha_submit_recordings(cmds)"), 2)

    def test_post_secondary_snapshot_and_dispatch_without_pending_gate(self):
        execute = body("layer/cmd.cpp", "CmdExecuteCommands")
        self.assertIn("post_secondaries.push_back(sec->recording)", execute)
        submit = body("layer/cmd.cpp", "post_submit_recordings")
        self.assertIn("s->post_secondaries", submit)
        self.assertIn("rrpost_submit_recording(recording)", submit)
        self.assertIn("ngx_submit_recording(recording)", submit)
        temporal = body("layer/rrpost.cpp", "rrpost_temporal")
        self.assertNotIn(".pending", temporal)
        self.assertIn("producer.readable()", temporal)
        self.assertIn("g_temporal_active = true", temporal)
        self.assertNotIn("&g_hist_ac[0], &g_hist_eb[0]", temporal)
        allocator = body("layer/rrpost.cpp", "acquire_set")
        self.assertIn("life_before_destroy", allocator)
        self.assertIn("g_set_pages.push_back", allocator)
        self.assertIn("life_note(cmd", allocator)

    def test_guides_carry_transition_in_every_recording(self):
        init = body("layer/ngx.cpp", "guides_init_barrier")
        self.assertNotIn("if (!g.initialized)", init)
        self.assertNotIn("g.initialized = true", init)
        self.assertIn("VK_IMAGE_LAYOUT_UNDEFINED", init)
        self.assertIn("VK_PIPELINE_STAGE_ALL_COMMANDS_BIT", init)
        self.assertIn("guide.initialized = true", body("layer/ngx.cpp", "ngx_submit_recording"))

    def test_device_teardown_resets_upload_state(self):
        code = body("layer/alphatest.cpp", "alpha_device_destroyed")
        self.assertIn("g_dummy_init = {}; g_vt_upload = {};", code)


if __name__ == "__main__":
    unittest.main()
