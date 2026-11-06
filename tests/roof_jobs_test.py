"""Roof rejection GPU batch and synthetic reset-aligned cost receipts; offline only."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, pipeline_readiness
from tools.live_batches.opt_r6.roof import analyze, make_job


class RoofTests(unittest.TestCase):
    def receipt(self, values, missing=None):
        plan = [dict(label=label, frames=600, stats_reset=True) for label in analyze.LABELS]
        runner, log = '', ''
        for i, value in enumerate(values):
            stamp = lambda s: f'10:{i:02d}:{s:06.3f}'
            runner += f'{stamp(0)} [pan-plan] window {i}\n'
            runner += f'{stamp(2.1)} [motion-ready] phase={i} stats_reset={i+1} frame=100\n'
            log += f'{stamp(.1)} [cfg] settings read\n'
            log += f'{stamp(2)} [stats] reset token={i+1} frame=100\n'
            # Pre-reset outlier must never enter a cost receipt.
            log += f'{stamp(1)} gi: trace pass main GPU median 999.0 ms\n'
            for second in (6, 10, 14):
                lines = {'GI': 'gi: GPU cost 20.0 ms/frame',
                         'GI trace': f'gi: trace pass main GPU median {value} ms',
                         'reflections': 'rf: reflection pass GPU median 2.0 ms',
                         'local shadows': 'ls: local-light shadow pass GPU median 1.0 ms',
                         'RR': '[ngx] gpu RR: execute = 2.5 ms',
                         'scene build': 'scene build GPU total median 2.0 ms'}
                for name, line in lines.items():
                    if missing != (i, name):
                        log += stamp(second)+' '+line+'\n'
        runner += '10:03:20.000 [motion-plan] complete\n'
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p/'log').write_text(log)
            (p/'runner').write_text(runner)
            rows = analyze.costs(p/'log', p/'runner', plan)
        self.assertEqual(rows[analyze.LABELS[0]]['GI trace']['samples'], 3)
        self.assertEqual(rows[analyze.LABELS[0]]['GI trace']['median_ms'], values[0])
        return rows

    def test_every_on_must_beat_both_off_beyond_spread(self):
        for values, expected in [((10., 9.5, 10.125, 9.6), 'SAVING'),
                                 ((10., 9.875, 10.125, 9.875), 'NO_SAVING'),
                                 ((10., 9.5, 10.125, 10.2), 'NO_SAVING'),
                                 ((10., 10., 10., 10.), 'NO_SAVING')]:
            with self.subTest(values=values):
                result = analyze.analyze(self.receipt(values))
                self.assertEqual(result['status'], expected)
                self.assertFalse(result['errors'])
                self.assertEqual(result['gpu']['GI trace']['paired_delta_ms'],
                    {'on-a--static-a': values[1]-values[0], 'on-b--static-a': values[3]-values[2]})

    def test_missing_secondary_timer_invalidates(self):
        result = analyze.analyze(self.receipt((10., 9.5, 10.125, 9.6), missing=(3, 'RR')))
        self.assertEqual(result['status'], 'INVALID')
        self.assertTrue(any('RR' in e for e in result['errors']))
        self.assertEqual(analyze.analyze({})['status'], 'INVALID')

    def test_cli_exit_only_for_missing_evidence(self):
        from unittest.mock import patch
        for values, missing, exit_code in [((10., 9.5, 10.125, 9.6), None, 0),
                                         ((10., 10., 10., 10.), None, 0),
                                         ((10., 9.5, 10.125, 9.6), (0, 'RR'), 1)]:
            rows = self.receipt(values, missing)
            with tempfile.TemporaryDirectory() as directory, patch.object(analyze, 'costs', return_value=rows):
                args = ['--job', str(next((make_job.HERE/'jobs').glob('*.json'))),
                        '--log', 'unused', '--runner', 'unused', '--out', str(Path(directory)/'result.json')]
                self.assertEqual(analyze.main(args), exit_code)

    def test_jobs_and_readiness(self):
        jobs = list((make_job.HERE/'jobs').glob('*.json'))
        self.assertEqual(len(jobs), 2)
        self.assertIn(make_job.KEY, pipeline_readiness.TABLE)
        self.assertNotIn(make_job.KEY, pipeline_readiness.NO_PIPELINE)
        self.assertEqual(pipeline_readiness.TABLE[make_job.KEY], [pipeline_readiness.GI_TRACE])
        self.assertEqual({flow.load_job(p)[0]['save'] for p in jobs},
                         {'The Blushing Mermaid - 96h 39m', 'Water Queens House'})
        for path in jobs:
            job, plan, _ = flow.load_job(path)
            self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
            self.assertTrue(job['fail_fast'])
            expected = flow.settings(flow.read(ROOT/'tools/live_batches/opt_r5/compact_hit/jobs/opt-compact-hit-inn.json')['set'])
            expected.update(rt_compact_hit_state='1', gi_far_roof_early='0')
            self.assertEqual(flow.settings(job['set']), expected)
            self.assertEqual(flow.settings(job['set'])['dev'], '1')
            self.assertEqual(flow.settings(job['set'])['rt_stats'], '0')
            self.assertEqual(flow.settings(job['set'])['rt_compact_hit_state'], '1')
            self.assertEqual(job['max_minutes'], 5)
            self.assertGreaterEqual(job['hold'], 90)
            measured = [p for p in plan if p.get('frames')]
            self.assertEqual([p['label'] for p in measured], analyze.LABELS)
            self.assertEqual([flow.settings(p['set'])[make_job.KEY] for p in measured], ['0','1','0','1'])
            for p in measured:
                self.assertEqual(p['frames'], 600)
                self.assertTrue(p['stats_reset'])
                self.assertEqual(p['kind'], 'static')
            self.assertEqual(pipeline_readiness.problems(job, plan), [])
            for p in plan:
                if p['label'].startswith('ready-'):
                    self.assertEqual(p['wait_log'], [pipeline_readiness.QUIET])
                    self.assertEqual(p['wait_log_fresh'], p['wait_log'])
            self.assertEqual(len(job['expect_every']), 6)
            for gate in job['expect_every']:
                self.assertEqual(gate['windows'], analyze.LABELS)
                self.assertEqual(gate['period_frames'], 120)


if __name__ == '__main__':
    unittest.main()
