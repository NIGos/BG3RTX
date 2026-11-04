"""Synthetic acceptance-path checks; no game or capture files."""
import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import numpy as np
from unittest.mock import patch
from tools.live_batches.gi_1ray_qual.analyze import temporal_rule, float_samples, trace_costs
from tools import m4_promotion_gate as m4


class FloatSamplesTests(unittest.TestCase):
    def test_trace_selection_keeps_full_log_exposure_and_root_mapping(self):
        with TemporaryDirectory() as tmp:
            run = m4.Run.__new__(m4.Run)
            run.captures_root = Path(tmp)
            run.energy_fs = m4.SCENE_FS
            run.phases = [('shot', 10, 20)]
            run.lines = []
            for i, exposure in enumerate((.25, .5, 1)):
                cap = run.captures_root / str(i)
                cap.mkdir()
                (cap/'dumps.json').write_text(json.dumps(dict(d1=dict(
                    pass_fs=m4.SCENE_FS, slot=0, mean=[exposure]*3, nonfinite=0))))
                # The probe is deliberately separate from the trace line. No
                # PNG or f32 is needed: dumps.json contains float statistics.
                run.lines += [(11+i, f'[rrpost] exposure probe (frame {100+i}): lighting s1b13 = [{exposure}]'),
                              (11+i, f'[trace] frame {100+i} written: 4 submits, 1 images -> absent/{i}')]
            original = list(run.lines)
            values = float_samples(run, 'shot')
            np.testing.assert_allclose([v[0]/v[2] for v in values], [1]*3)
            self.assertEqual(run.lines, original)
            del run._probes
            run.lines = [line for line in original if 'probe' not in line[1]]
            with self.assertRaisesRegex(ValueError, 'exposure'):
                float_samples(run, 'shot')
            self.assertEqual(len(run.lines), 3)


class TraceCostTests(unittest.TestCase):
    def test_reset_makes_first_complete_period_valid(self):
        labels = ['off-a', 'on-a', 'off-b', 'on-b']
        windows = [dict(label='cost-'+label, applied=10, stats_reset=9, end=12)
                   for label in labels]
        line = 'gi: trace pass (ray-traced) GPU median 4.25 ms'
        self.assertEqual(trace_costs([(8, line), (11, line), (12, line)], windows),
                         dict.fromkeys(labels, 4.25))
        # Without reset, a lone line may straddle the settings change.
        for window in windows:
            window['stats_reset'] = None
        with self.assertRaisesRegex(ValueError, 'missing per-state'):
            trace_costs([(8, line), (11, line)], windows)


def result(passed, area=0, score=0, inconclusive=False):
    return dict(pass_=passed, metrics=dict(excess_area=area, inconclusive=inconclusive),
                score=score, threshold=1)


class TemporalRuleTests(unittest.TestCase):
    def test_existing_detector_receipt_is_json_serializable(self):
        rng = np.random.default_rng(42)
        frames = [.3 + rng.normal(0, .001, (48,64,3)).astype('f4') for _ in range(3)]
        sets = dict.fromkeys(['off-a','off-b','on-a','on-b'], frames)
        receipt = temporal_rule(sets, np.zeros((48,64), bool))
        self.assertTrue(receipt['ok'])
        json.dumps(receipt, default=lambda x: x.item())

    def run_rule(self, rows):
        for r in rows:
            r['pass'] = r.pop('pass_')
        with patch('tools.live_batches.gi_1ray_qual.analyze.det.temporal', side_effect=rows):
            return temporal_rule(dict.fromkeys(['off-a','off-b','on-a','on-b'], []), None)

    def test_second_on_repeat_cannot_hide_behind_first(self):
        rows = [result(True) for _ in range(5)] + [result(False, area=1)]
        self.assertFalse(self.run_rule(rows)['ok'])

    def test_failed_null_uses_off_floor_and_pq_limit(self):
        rows = [result(False, 2), result(False, 3)] + [result(False, 3, .5) for _ in range(4)]
        self.assertTrue(self.run_rule(rows)['ok'])
        rows = [result(False, 2), result(False, 3)] + [result(False, 3, 1.01) for _ in range(4)]
        self.assertFalse(self.run_rule(rows)['ok'])

    def test_inconclusive_null_never_licenses_a_pass(self):
        rows = [result(False, inconclusive=True)] + [result(True) for _ in range(5)]
        with self.assertRaisesRegex(ValueError, 'INCONCLUSIVE'):
            self.run_rule(rows)


if __name__ == '__main__':
    unittest.main()
