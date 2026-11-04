"""Offline profiling contracts and parser fixtures; no game or queue access."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot
from tools.live_batches.opt_profile import make_jobs as batch, analyze
from tools.live_batches.release_qual.analyze_final import costs


class OptProfileTests(unittest.TestCase):
    def test_four_launches_have_both_release_profiles_and_clean_cost_windows(self):
        snapshot = lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        paths = flow.job_files(batch.HERE)
        self.assertEqual(len(paths), 4)
        self.assertEqual({flow.read(p)['save'] for p in paths}, {save for _, save in batch.SCENES})
        for path in paths:
            job, plan, _ = flow.load_job(path)
            self.assertEqual(job['timeout'], 300)
            self.assertEqual(job['max_minutes'], 5)
            self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
            self.assertEqual(flow.lint(job, plan, [], snapshot['matches'])['problems'], [])
            self.assertLessEqual(120+flow.duration_budget(job, plan)['seconds'], 300)
            judged = []
            for phase, state in zip(plan, flow.phase_states(job, plan)):
                self.assertEqual(state['detailed_logging'], '1')
                self.assertEqual(state['cpu_timing'], '1')
                self.assertEqual(state['rt_build'], '1')
                self.assertNotIn('trace_at', phase)
                for key in ('rt_stats', 'rt_primary_check', 'ls_stats', 'rf_stats', 'ls_check',
                            'dump_images', 'dump_raw', 'dump_preview_f32', 'dump_shaders', 'pan_capture'):
                    self.assertEqual(state[key], '0', key)
                if phase.get('frames'):
                    judged.append((state['preset'], phase['label'].split('--')[1]))
                    self.assertEqual(phase['frames'], 360)
                    self.assertTrue(phase['stats_reset'] and phase['wait_stats'])
                    self.assertEqual(state['rt_stats_period_frames'], '120')
                    if phase['kind'] == 'pan':
                        self.assertLess(phase['key_down_ms'], phase['key_period_ms'])
            self.assertEqual(judged, [('ultra', 'static-a'), ('ultra', 'pan-q'),
                                      ('high', 'static-a'), ('high', 'pan-q')])

    def test_parser_separates_gpu_costs_and_watch_totals_and_drops_fg_straddle(self):
        plan = [dict(label='ultra--static-a', stats_reset=True)]
        runner = ('10:00:00.000 [pan-plan] window 0\n'
                  '10:00:02.100 [motion-ready] phase=0 stats_reset=7 frame=100\n'
                  '10:00:18.000 [motion-plan] complete')
        log = ('10:00:00.100 [cfg] settings read\n'
               '10:00:02.000 [stats] reset token=7 frame=100\n'
               '10:00:01.000 gi: GPU cost 999.0 ms/frame\n')
        for n, fg in enumerate((99, 1, 3)):
            stamp = f'10:00:{6+4*n:02d}.000 '
            log += stamp+'gi: GPU cost 4.0 ms/frame (average of 120 frames)\n'
            log += stamp+f'[sl] gpu FG input copy: median {fg}.0 ms, p99 999.0 ms (120 samples)\n'
            log += stamp+'[watch] presents 120 (displayed 240) | pipelines 2 (40 ms in driver) | submits 500 | submit CPU 24.0 ms\n'
            log += stamp+'[em] per build: 5 lights; GPU: build median 0.5 ms (p99 3.0, 120 samples)\n'
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'log').write_text(log)
            (p/'runner').write_text(runner)
            row = costs(p/'log', p/'runner', plan, analyze.EXTRA, skip_first=('FG copy',))['ultra--static-a']
            self.assertEqual(row['GI']['median_ms'], 4)
            self.assertEqual(row['FG copy']['median_ms'], 2)
            self.assertEqual(row['FG copy']['samples'], 2)
            self.assertEqual(row['CPU submit']['median_ms'], 24)
            self.assertEqual(row['driver']['median_ms'], 40)
            self.assertEqual(row['emissive']['median_ms'], .5)
            (p/'runner').write_text(runner.replace(' stats_reset=7 frame=100', ''))
            with self.assertRaisesRegex(ValueError, 'unacknowledged'):
                costs(p/'log', p/'runner', plan)

    def test_absent_active_costs_are_invalid_disabled_costs_are_not_zero(self):
        job = dict(save='fixture', set='preset=high,rt_water_geometry=0,rt_emissive=0,rr_post_temporal=0')
        plan = [dict(label='high--static-a', frames=360, set='')]
        table, errors = analyze.analyze(job, plan, {})
        self.assertTrue(errors)
        self.assertIn('disabled by preset', table)
        self.assertIn('shared RR (no separate timer)', table)
        self.assertIn('ms/120-frame period', table)
        self.assertNotIn('| 0.000 |', table)


if __name__ == '__main__':
    unittest.main()
