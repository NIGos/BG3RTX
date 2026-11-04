"""Offline contract of the final release live, never a runtime quality pass."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
import io

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot
from tools.live_batches.combine import settings
from tools.live_batches.release_qual.analyze_final import energy

HERE = ROOT / 'tools/live_batches/release_qual'


class FinalReleaseTests(unittest.TestCase):
    def test_frozen_cli_never_reads_or_writes_raw_corpus(self):
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        with tempfile.TemporaryDirectory() as d, patch.object(flow, 'corpus', side_effect=AssertionError('raw corpus forbidden')):
            cache, report = Path(d)/'cache.json', Path(d)/'report.json'
            with redirect_stdout(io.StringIO()):
                result = flow.main([str(HERE), '--preflight', '--frozen', '--cache', str(cache), '--report', str(report)])
            self.assertEqual(result, 0)
            self.assertFalse(cache.exists())
            self.assertEqual(len(flow.read(report)['jobs']), 5)

    def test_two_launches_cover_three_release_profiles(self):
        paths = list((HERE/'final/jobs').glob('*.json'))
        self.assertEqual(len(paths), 2)
        self.assertEqual({flow.read(p)['save'] for p in paths}, {'Water Queens House', 'Test Dialogue1'})
        for p in paths:
            job, plan, _ = flow.load_job(p)
            self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
            self.assertEqual(job['timeout'], 300)
            self.assertEqual(job['max_minutes'], 5)
            self.assertTrue(job['patterns'] and job['expect'] and job['expect_every'])
            for phase in plan:
                if phase.get('wait_stats'):
                    self.assertTrue(phase['frames'] and phase['stats_reset'])
                if phase['label'].rsplit('--', 1)[-1] in ('static-a', 'pan-q', 'pan-e', 'rotate-r', 'rotate-l', 'cost-hair'):
                    self.assertFalse(phase.get('trace_at'))
                    self.assertEqual(settings(phase['set']).get('rt_stats', '0'), '0')
        high = flow.read(HERE/'jobs/rel2-smoke-high-s8.json')
        state = settings(high['set'])
        self.assertEqual(state['preset'], 'high')
        for key in ('rt_omm', 'rt_omm_dyn', 'rt_vt_alpha_masks', 'rt_alpha_fine_lod'):
            self.assertEqual(state[key], '1')
        self.assertEqual(state['dev'], '0')
        self.assertTrue(any(g['select'].startswith('omm check') for g in high['expect_every']))

    def test_all_receipts_pass_frozen_lint(self):
        index = lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        for p in flow.job_files(HERE):
            with self.subTest(job=p.name):
                job, plan, _ = flow.load_job(p)
                self.assertEqual(flow.lint(job, plan, [], index['matches'])['problems'], [])
                budget = flow.duration_budget(job, plan)
                self.assertEqual(flow.launch_problems(job, budget), [])
                self.assertLessEqual(120+budget['seconds']+budget['expected_readiness_seconds'], 300)

    def test_energy_joins_pre_post_colour_and_rejects_nonfinite(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            frame = dict(submits=[dict(cmds=[dict(events=[dict(op='pass', tag='rrpost'),
                dict(op='end', dumps=['bloom', 'scene'])])])])
            dumps = dict(bloom=dict(slot=6, mean=[999,999,999], nonfinite=0),
                         scene=dict(slot=2, mean=[1,2,3], nonfinite=0, w=4, h=2))
            flow.write(p/'frame.json', frame)
            flow.write(p/'dumps.json', dumps)
            self.assertAlmostEqual(energy(p)['luminance'], 1.8596)
            dumps['scene']['nonfinite'] = 1
            flow.write(p/'dumps.json', dumps)
            with self.assertRaises(ValueError):
                energy(p)


if __name__ == '__main__':
    unittest.main()
