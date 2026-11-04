"""Synthetic hv6 diagnostics: no runtime, queue or Git changes."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts import queue_gates, live_guard
from tools.live_batches.hair_validation import make_job as jobs
from tools.live_batches.hair_validation.analyze import diagnostic_flicker
from tools.live_batches import live_flow

class DiagnosticTests(unittest.TestCase):
    def test_stable_and_flickering_head(self):
        roi=np.zeros((24,24),bool); roi[4:20,4:20]=True
        windows={n:np.full((8,24,24,3),.5) for n in jobs.CAPTURE_WINDOWS}
        self.assertEqual(diagnostic_flicker(windows,roi,1/1023)['status'],'PASS')
        windows['order-gbuffer-2'][1::2,roi]=1
        result=diagnostic_flicker(windows,roi,1/1023)
        self.assertEqual(result['status'],'FAIL')
        self.assertEqual(next(x for x in result['checks'] if x['name'].startswith('order-gbuffer-2'))['status'],'FAIL')
        del windows['cards-c']
        with self.assertRaises(ValueError): diagnostic_flicker(windows,roi,1/1023)

    def test_noisy_background_is_outside_baseline_head(self):
        roi=np.zeros((24,24),bool); roi[4:20,4:20]=True
        windows={n:np.full((8,24,24,3),.5) for n in jobs.CAPTURE_WINDOWS}
        windows['order-gbuffer-2'][1::2,~roi]=1
        self.assertEqual(diagnostic_flicker(windows,roi,1/1023)['status'],'PASS')
        windows['cards-a'][0,roi]=np.nan
        with self.assertRaises(ValueError): diagnostic_flicker(windows,roi,1/1023)

    def test_coverage_failure_aborts_before_window_end(self):
        plan=[dict(label='order-gbuffer-1')]
        gate=jobs.diagnostic_job('test','Test Dialogue1',jobs.make_diagnostic())['expect_every'][1]
        gate=dict(gate,windows=['order-gbuffer-1'])
        prefix='[hair primary] coverage (last 300 frames): 2 draws probed, 1 swapped; per draw (key prefix: newest strand pixels/card samples = ratio, swapped frames of the period): '
        good=prefix+'abcd:10/100=0.100,0 ef01:90/100=0.900,30; ratio min 0.100, bound 0.800, upper 1.094, draws outside bounds 1'
        bad=good.replace('0.100,0','0.100,1')
        self.assertFalse(queue_gates.line_matches(gate,good.replace('90/100=0.900,30','255/100=2.550,30')))   # above the upper bound
        self.assertTrue(queue_gates.line_matches(gate,good))
        self.assertFalse(queue_gates.line_matches(gate,bad))
        runner='10:00:00.000 [pan-plan] window 0'
        log='10:00:00.100 [cfg] settings read\n10:00:01.000 '+good+'\n10:00:11.000 '+bad
        failure=live_guard.evaluate(dict(expect_every=[gate]),plan,log,runner,0,12)
        self.assertIn('line(s) fail /',failure['check'])

    def test_job_caps_and_toggle_order(self):
        root=Path(__file__).resolve().parents[1]/'tools/live_batches/hair_validation'
        for n in ('hv5-reload-bisect','hv6-strand-diagnostic'):
            job,plan,_=live_flow.load_job(root/'jobs'/(n+'.json'))
            b=live_flow.duration_budget(job,plan)
            self.assertLessEqual(b['seconds']+b['expected_readiness_seconds'],780)  # the allowance is only a timeout (median accounting)
            self.assertFalse(live_flow.launch_problems(job,b))
        plan=jobs.make_diagnostic()
        self.assertEqual([live_flow.settings(p['set'])['rt_hair_groom_primary_order'] for p in plan if p['label'].startswith('order-')],['0','1','0','1'])
        self.assertEqual(sum(bool(p.get('reload_selected')) for p in jobs.make_bisect()),len(jobs.STAGES))
        self.assertFalse(any(p['label'].startswith('order-') for p in jobs.make_bisect()))

if __name__=='__main__': unittest.main()
