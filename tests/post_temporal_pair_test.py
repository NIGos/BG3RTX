"""Paired Dialogue3 inference and schedule regression fixtures. No game required."""
import json
import math
from pathlib import Path
import sys
import unittest

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.post_temporal import paired as g
from tools.live_batches.post_temporal.analyze_d3 import frozen_masks
from tools.live_batches.live_flow import load_job, lint

HERE = Path(__file__).resolve().parents[1] / 'tools/live_batches/post_temporal'


def fixture(effect):
    rows = {}
    # Smooth monotonic drift dwarfs the treatment's magnitude across the run.
    # Counterbalanced order makes no-effect differences alternate in sign.
    for i in range(12):
        states = (0,1) if i % 2 == 0 else (1,0)
        for j,on in enumerate(states):
            value = 10 + 3*(2*i+j) - effect*on
            metric = dict(residual_x1000=value, residual_even=value/1000, residual_odd=value/1000)
            rows[f'{"on" if on else "off"}-{i:02d}'] = dict(
                registration={d:{} for d in g.DOMAINS},
                **{d:{r:dict(metric) for r in g.REGIONS} for d in g.DOMAINS})
    return {'windows':rows}


class PairedTest(unittest.TestCase):
    def test_effect_with_drift_passes(self):
        result = g.evaluate(fixture(5))
        self.assertEqual(result['verdict'], 'PASS')
        for obs in result['observables'].values():
            t = obs['comparisons']['residual_x1000']
            self.assertEqual(t['improvements'], 12)
            self.assertEqual(t['p_one_sided'], 1/4096)
            self.assertGreater(t['off_repeat_spread'], 50)
        self.assertFalse(result['quality_pass'])  # energy/S8 still required
        shuffled = fixture(5)
        shuffled['windows'] = dict(sorted(shuffled['windows'].items()))
        with self.assertRaises(ValueError): g.evaluate(shuffled)

    def test_no_effect_with_drift_fails(self):
        result = g.evaluate(fixture(0))
        self.assertEqual(result['verdict'], 'FAIL')
        t = result['observables']['rendered/hair_over_dof']['comparisons']['residual_x1000']
        self.assertEqual(t['improvements'], 6)
        self.assertGreater(t['p_one_sided'], .5)

    def test_existing_run_is_insufficient(self):
        report = json.loads((HERE/'RESULT_20261103_D3_valid.json').read_text())
        result = g.evaluate(report)
        self.assertEqual(result['verdict'], 'INSUFFICIENT')
        self.assertEqual(result['reason'], 'insufficient alternations')
        for obs in result['observables'].values():
            for t in obs['comparisons'].values():
                self.assertEqual(t['pairs'], 2)
                self.assertEqual(t['minimum_pairs'], 8)
        hair = result['observables']['rendered/hair_over_dof']['comparisons']['residual_x1000']
        self.assertAlmostEqual(hair['off_repeat_spread'], 33.2349-7.5164, places=4)
        self.assertFalse(result['qualified_for_ultra'])

    def test_minimum_derived_from_exact_tail(self):
        self.assertEqual(g.MIN_PAIRS, 8)
        self.assertGreater(2**-(g.MIN_PAIRS-1), g.ALPHA/g.FAMILY)
        self.assertLessEqual(2**-g.MIN_PAIRS, g.ALPHA/g.FAMILY)
        self.assertEqual(g.paired_sign([1]*7,[0]*7)['verdict'], 'INSUFFICIENT')
        self.assertEqual(g.paired_sign([1]*8,[0]*8)['verdict'], 'PASS')

    def test_ties_and_nonfinite_never_pass(self):
        self.assertEqual(g.paired_sign([1]*12,[1]*12)['verdict'], 'FAIL')
        self.assertEqual(g.paired_sign([1]*12,[math.nan]*12)['verdict'], 'INVALID')
        self.assertEqual(g.paired_sign([1],[0,0])['verdict'], 'INVALID')

    def test_registration_and_missing_support_block_pass(self):
        data = fixture(5)
        data['windows']['on-00']['registration']['presented']['cross_window_error'] = 'cut'
        self.assertEqual(g.evaluate(data)['verdict'], 'FAIL')
        data = fixture(5)
        data['windows']['on-00']['rendered']['hair_over_dof'] = {'error':'empty frozen OFF support'}
        self.assertEqual(g.evaluate(data)['verdict'], 'FAIL')
        del data['windows']['off-00']
        with self.assertRaises(ValueError): g.evaluate(data)

    def test_each_parity_must_pass(self):
        data = fixture(5)
        for l,row in data['windows'].items():
            if l.startswith('on-'):
                row['rendered']['bright_contour']['residual_odd'] += 1
        self.assertEqual(g.evaluate(data)['verdict'], 'FAIL')

    def test_masks_ignore_on_and_include_all_off(self):
        depth = np.full((64,80,1),10.,dtype='f4');depth[12:52,20:60]=2
        mat = np.zeros((64,80,4),dtype='f4');mat[12:52,20:60,3]=3/255
        final = np.ones((64,80,3),dtype='f4')
        bursts = {l:dict(depth=[depth.copy()]*8, material=[mat.copy()]*2, final=[final.copy()]*8)
                  for l in ('off-00','on-00','off-01')}
        regs = {l:dict(shift_px=[0,0]) for l in bursts}
        off = ['off-00','off-01']
        before,_ = frozen_masks(bursts,regs,off)
        for m in bursts['off-01']['material']:m[12:52,20:23,3]=5/255
        after,_ = frozen_masks(bursts,regs,off)
        self.assertGreater(after['hair_over_dof'].sum(),before['hair_over_dof'].sum())
        bursts['on-00']['material'] = [np.full_like(mat,math.nan)]*2
        unchanged,_ = frozen_masks(bursts,regs,off)
        for key in after: np.testing.assert_array_equal(after[key],unchanged[key])

    def test_schedule_and_duration_bounds(self):
        job,plan,_ = load_job(HERE/'jobs/pt-dialogue3.json')
        # Historical regex corpus is checked separately by live_flow CLI.
        result = lint(job,plan,[])
        self.assertEqual(result['problems'], [])
        self.assertEqual(result['duration']['findings'], [])
        labels = [p['label'][9:] for p in plan if p['label'].startswith('rendered-')]
        self.assertEqual(len(labels), 24)
        for i in range(12):
            expected = [f'off-{i:02d}',f'on-{i:02d}']
            self.assertEqual(labels[2*i:2*i+2], expected if i%2==0 else expected[::-1])
        total = result['duration']['seconds']+result['duration']['readiness_timeout_seconds']+120
        self.assertAlmostEqual(total, 818.9)
        self.assertLess(total, 900)
        self.assertEqual(job['timeout'], 900)
        self.assertTrue(all(p['seconds']==2.6 for p in plan if p['label'].startswith('rendered-')))
        last_quality = max(i for i,p in enumerate(plan) if p['label'].startswith('energy-'))
        self.assertTrue(all(i>last_quality for i,p in enumerate(plan) if p['label'].startswith('timing-')))


if __name__ == '__main__': unittest.main()
