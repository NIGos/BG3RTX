"""CPU4 preparation contracts; never accesses the live queue."""
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, pipeline_readiness
from tools.live_batches.opt_r5.cpu4 import analyze, make_job

class Cpu4JobsTests(unittest.TestCase):
    def test_qualified_defaults_and_layer_only_toggle(self):
        defaults = flow.settings((ROOT/'bg3rtx.ini').read_text().replace('\n', ','))
        self.assertEqual(defaults['cpu_opt1'], '1')
        self.assertEqual(defaults['cpu_opt2'], '1')
        self.assertEqual(defaults['cpu_opt3'], '1')
        self.assertEqual(defaults['cpu_opt4'], '0')
        self.assertIn('cpu_opt4', pipeline_readiness.NO_PIPELINE)

    def test_one_inn_session_quiet_readiness_and_complete_cpu_windows(self):
        jobs = list((make_job.HERE/'jobs').glob('*.json'))
        self.assertEqual(len(jobs), 1)
        job, plan, _ = flow.load_job(jobs[0])
        self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
        state = flow.settings(job['set'])
        self.assertEqual((state['cpu_opt1'], state['cpu_opt2'], state['cpu_opt3']), ('1','1','1'))
        self.assertEqual(state['preset'], 'ultra')
        self.assertEqual(state['perf_hook_state'], '0')
        self.assertEqual(state['rt_alpha_stream'], '1')
        self.assertEqual(state['cpu_opt4'], '0')
        measured = [p for p in plan if p.get('frames')]
        self.assertEqual([flow.settings(p['set']) for p in measured],
                         [dict(cpu_opt4=v) for v in ('0','1','0','1')])
        for p in measured:
            self.assertEqual(p['frames'], 600)
            self.assertTrue(p['stats_reset'])
        for p in plan:
            if p['label'].startswith('ready-'):
                self.assertEqual(p['wait_log'], [r'\[watch\].*\| pipelines 0 \(0 ms in driver\)'])
                self.assertEqual(p['wait_log_fresh'], p['wait_log'])
        labels = [p['label'] for p in measured]
        for gate in job['expect_every']:
            self.assertEqual(gate['windows'], labels)
            self.assertEqual(gate['period_frames'], 120)
            self.assertGreaterEqual(gate['min'], 3)
        names = list(analyze.EXTRA)+['capture pre-draw', 'capture pass after (includes scene recording)',
                                    'bvh per-draw BLAS keys', 'bvh TLAS instances']
        rows = {label: {name: dict(n=2) for name in names} for label in labels}
        self.assertEqual(analyze.missing_required(rows), [])
        rows[labels[-1]]['capture cascade sets (attribution)']['n'] = 1
        self.assertTrue(any('capture cascade sets (attribution)' in error for error in analyze.missing_required(rows)))
        self.assertTrue(analyze.missing_required({}))

    def test_attribution_receipts_and_missing_period_rejection(self):
        fields = ('prepass observe', 'cascade sets', 'draw plan', 'alpha lookup',
                  'opaque material', 'bvh record', 'xfb commands', 'cascade draw', 'emissive draw', 'alpha material')
        plan = [dict(label='off-a--static-a', frames=600, stats_reset=True)]
        runner = ('10:00:00.000 [pan-plan] window 0\n'
                  '10:00:02.100 [motion-ready] phase=0 stats_reset=7 frame=100\n'
                  '10:00:18.000 [motion-plan] complete')
        log = ('10:00:00.100 [cfg] settings read\n'
               '10:00:02.000 [stats] reset token=7 frame=100\n')
        for n in range(3):
            log += f'10:00:{6+4*n:02d}.000 [cpu] capture attribution over 120 frames, ms/frame: '
            log += ', '.join(f'{field} {i/1000:.3f}' for i,field in enumerate(fields,1))+'\n'
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'log').write_text(log)
            (p/'run').write_text(runner)
            row = analyze.base.analyze(p/'log', p/'run', plan)['off-a--static-a']
        for i,field in enumerate(fields,1):
            self.assertEqual(row['capture '+field+' (attribution)']['median'], i/1000)
            self.assertGreaterEqual(row['capture '+field+' (attribution)']['n'],2)
        self.assertTrue(analyze.missing_required({'off-a--static-a':row}))

    def test_cost_gate_uses_both_on_windows_and_off_repeat_spread(self):
        names = ('capture pre-draw', 'capture hook bodies (driver draw excluded)')
        labels = [x+'--static-a' for x in ('off-a', 'on-a', 'off-b', 'on-b')]
        def rows(values):
            return {label: {name: dict(n=3, median=value) for name in names}
                    for label, value in zip(labels, values)}
        self.assertEqual(analyze.cost_decision(rows((10.,9.5,10.1,9.6)))[0], 'KEPT_CPU_COST')
        self.assertEqual(analyze.cost_decision(rows((10.,9.95,10.1,9.95)))[0], 'NO_SAVING')
        self.assertEqual(analyze.cost_decision(rows((10.,9.5,10.1,10.2)))[0], 'NO_SAVING')
        self.assertEqual(analyze.cost_decision({})[0], 'NO_SAVING')

if __name__ == '__main__':
    unittest.main()
