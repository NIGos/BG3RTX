"""A failing spirv-opt in the release equivalence gate (tools/release_shader_equivalence.py, tools/spirv_equivalence.py).

    py -3 tests/release_shader_equivalence_test.py

The 2026-11-20 --freeze-spec run died after about 30 minutes with "spirv-opt (...): " and nothing else: a crashed
spirv-opt prints no text and the exception ended the whole gate without naming the request. No DXC or spirv-opt runs
here: subprocess.run and the normalizer are fakes.
"""
import queue
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import release_shader_equivalence as rse  # noqa: E402
from tools import spirv_equivalence as se  # noqa: E402

CRASH = 0xC0000005


class Tool:
    dxc = Path("C:/sdk/bin/dxc.exe")


def fake_run(fail=lambda passes: None):
    """subprocess.run stand-in: copies the input to -o unless fail(passes) returns an exit code."""
    calls = []

    def run(cmd, capture_output=True, text=True):
        passes = cmd[1:-3]
        calls.append(passes)
        code = fail(passes)
        if code:
            return subprocess.CompletedProcess(cmd, code, "", "")
        Path(cmd[-1]).write_bytes(Path(cmd[-3]).read_bytes())
        return subprocess.CompletedProcess(cmd, 0, "", "")
    return run, calls


class Opt(unittest.TestCase):
    def test_crash_is_named(self):
        run, _ = fake_run(lambda p: CRASH)
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(se.subprocess, "run", run):
            src = Path(tmp) / "a.spv"
            src.write_bytes(b"\0" * 20)
            with self.assertRaises(se.OptError) as e:
                se.opt(Tool, src, Path(tmp) / "b.spv", "-O", stage="optimize 2")
        text = str(e.exception)
        self.assertTrue(e.exception.crashed)
        self.assertIn("optimize 2", text)
        self.assertIn("0xC0000005 access violation", text)
        self.assertIn("(no output)", text)

    def test_error_text_is_kept(self):
        def run(cmd, capture_output=True, text=True):
            return subprocess.CompletedProcess(cmd, 1, "", "error: invalid id 7")
        with mock.patch.object(se.subprocess, "run", run):
            with self.assertRaises(se.OptError) as e:
                se.opt(Tool, "a.spv", "b.spv", "--freeze-spec-const", stage="freeze")
        self.assertFalse(e.exception.crashed)
        self.assertIn("exit 1: error: invalid id 7", str(e.exception))

    def test_schedules(self):
        for schedule, rounds in (("full", 2), ("single", 1)):
            run, calls = fake_run()
            with tempfile.TemporaryDirectory() as tmp, mock.patch.object(se.subprocess, "run", run):
                src = Path(tmp) / "a.spv"
                src.write_bytes(b"\3\2\x23\7" + b"\0" * 16)
                out = se.specialize(Tool, src, Path(tmp) / "out.spv", {1000: 0}, schedule=schedule)
                self.assertTrue(Path(out).exists())
            self.assertEqual(sum(c[:1] == ["-O"] for c in calls), rounds, schedule)
        # The default schedule is the one the gate always ran: assign, freeze, -O twice.
        run, calls = fake_run()
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(se.subprocess, "run", run):
            src = Path(tmp) / "a.spv"
            src.write_bytes(b"\0" * 20)
            se.specialize(Tool, src, Path(tmp) / "out.spv", {1000: 1})
        self.assertEqual(calls, [["--set-spec-const-default-value=1000:1"],
                                 ["--freeze-spec-const", "--fold-spec-const-op-composite"],
                                 list(se.OPTIMIZE), list(se.OPTIMIZE)])

    def test_stage_names_the_failing_round(self):
        rounds = []

        def second_round_crashes(passes):
            if passes == list(se.OPTIMIZE):
                rounds.append(1)
                return CRASH if len(rounds) == 2 else 0
            return 0
        run, _ = fake_run(second_round_crashes)
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(se.subprocess, "run", run):
            src = Path(tmp) / "a.spv"
            src.write_bytes(b"\0" * 20)
            with self.assertRaises(se.OptError) as e:
                se.specialize(Tool, src, Path(tmp) / "out.spv", {})
        self.assertEqual(e.exception.stage, "optimize 2")


