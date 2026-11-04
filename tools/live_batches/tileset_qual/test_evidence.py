"""Synthetic evidence checks; no queue or game access."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates
from tools.live_batches.tileset_qual.analyze import compare, producer_lines

HERE = Path(__file__).resolve().parent


class EvidenceTests(unittest.TestCase):
    def test_every_tileset_record_must_be_valid(self):
        job = json.loads((HERE / 'jobs/ts1-native-tileset-on.json').read_text())
        gate = next(g for g in job['expect_every'] if g['name'].startswith('Tileset records'))
        plan = [dict(label='diag-on', seconds=10), dict(label='restore', seconds=1)]
        runner = ('10:00:00.000 [pan-plan] window 0 serial 0 set=rt_stats=1\n'
                  '10:00:10.000 [pan-plan] window 1 serial 0 set=rt_stats=0\n'
                  '10:00:11.000 [motion-plan] complete phases=2')
        good = '[alpha tileset] records complete 56 unknown 0 native samplers 2 capture-incomplete 0 stride 88'
        def check(last):
            game = '\n'.join(f'10:00:0{t}.000 {line}' for t, line in [(1, good), (3, good), (5, last)])
            return queue_gates.check([gate], game, runner, plan)
        self.assertEqual(check(good), [])
        self.assertEqual(check(good.replace('unknown 0', 'unknown 33')), [])
        for bad in [good.replace('complete 56', 'complete 0'),
                    good.replace('capture-incomplete 0', 'capture-incomplete 1')]:
            with self.subTest(bad=bad):
                self.assertTrue(check(bad))
        self.assertTrue(queue_gates.check([gate], '', runner, plan))

    def test_reset_aligned_single_period_and_legacy_straddle(self):
        lines = [(9, 'GPU cost 99'), (12, 'GPU cost 9.242'), (15, 'GPU cost 88')]
        window = dict(applied=10, end=15, stats_reset=10)
        self.assertEqual(producer_lines(lines, window, 'GPU cost'), ['GPU cost 9.242'])
        self.assertEqual(producer_lines(lines, dict(window, stats_reset=None), 'GPU cost'), [])
        self.assertEqual(producer_lines(lines, window, 'missing'), [])
        self.assertEqual(producer_lines(lines, dict(window, stats_reset=13), 'GPU cost'), [])

    def test_diagnostic_debug_is_restored_and_cost_waits_one_period(self):
        from tools.live_batches.live_flow import load_job, phase_states
        for suffix in ('off', 'on'):
            job, plan, _ = load_job(HERE / 'jobs' / f'ts1-native-tileset-{suffix}.json')
            states = list(phase_states(job, plan))
            self.assertEqual(states[2]['rt_stats'], '1')
            self.assertEqual(states[2]['rt_debug'], '9' if suffix == 'on' else '0')
            for state in states[3:]:
                self.assertEqual(state['rt_debug'], '0')
                self.assertEqual(state['rt_stats'], '0')
            cost = plan[3]
            self.assertTrue(cost['stats_reset'])
            self.assertEqual(cost['frames'], int(states[3]['rt_stats_period_frames']))
            self.assertIn('rt passes: GPU cost', cost['wait_stats'])
            self.assertIn('gi: GPU cost', cost['wait_stats'])

    def test_shading_energy_is_descriptive_and_exposure_normalized(self):
        off = dict(evidence_status='VALID_TECHNICAL', float_pre_post=[2, 0, 2],
                   gpu=dict(rt_ms=10, gi_ms=4), coverage=['off'])
        on = dict(evidence_status='VALID_TECHNICAL', float_pre_post=[6, 0, 3],
                  gpu=dict(rt_ms=12, gi_ms=5), coverage=['on'])
        result = compare(off, on)
        self.assertEqual(result['energy_relative_delta'], 2)
        self.assertEqual(result['exposure_normalized_relative_delta'], 1)
        self.assertEqual(result['gpu_delta_ms'], dict(rt_ms=2, gi_ms=1))
        self.assertEqual(result['quality_status'], 'AWAITING_USER_LOOK')

    def test_invalid_launch_cannot_be_compared(self):
        with self.assertRaises(ValueError):
            compare(dict(evidence_status='INVALID'), dict(evidence_status='VALID_TECHNICAL'))


if __name__ == '__main__':
    unittest.main()
