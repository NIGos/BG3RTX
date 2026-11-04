"""Synthetic acceptance-path checks; no game or capture files."""
import unittest
import json
import numpy as np
from unittest.mock import patch
from tools.live_batches.gi_1ray_qual.analyze import temporal_rule


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