class FakeNormalizer:
    """get() fails with a crash for the listed (blob, schedule) pairs, else returns a digest of the blob."""

    def __init__(self, directory, fail):
        self.directory, self.fail, self.calls = Path(directory), fail, []

    def get(self, blob, assign, temp, name, full=False, schedule="full"):
        self.calls.append((blob, schedule))
        if (blob, schedule) in self.fail:
            raise se.OptError(se.OPTIMIZE, CRASH, "", "optimize 2")
        return "same" if blob in (b"A", b"B") else blob.decode()


class Pair(unittest.TestCase):
    def test_fallback_on_both_sides(self):
        with tempfile.TemporaryDirectory() as tmp:
            n = FakeNormalizer(tmp, {(b"B", "full")})
            da, db, reason = rse.normalized_pair(n, b"A", b"B", {}, Path(tmp))
            self.assertEqual((da, db), ("same", "same"))
            self.assertIn("current", reason)
            self.assertIn("0xC0000005", reason)
            # The fallback ran the same schedule on both sides (a proof only then).
            self.assertIn((b"A", "single"), n.calls)
            self.assertIn((b"B", "single"), n.calls)
            self.assertTrue(list((Path(tmp) / "failed").glob("*.spv")))

    def test_both_schedules_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            n = FakeNormalizer(tmp, {(b"A", "full"), (b"A", "single")})
            with self.assertRaises(se.OptError) as e:
                rse.normalized_pair(n, b"A", b"B", {}, Path(tmp))
            self.assertEqual(e.exception.side, "baseline")
            self.assertIn("baseline", e.exception.first)


class Request(unittest.TestCase):
    def run_request(self, fail):
        with tempfile.TemporaryDirectory() as tmp:
            progress = queue.Queue()
            rse._WORKER_NORMALIZER, rse._WORKER_PROGRESS = FakeNormalizer(tmp, fail), progress
            values = rse.assignments({1029, 1051}, set())
            item = ("gi/x.hlsl?A=1", [("main", b"A", b"B", values), ("other", b"C", b"D", values)],
                    2 * len(values), [], False, {1054: 0})
            r, row = rse.check_request(item)
            steps = []
            while not progress.empty():
                steps.append(progress.get())
        return r, row, steps, len(values)

    def test_error_names_request_and_continues_counting(self):
        r, row, steps, n = self.run_request({(b"C", "full"), (b"C", "single")})
        self.assertEqual(r, "gi/x.hlsl?A=1")
        self.assertEqual(row["state"], "ERROR")
        self.assertIn("other", row["detail"])
        self.assertIn("baseline module", row["detail"])
        self.assertIn("0xC0000005 access violation", row["detail"])
        # Every assignment of the request reached the progress counter; the unproven ones as failures.
        self.assertEqual(len(steps), 2 * n)
        self.assertEqual(sum(steps), n)

    def test_fallback_is_recorded(self):
        _, row, steps, n = self.run_request({(b"B", "full")})
        self.assertEqual(row["state"], "DIFFERENT")   # C and D differ in the fake
        self.assertTrue(row["fallbacks"])
        self.assertEqual(row["fallbacks"][0]["entry"], "main")
        self.assertEqual(len(steps), 2 * n)
        # "main" is universal (2 explicit checks); "other" fails its universal check and stops at the first of its
        # n Cartesian assignments: the first difference decides, the rest count as failed.
        self.assertEqual(row["actual_frozen_checks"], 3)
        self.assertEqual(sum(steps), n)


if __name__ == "__main__":
    unittest.main()
