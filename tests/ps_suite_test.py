"""Runs the offline PowerShell tests of the queue runner and the scripts through CTest.

Until 2026-11-14 nothing ran them: queue_child_permission_test.ps1 had been failing since 2026-10-31 and nobody saw it.
Every test here is CPU only and works in a scratch folder under TEMP (its first line says so): no game, no real queue
inbox, no runner entry point. Not in the list, on purpose: queue_cancellation_test.ps1 and
queue_child_startprocess_test.ps1 start the real test-queue.ps1 against a stand-in tree (run them by hand before a
change to the runner), and readiness_cached_replay.ps1 needs a folder of real logs."""
import pathlib
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TESTS = ("auto_run_trace_test", "camera_plan_fields_test", "camera_plan_test", "hair_reload_test",
         "live_guard_restore_test", "merge_green_recovery_test", "move_plan_array_test", "queue_build_identity_test",
         "queue_child_permission_test", "queue_completion_test", "queue_permission_test", "queue_preflight_test",
         "queue_shared_marker_test", "queue_window_mode_test", "se_console_probe_test", "se_probe_permission_test")
BY_HAND = {"queue_cancellation_test", "queue_child_startprocess_test", "readiness_cached_replay"}


class PowerShellSuite(unittest.TestCase):
    def test_every_script_is_listed(self):
        found = {p.stem for p in (ROOT / "tests").glob("*.ps1")}
        self.assertEqual(found, set(TESTS) | BY_HAND, "a new tests/*.ps1 must be added to TESTS or BY_HAND")

    def test_scripts_pass(self):
        ps = shutil.which("powershell")
        self.assertIsNotNone(ps)
        for name in TESTS:
            with self.subTest(name=name):
                r = subprocess.run([ps, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                                    str(ROOT / "tests" / (name + ".ps1"))], cwd=ROOT, capture_output=True, text=True,
                                   timeout=300)
                print(f"{name}: exit {r.returncode}", flush=True)
                self.assertEqual(r.returncode, 0, (r.stdout + r.stderr)[-1500:])


if __name__ == "__main__":
    unittest.main()
