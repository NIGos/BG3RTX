"""Fail-closed QA evidence and emission regressions; no game required."""
import contextlib
import io
import json
import sys
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.qa import analyze, em_gate
from scripts.qa.decode import Trace
from PIL import Image


class EmissionEvidence(unittest.TestCase):
    def evaluate(self, ratios):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "em.log"
            log.write_text("\n".join(f"[em] ours/game per family deadbeef test {r} (100 bright px of 100)" for r in ratios))
            with contextlib.redirect_stdout(io.StringIO()):
                return em_gate.evaluate(log)

    def test_no_cli_inputs_invalid(self):
        result = subprocess.run([sys.executable, "-m", "scripts.qa.em_gate"],
                                cwd=Path(__file__).resolve().parents[1], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)

    def test_empty_invalid(self):
        self.assertFalse(self.evaluate([]))

    def test_two_windows_invalid(self):
        self.assertFalse(self.evaluate([1, 1]))

    def test_zero_ratio_fails(self):
        self.assertFalse(self.evaluate([1, 0, 1]))

    def test_supported_unit_ratio_passes(self):
        self.assertTrue(self.evaluate([1, 1, 1]))


class NonfiniteEvidence(unittest.TestCase):
    def check(self, off, on):
        with tempfile.TemporaryDirectory() as temp:
            traces = {}
            for name, dumps in (("off0", off), ("on0", on)):
                folder = Path(temp) / name
                folder.mkdir()
                (folder / "dumps.json").write_text(json.dumps(dumps))
                traces[name] = Trace(folder)
            return analyze.nonfinite_check(traces)

    def dump(self, count, **kw):
        return dict(fmt="D32F", pass_fs="deadbeef00000001", slot="depth", w=16, h=16, nonfinite=count, **kw)

    def test_on_excess_fails_same_signature(self):
        result = self.check({"b0": self.dump(1)}, {"b0": self.dump(1000)})
        self.assertFalse(result["pass"])
        self.assertEqual(result["score"], 999)

    def test_equal_game_counts_pass(self):
        self.assertTrue(self.check({"b0": self.dump(10)}, {"b0": self.dump(10)})["pass"])

    def test_full_shader_identity(self):
        on = self.dump(10)
        on["pass_fs"] = "deadbeef00000002"
        self.assertFalse(self.check({"b0": self.dump(10)}, {"b0": on})["pass"])

    def test_separate_dump_support(self):
        self.assertFalse(self.check({"b0": self.dump(10), "b1": self.dump(0)},
                                    {"b0": self.dump(0), "b1": self.dump(10)})["pass"])

    def test_image_support_must_match(self):
        on = self.dump(10)
        on["w"] = 32
        self.assertFalse(self.check({"b0": self.dump(10)}, {"b0": on})["pass"])


class MissingCaptures(unittest.TestCase):
    def test_missing_repeat_or_image_with_surviving_captures(self):
        for fault in ("repeat", "image"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                names = ("on0", "on1", "off0", "off1")
                for name in names:
                    trace = folder / "raw" / name
                    trace.mkdir(parents=True)
                    dump = dict(fmt="RGB10A2", pass_fs="d25b808600000000", slot=0, w=2, h=2,
                                png="final.png", min=[0, 0, 0], max=[1, 1, 1], nonfinite=0)
                    (trace / "dumps.json").write_text(json.dumps({"b0": dump}))
                    Image.new("RGB", (2, 2)).save(trace / "final.png")
                if fault == "repeat":
                    (folder / "raw/off1/dumps.json").unlink()
                else:
                    (folder / "raw/off1/final.png").unlink()
                (folder / "run.json").write_text(json.dumps(dict(scene="fixture", save="synthetic", ok=True,
                    phases=[[n, {}] for n in names], traces={n: n for n in names})))
                with ThreadPoolExecutor(1) as pool:
                    result = analyze.analyze_scene(folder, pool)
                self.assertFalse(result["run_ok"])
                self.assertIn(": INVALID", result["summary"])
                self.assertEqual(set(result["traces"]), {"on0", "on1", "off0"})

    def test_missing_captures_override_successful_metadata(self):
        for traces in ({}, {"on0": "on0", "off0": "off0"}):
            with self.subTest(traces=traces), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                (folder / "run.json").write_text(json.dumps(dict(scene="fixture", save="synthetic", ok=True,
                    phases=[[n, {}] for n in ("on0", "on1", "off0", "off1")], traces=traces)))
                with ThreadPoolExecutor(1) as pool:
                    result = analyze.analyze_scene(folder, pool)
                self.assertFalse(result["run_ok"])
                self.assertIn(": INVALID", result["summary"])
                self.assertTrue((folder / "result.json").exists())


if __name__ == "__main__":
    unittest.main()
