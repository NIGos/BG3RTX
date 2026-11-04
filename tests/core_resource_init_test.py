"""Failure-inject production Vulkan initialization bodies with a fake host (no GPU)."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index("bool " + name + "(")
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class CoreResourceTests(unittest.TestCase):
    def test_vulkan_failures_retry_and_retire_all_variants(self):
        fixture = (ROOT / "tests/core_resource_init_fixture.cpp").read_text(encoding="utf-8")
        for name in ("make_layout", "ensure_ls_layout", "ensure_rf_layout"):
            fixture = fixture.replace("// INSERT " + name, function("core/gi.cpp", name))
        fixture = fixture.replace("// INSERT ensure", function("core/emissive.cpp", "ensure"))
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "init.cpp"
            source.write_text(fixture, encoding="utf-8")
            result = subprocess.run([
                "cl.exe", "/nologo", "/std:c++20", "/EHsc", "/W3", "/MT",
                "/DVK_NO_PROTOTYPES", "/DVK_USE_PLATFORM_WIN32_KHR", "/DNOMINMAX",
                "/DWIN32_LEAN_AND_MEAN", "/I" + str(ROOT),
                "/I" + str(Path(os.environ["VULKAN_SDK"]) / "Include"), str(source),
                "/Fe:" + str(Path(tmp) / "init.exe")],
                cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(Path(tmp) / "init.exe")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_water_shared_state_read_is_locked(self):
        text = (ROOT / "core/water.cpp").read_text(encoding="utf-8")
        body = text[text.index("void water_on_pass_end("):text.index("void water_release(")]
        self.assertLess(body.index("lock(g_mu)"), body.index("&& g_csun"))
        self.assertEqual(body.count("lock(g_mu)"), 1)

    def test_unused_probe_removed_census_preserved(self):
        text = (ROOT / "core/core.cpp").read_text(encoding="utf-8")
        self.assertNotIn("g_probe", text)
        self.assertNotIn("probe.hlsl", text)
        self.assertIn("if (want) census_dump();", text)
        self.assertIn("PassStat& s = g_census[k];", text)


if __name__ == "__main__":
    unittest.main()
