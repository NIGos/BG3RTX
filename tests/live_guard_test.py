"""Synthetic early-rejection checks. No queue, game, or user settings access."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.live_guard import evaluate, evaluate_session


def stamp(sec, text):
    return f"00:00:{sec:06.3f} {text}\n"


class LiveGuardTest(unittest.TestCase):
    def setUp(self):
        self.plan = [dict(label="diag", seconds=10, set="rt_stats=1"),
                     dict(label="reset", seconds=1, set="rt_stats=0"),
                     dict(label="next", seconds=10, set="rt_stats=1")]
        self.job = dict(set="rt_stats=1", window_mode="visible_unfocused_fg", forbid=["BROKEN"], expect=[],
                        expect_every=[dict(select="stats", match="good", windows=["diag"])],
                        live_flow=dict(fps_windows=[dict(state=[1,0,0,0,0], fps=30, windows=1, logs=["synthetic"])]))
        self.runner = stamp(1, "[pan-plan] window 0 set=rt_stats=1")

    def run_guard(self, game="", now=5, phase=0, runner=None, failed=()):
        return evaluate(self.job, self.plan, game, self.runner if runner is None else runner, phase, now, failed)

    def test_readiness_excludes_bad_samples_until_complete(self):
        self.plan[0].update(wait_log=["ready"], wait_log_timeout=30)
        game = stamp(2, "[cfg] settings read") + stamp(3, "stats bad") + stamp(5, "stats bad")
        self.assertIsNone(self.run_guard(game, now=8))
        runner = self.runner + stamp(8, "[motion-ready] phase=0")
        game += stamp(9, "stats good") + stamp(10, "stats good")
        self.assertIsNone(self.run_guard(game, now=10, runner=runner))
        from scripts.queue_gates import check
        end = runner + stamp(11, "[motion-plan] complete phases=1")
        self.assertEqual(check(self.job["expect_every"], game, end, self.plan), [])
        missing = self.runner + stamp(11, "[motion-plan] complete phases=1")
        self.assertIn("missing motion-ready", check(self.job["expect_every"], game, missing, self.plan)[0])

    def test_burst_gate_keeps_motion_ready_cutoff(self):
        from scripts.queue_gates import check
        self.plan[0].update(wait_log=['ready'], wait_log_timeout=30)
        gate = dict(select=r'burst 98603 completed', match='completed', event=True, windows=['diag'])
        runner = self.runner + stamp(8, '[motion-ready] phase=0') + stamp(11, '[motion-plan] complete')
        game = stamp(2, '[cfg] settings read') + stamp(7.863, 'burst 98603 completed')
        self.assertTrue(check([gate], game, runner, self.plan))
        self.assertEqual(check([gate], game+stamp(8.1, 'burst 98603 completed'), runner, self.plan), [])

    def test_first_only_and_shared_item_semantics(self):
        from scripts.queue_gates import check
        gate = dict(select=r"strand draws", match=r"0 strand draws", first_only=True)
        self.job['expect_every'] = [gate]
        game = stamp(2, "0 strand draws") + stamp(3, "12 strand draws")
        self.assertEqual(check([gate], game), [])
        self.assertIsNone(self.run_guard(game))
        bad = stamp(2, "12 strand draws") + stamp(3, "0 strand draws")
        self.assertIsNotNone(self.run_guard(bad))
        self.job['expect_every'] = [dict(select='coverage', match='coverage',
            items=dict(pattern=r'active=(?P<active>\d+) value=(?P<value>\d+)', bound=r'bound=(\d+)'))]
        self.assertIsNotNone(self.run_guard(stamp(2, 'coverage bound=5 active=1 value=4')))
        self.assertIsNone(self.run_guard(stamp(2, 'coverage bound=5 active=1 value=5')))

    def test_generated_hv6_startup_gate(self):
        from tools.live_batches.hair_validation.make_job import make_diagnostic, diagnostic_job
        from scripts.queue_gates import check
        plan = make_diagnostic()
        generated = diagnostic_job('hv6-strand-diagnostic', 'Test Dialogue1', plan)
        gate = next(g for g in generated['expect_every'] if g.get('first_only'))
        self.job['expect_every'] = [gate]
        game = (stamp(2, '[hair primary] frame 1: 0 G-buffer and 0 velocity strand draws') +
                stamp(3, '[hair primary] frame 2: 15 G-buffer and 15 velocity strand draws'))
        self.assertEqual(check([gate], game), [])
        self.assertIsNone(self.run_guard(game))
        self.assertIsNotNone(self.run_guard(game.splitlines()[1]+'\n'))

    def test_forbid_and_optout(self):
        r = self.run_guard(stamp(2, "BROKEN"))
        self.assertEqual(r, dict(check="forbidden log pattern: BROKEN", phase=0, slice=-1, next_phase=-1))
        self.job["fail_fast"] = False
        self.assertIsNone(self.run_guard(stamp(2, "BROKEN")))

    def test_closed_window_settles_then_aborts(self):
        runner = self.runner + stamp(11, "[pan-plan] window 1 set=rt_stats=0")
        self.assertIsNone(self.run_guard(now=11.9, phase=1, runner=runner))
        r = self.run_guard(now=12, phase=1, runner=runner)
        self.assertEqual(r["phase"], 0)
        self.assertIn("need 1", r["check"])

    def test_complete_samples_and_settings_application(self):
        runner = self.runner + stamp(11, "[pan-plan] window 1 set=rt_stats=0")
        # One complete interval after actual settings read is required.
        game = stamp(2, "[cfg] settings read") + stamp(3, "stats good") + stamp(7, "stats good")
        self.assertIsNone(self.run_guard(game, 12, 1, runner))
        self.assertIsNotNone(self.run_guard(stamp(3, "stats good"), 12, 1, runner))

    def test_final_window_is_checked(self):
        runner = stamp(1, "[pan-plan] window 2 set=rt_stats=1") + stamp(11, "[motion-plan] complete phases=3")
        self.job["expect_every"][0]["windows"] = ["next"]
        self.assertIn("need 1", self.run_guard(now=12, phase=2, runner=runner)["check"])

    def test_bad_complete_sample_fails_while_window_open(self):
        game = stamp(2, "stats good") + stamp(6, "stats bad")
        self.assertIn("line(s) fail", self.run_guard(game, now=6)["check"])
        # The first straddling sample remains exempt.
        self.assertIsNone(self.run_guard(stamp(2, "stats bad"), now=3))

    def test_silent_producer_requires_useful_full_expect(self):
        pattern = r"\[hit material paks join\].*draws [1-9]"
        self.job["expect"] = [pattern]
        self.job["expect_every"] = []
        game = stamp(2, "[hit material paks join] attempts 0 draws 0")
        self.assertIsNone(self.run_guard(game, 6.9))
        self.assertEqual(self.run_guard(game, 7)["check"], "producer silent: " + pattern)
        self.assertIsNone(self.run_guard(game + stamp(6, "[hit material paks join] draws 2"), 7))

    def test_producer_not_requested_during_off_warmup(self):
        pattern = r"\[rrpost\] gpu: .*active=1"
        self.job["expect"] = [pattern]
        self.job["expect_every"] = [dict(select=r"\[rrpost\] gpu:", match="active=1", windows=["next"])]
        self.assertIsNone(self.run_guard(now=50))
        runner = self.runner + stamp(51, "[pan-plan] window 2 set=rt_stats=1")
        self.assertIsNone(self.run_guard(now=57, phase=2, runner=runner))
        self.assertEqual(self.run_guard(now=58, phase=2, runner=runner)["check"], "producer silent: " + pattern)

    def combined(self):
        self.job["fail_fast_slices"] = [dict(offset=0, count=1, forbid=["BROKEN"], expect=[]),
                                        dict(offset=2, count=1, forbid=[], expect=[])]

    def test_slice_failure_jumps_to_settings_reset(self):
        self.combined()
        self.assertEqual(self.run_guard(stamp(2, "BROKEN"))["next_phase"], 1)
        runner = self.runner + stamp(11, "[pan-plan] window 1 set=rt_stats=0") + stamp(12, "[pan-plan] window 2 set=rt_stats=1")
        self.assertIsNone(self.run_guard(stamp(2, "BROKEN"), 14, 2, runner, failed=(0,)))

    def test_late_closed_failure_does_not_rewind_next_slice(self):
        self.combined()
        runner = self.runner + stamp(11, "[pan-plan] window 1 set=rt_stats=0") + stamp(12, "[pan-plan] window 2 set=rt_stats=1")
        r = self.run_guard(now=13, phase=2, runner=runner)
        self.assertEqual((r["phase"], r["slice"], r["next_phase"]), (0, 0, 2))

    def test_first_minute_preamble_and_global_failure(self):
        self.combined()
        r = self.run_guard(stamp(0, "BROKEN"), now=0.5, phase=-1, runner="")
        self.assertEqual((r["slice"], r["next_phase"]), (0, 1))
        self.assertIsNone(self.run_guard(stamp(0, "BROKEN"), now=0.5, phase=-1, runner="", failed=(0,)))
        r = self.run_guard(stamp(0, "[fault] VK_ERROR_DEVICE_LOST from vkQueueSubmit"), phase=-1, runner="")
        self.assertEqual((r["slice"], r["next_phase"]), (-1, -1))

    def test_device_loss_zero_diagnostic_is_not_a_global_failure(self):
        self.assertIsNone(self.run_guard(stamp(2, "[diagnostics] device_lost count 0")))
        self.assertIsNone(self.run_guard(stamp(2, "[diagnostics] VK_ERROR_DEVICE_LOST count 0")))
        r = self.run_guard(stamp(2, "[aftermath] device lost: waiting for crash dump"))
        self.assertEqual((r["slice"], r["next_phase"]), (-1, -1))

    def test_zero_tolerance_and_when_present(self):
        self.job["expect_every"] = [dict(select="overflow", match=r"(?!)", event=True, min=0, windows=["diag"])]
        runner = self.runner + stamp(11, "[pan-plan] window 1 set=rt_stats=0")
        self.assertIsNone(self.run_guard(now=12, phase=1, runner=runner))
        self.assertIsNotNone(self.run_guard(stamp(3, "overflow"), 12, 1, runner))

    def test_producer_sanity_precedes_measurement_and_pipeline_wait(self):
        pattern = r"\[hit material paks join\].*draws [1-9]"
        self.plan[0].update(label="producer-sanity-join", wait_log=[pattern], wait_log_timeout=9)
        self.job["expect_every"] = []
        self.job["expect"] = [pattern]
        self.assertEqual(self.run_guard(stamp(2, "[hit material paks join] draws 0"), now=7)["check"], "producer silent: " + pattern)
        # A first-use compile wait must finish before ordinary producer deadlines.
        self.plan[0]["label"] = "warm-on"
        self.assertIsNone(self.run_guard(now=40))
        ready = self.runner + stamp(41, "[motion-ready] phase=0 label=warm-on")
        self.assertIsNone(self.run_guard(now=46, runner=ready))
        self.assertEqual(self.run_guard(now=47, runner=ready)["check"], "producer silent: " + pattern)

    def test_session_silence_and_optout(self):
        pattern = r"\[hit material paks join\].*draws [1-9]"
        self.job["expect"] = [pattern]
        self.job["expect_every"] = []
        self.assertIsNone(evaluate_session(self.job, "", 5.9))
        self.assertEqual(evaluate_session(self.job, "", 6)["check"], "producer silent: " + pattern)
        self.job["fail_fast"] = False
        self.assertIsNone(evaluate_session(self.job, "BROKEN", 100))

    def test_explicit_gate_cadence_is_used_for_new_producer(self):
        pattern = r"\[hair primary\] frame \d+: [1-9]\d* grooms published"
        self.job["expect"] = [pattern]
        self.job["expect_every"] = [dict(select=r"\[hair primary\] frame", match=pattern,
                                         period_frames=300, windows=["next"])]
        self.plan[0].update(label="producer-sanity-strands", wait_log=[pattern], wait_log_timeout=15)
        self.assertIsNone(self.run_guard(now=12.9))
        self.assertEqual(self.run_guard(now=13)["check"], "producer silent: " + pattern)


if __name__ == "__main__":
    unittest.main()
