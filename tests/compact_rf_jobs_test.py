"""Compact-RF GPU batch and synthetic reset-aligned cost receipts; offline only."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, pipeline_readiness
from tools.live_batches.opt_r5.compact_rf import analyze, make_job


class CompactRfTests(unittest.TestCase):
    def energies(self, values=(1., 1., 1., 1.)):
        return {label: dict(mean_rgb=[v]*3, luminance=v, dimensions=[16,16])
                for (label, _), v in zip(make_job.STATES, values)}

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
            log += f'{stamp(1)} rf: reflection pass GPU median 999.0 ms\n'
            for second in (6, 10, 14):
                lines = {'GI': 'gi: GPU cost 20.0 ms/frame',
                         'GI trace': 'gi: trace pass main GPU median 10.0 ms',
                         'reflections': f'rf: reflection pass GPU median {value} ms',
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
        self.assertEqual(rows[analyze.LABELS[0]]['reflections']['samples'], 3)
        self.assertEqual(rows[analyze.LABELS[0]]['reflections']['median_ms'], values[0])
        return rows

    def test_every_on_must_beat_both_off_beyond_spread(self):
        for values, expected in [((10., 9.5, 10.125, 9.6), 'SAVING'),
                                 ((10., 9.875, 10.125, 9.875), 'NO_SAVING'),
                                 ((10., 9.5, 10.125, 10.2), 'NO_SAVING'),
                                 ((10., 10., 10., 10.), 'NO_SAVING')]:
            with self.subTest(values=values):
                result = analyze.analyze(self.receipt(values), self.energies())
                self.assertEqual(result['status'], expected)
                self.assertFalse(result['errors'])

    def test_missing_secondary_timer_invalidates(self):
        result = analyze.analyze(self.receipt((10., 9.5, 10.125, 9.6), missing=(3, 'RR')), self.energies())
        self.assertEqual(result['status'], 'INVALID')
        self.assertTrue(any('RR' in e for e in result['errors']))
        self.assertEqual(analyze.analyze({})['status'], 'INVALID')

    def test_cli_exit_only_for_missing_evidence(self):
        from unittest.mock import patch
        for values, missing, exit_code in [((10., 9.5, 10.125, 9.6), None, 0),
                                         ((10., 10., 10., 10.), None, 0),
                                         ((10., 9.5, 10.125, 9.6), (0, 'RR'), 1)]:
            rows = self.receipt(values, missing)
            with tempfile.TemporaryDirectory() as directory, patch.object(analyze, 'costs', return_value=rows), patch.object(analyze, 'read_energy', return_value=self.energies()):
                args = ['--job', str(next((make_job.HERE/'jobs').glob('*.json'))),
                        '--log', 'unused', '--runner', 'unused', '--out', str(Path(directory)/'result.json')]
                self.assertEqual(analyze.main(args), exit_code)

    def test_energy_change_cli_is_nonzero(self):
        from unittest.mock import patch
        rows = self.receipt((10.,9.,10.,9.))
        with tempfile.TemporaryDirectory() as directory, patch.object(analyze,'costs',return_value=rows), patch.object(analyze,'read_energy',return_value=self.energies((1.,.99,1.,1.))):
            self.assertEqual(analyze.main(['--job',str(next((make_job.HERE/'jobs').glob('*.json'))),
                '--log','unused','--runner','unused','--out',str(Path(directory)/'result.json')]),1)

    def test_jobs_and_readiness(self):
        jobs = list((make_job.HERE/'jobs').glob('*.json'))
        self.assertEqual(len(jobs), 2)
        self.assertIn(make_job.KEY, pipeline_readiness.TABLE)
        self.assertNotIn(make_job.KEY, pipeline_readiness.NO_PIPELINE)
        for path in jobs:
            job, plan, _ = flow.load_job(path)
            state = flow.settings(job['set'])
            self.assertEqual(state['dev'], '1')
            self.assertEqual(state['rf_hit_shade'], '0')
            self.assertEqual(state['rf_hit_gi'], '0')
            self.assertIs(job['keep_reshade'], False)
            self.assertEqual(len([p for p in plan if p.get('trace_at') == [1]]), 4)
            self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
            self.assertTrue(job['fail_fast'])
            self.assertEqual(job['max_minutes'], 5)
            self.assertGreaterEqual(job['hold'], 90)
            measured = [p for p in plan if p.get('frames')]
            self.assertEqual([p['label'] for p in measured], analyze.LABELS)
            self.assertEqual([flow.settings(p['set'])[make_job.KEY] for p in measured], ['0','1','0','1'])
            energy_indices = [i for i, p in enumerate(plan) if p.get('trace_at') == [1]]
            self.assertGreater(min(energy_indices), max(i for i, p in enumerate(plan) if p.get('frames')))
            for p in measured:
                self.assertEqual(p['frames'], 600)
                self.assertTrue(p['stats_reset'])
                self.assertEqual(p['kind'], 'static')
            self.assertEqual(pipeline_readiness.problems(job, plan), [])
            for p in plan:
                if p['label'].startswith('ready-'):
                    self.assertEqual(p['wait_log'], [pipeline_readiness.QUIET])
                    self.assertEqual(p['wait_log_fresh'], p['wait_log'])
            self.assertEqual(len(job['expect_every']), 10)
            for gate in job['expect_every']:
                if gate.get('event'):
                    continue
                self.assertEqual(gate['windows'], analyze.LABELS)
                self.assertEqual(gate['period_frames'], 120)


    def test_preceding_off_pairs_expose_drift(self):
        result = analyze.analyze(self.receipt((10.,9.,8.,7.)), self.energies())
        rf = result['gpu']['reflections']
        self.assertEqual(list(rf['preceding_off_delta_ms'].values()), [-1.,-1.])
        self.assertEqual(len(rf['window_ms']), 4)
        self.assertEqual(list(rf['pair_verdict'].values()), ['NO_SAVING','NO_SAVING'])
        self.assertEqual(result['status'], 'NO_SAVING')

    def test_energy_change_and_missing_evidence_fail_closed(self):
        rows = self.receipt((10.,9.,10.,9.))
        self.assertEqual(analyze.analyze(rows)['status'], 'INVALID')
        for values in [(1.,.99,1.,1.), (1.,1.01,1.,1.), (1.,1.,1.,float('nan'))]:
            result = analyze.analyze(rows, self.energies(values))
            self.assertIn(result['status'], ('ENERGY_CHANGE','INVALID'))
        energies = self.energies()
        energies['on-b']['dimensions'] = [8,8]
        self.assertEqual(analyze.analyze(rows, energies)['status'], 'INVALID')

    def test_float_reader_and_missing_or_duplicate_export(self):
        from unittest.mock import patch
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cap = root/'capture'; cap.mkdir()
            (cap/'frame.json').write_text(json.dumps(dict(submits=[dict(cmds=[dict(events=[
                dict(op='pass',tag='rrpost'), dict(op='end',dumps=['scene'])])])])) )
            (cap/'dumps.json').write_text(json.dumps(dict(scene=dict(slot=2, nonfinite=0,mean=[1,1,1,1],w=16,h=16))))
            log = root/'game'; runner = root/'runner'; runner.write_text('unused')
            windows = [dict(label='energy-'+label, ready=True, applied=i*10,end=i*10+10)
                       for i,(label,_) in enumerate(make_job.STATES)]
            lines = [(i*10+1,f'[trace] frame 1 written: x -> {cap}') for i in range(4)]
            with patch.object(analyze.gates,'windows',return_value=windows), patch.object(analyze.gates,'stamped_lines',return_value=lines):
                log.write_text('unused')
                self.assertEqual(len(analyze.read_energy(log,runner,[])),4)
                lines.append(lines[0])
                with self.assertRaisesRegex(ValueError,'one float'):
                    analyze.read_energy(log,runner,[])
                lines[:] = lines[1:4]
                with self.assertRaisesRegex(ValueError,'one float'):
                    analyze.read_energy(log,runner,[])


if __name__ == '__main__':
    unittest.main()
