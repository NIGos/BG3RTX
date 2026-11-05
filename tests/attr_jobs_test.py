"""Offline attribution receipts, bracketing math, and prepared runner contracts."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, pipeline_readiness
from tools.live_batches.opt_r6.attr import analyze, make_job


class AttributionTests(unittest.TestCase):
    def receipts(self, missing=None):
        plan = [dict(label=label, frames=480, stats_reset=True) for label in analyze.LABELS]
        log, runner = '', ''
        for i, label in enumerate(analyze.LABELS):
            stamp = lambda s: f'10:{i:02d}:{s:06.3f}'
            runner += f'{stamp(0)} [pan-plan] window {i}\n'
            runner += f'{stamp(2.1)} [motion-ready] phase={i} stats_reset={i+1} frame=100\n'
            log += f'{stamp(.1)} [cfg] settings read\n'
            log += f'{stamp(1)} gi: trace pass (RT) GPU median 999.0 ms\n'
            log += f'{stamp(2)} [stats] reset token={i+1} frame=100\n'
            value = 10+i if not make_job.STATES[i][1] else 4+i
            for second in (6, 10, 14):
                for name, line in {
                    'GI': f'gi: GPU cost {value} ms/frame',
                    'GI trace': f'gi: trace pass (RT, 2 rays/sample, hit) GPU median {value} ms',
                    'origin': f'gi: origin pass GPU cost {value} ms/frame',
                    'sun trace': f'gi: sun trace pass GPU cost {value} ms/frame',
                    'reflections': f'rf: reflection pass GPU median {value} ms',
                    'local shadows': f'ls: local-light shadow pass GPU median {value} ms',
                    'RR': f'[ngx] gpu RR: execute = {value} ms',
                    'scene build': f'scene build GPU total median {value} ms',
                }.items():
                    if missing != (i, name):
                        log += stamp(second)+' '+line+'\n'
        runner += f'10:{len(analyze.LABELS)-1:02d}:20.000 [motion-plan] complete\n'
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p/'log').write_text(log)
            (p/'runner').write_text(runner)
            return analyze.costs(p/'log', p/'runner', plan, extra_costs=analyze.EXTRA_COSTS)

    def test_adjacent_baselines_and_reset_alignment(self):
        measured = self.receipts()
        result = analyze.analyze(measured)
        self.assertFalse(result['errors'])
        self.assertEqual(len(result['rows']), 2*8)
        for row in result['rows']:
            self.assertEqual(row['delta_ms'], -6)
            self.assertEqual(row['baseline_drift_ms'], 2)
        self.assertEqual(measured[analyze.LABELS[0]]['GI trace']['median_ms'], 10)
        # Positive cost and arbitrarily large drift remain diagnostic, never a failure.
        measured[analyze.LABELS[1]]['GI trace']['median_ms'] = 999
        self.assertFalse(analyze.analyze(measured)['errors'])

    def test_missing_nonfinite_and_short_evidence(self):
        for i in range(len(analyze.LABELS)):
            self.assertEqual(analyze.analyze(self.receipts((i, 'origin')))['status'], 'INVALID')
        measured = self.receipts()
        measured[analyze.LABELS[-1]]['RR']['median_ms'] = float('nan')
        self.assertEqual(analyze.analyze(measured)['status'], 'INVALID')
        measured = self.receipts()
        measured[analyze.LABELS[0]]['sun trace']['samples'] = 2
        self.assertEqual(analyze.analyze(measured)['status'], 'INVALID')

    def test_markdown_cli_exit_only_missing_evidence(self):
        for missing, expected in [(None, 0), ((4, 'RR'), 1)]:
            measured = self.receipts(missing)
            with tempfile.TemporaryDirectory() as directory, patch.object(analyze, 'costs', return_value=measured):
                output = Path(directory)/'out.md'
                self.assertEqual(analyze.main(['--job', str(make_job.HERE/'jobs/opt-attr-inn.json'),
                    '--log', 'unused', '--runner', 'unused', '--out', str(output)]), expected)
                self.assertIn('| Baseline mean | Delta | Drift |', output.read_text())

    def test_job_contracts(self):
        paths = sorted((make_job.HERE/'jobs').glob('*.json'))
        self.assertEqual(len(paths), 2)
        self.assertIn('hit_ao_rays', pipeline_readiness.NO_PIPELINE)
        for path in paths:
            job, plan, _ = flow.load_job(path)
            states = make_job.STATES
            labels = [label+'--static-a' for label, _ in states]
            measured = [p for p in plan if p.get('frames')]
            self.assertEqual([p['label'] for p in measured], labels)
            self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
            self.assertEqual(flow.settings(job['set'])['dev'], '1')
            self.assertEqual(job['repeat'], 1)
            self.assertFalse(job['session'])
            self.assertEqual(job['traces'], 0)
            self.assertEqual(min(job['timeout'], 60*job['max_minutes']), 300)
            budget = flow.duration_budget(job, plan)
            self.assertGreaterEqual(job['timeout'], budget['seconds']+budget['readiness_timeout_seconds'])
            self.assertEqual(job['max_minutes'], 5)
            self.assertLess(120+job['estimated_phase_seconds'], 300)
            self.assertTrue(job['post_gate'])
            self.assertTrue(job['expect'])
            self.assertEqual(len(job['expect_every']), 8)
            for gate in job['expect_every']:
                self.assertEqual(gate['windows'], labels)
                self.assertEqual((gate['min'], gate['period_frames'], gate['sample_frames']), (3, 120, 120))
            prewarm = plan[1:4]
            self.assertEqual([p['label'] for p in prewarm],
                ['warmup-sun-off', 'warmup-direct-off', 'warmup-baseline'])
            self.assertEqual(flow.settings(prewarm[-1]['set']), make_job.BASE)
            self.assertTrue(all(p['wait_log_timeout'] == 60 for p in prewarm))
            for p, (_, overrides) in zip(measured, states):
                self.assertEqual(flow.settings(p['set']), dict(make_job.BASE, **overrides))
                self.assertEqual(p['frames'], 480)
                self.assertTrue(p['stats_reset'])
                self.assertEqual(p['kind'], 'static')
            for p in plan:
                self.assertNotIn('rt_omm', flow.settings(p.get('set', '')) if p['label'] != 'warmup' else {})
                if p['label'].startswith(('ready-', 'warmup')) or p['label'] == 'restore':
                    self.assertIn(pipeline_readiness.QUIET, p['wait_log'])
                    self.assertEqual(p['wait_log_fresh'], [pipeline_readiness.QUIET])
            self.assertFalse(pipeline_readiness.problems(job, plan))
            self.assertFalse(flow.warmup_problems(job, plan))


if __name__ == '__main__':
    unittest.main()
