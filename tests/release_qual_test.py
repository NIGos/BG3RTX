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
from tools.live_batches import pipeline_readiness

HERE = ROOT / 'tools/live_batches/release_qual'


class FinalReleaseTests(unittest.TestCase):
    def test_energy_windows_cover_current_serialized_capture_model(self):
        # The old frozen retime shortened 8 s to 3.8 s. These generated
        # receipts retain the newer runner model and must pass in a fresh process.
        with patch.dict(flow.TRACE_STALL, {}, clear=True), patch.dict(flow.TRACE_EXPORT, {}, clear=True):
            for path in flow.job_files(HERE):
                job, plan, _ = flow.load_job(path)
                schedules = flow.trace_schedules(job, plan)
                for phase in plan:
                    if phase['label'].endswith('energy-static'):
                        with self.subTest(job=path.name, window=phase['label']):
                            self.assertEqual(phase['trace_at'], [1])
                            self.assertGreater(phase['seconds'], schedules[phase['label']][-1])
                            # A frozen-model 3.8 s regression must remain refused.
                            self.assertGreater(schedules[phase['label']][-1], 3.8)

    def test_gates_without_a_producer_in_the_profile_are_dropped(self):
        from tools.live_batches.release_qual import make_jobs as release
        job = dict(set='rt_water_reflections=1,rt_water_compose=0,rt_cascade_casters=1', expect_every=[
            release.gate('water traced', r'\[water\] reflections \(\d+ frames, ', 'x', ['a', 'b']),
            release.gate('compose', r'\[water\] compose stability \(\d+ frames\)', 'x', ['a', 'b']),
            release.gate('cascade', r'\[cascade\] casters: cascade \d+', 'x', ['a', 'b']),
            release.gate('scene wait', r'\[rt\] scene wait: \d+ frames', 'x', ['a', 'b'])])
        plan = [dict(label='a'), dict(label='b', set='rt_cascade_casters=0')]
        self.assertEqual(release.drop_unproduced(job, plan), ['compose'])
        self.assertEqual({g['name']: g['windows'] for g in job['expect_every']},
                         {'water traced': ['a', 'b'], 'cascade': ['a'], 'scene wait': ['a', 'b']})

    def test_staged_final_gates_only_windows_that_produce_their_line(self):
        # 2026-11-21: the High half kept the Ultra water/cascade gates; its stats wait never ended.
        from tools.live_batches.release_qual import make_jobs as release
        path = ROOT / 'tools/live_batches_staged/release_final/final/jobs/final-s8-ultra-high.json'
        job, plan, _ = flow.load_job(path)
        states = {p['label']: s for p, s in zip(plan, flow.phase_states(job, plan))}
        for g in job['expect_every']:
            keys = next((k for prefix, k in release.PRODUCERS if g['select'].startswith(prefix)), ())
            for w in g.get('windows', []) if keys else []:
                with self.subTest(gate=g['name'], window=w):
                    self.assertTrue(all(states[w].get(k, '0') != '0' for k in keys))
        for phase in plan:
            for rx in phase.get('wait_stats', []):
                keys = next((k for prefix, k in release.PRODUCERS if rx.startswith(prefix)), ())
                with self.subTest(window=phase['label'], wait=rx):
                    self.assertTrue(all(states[phase['label']].get(k, '0') != '0' for k in keys))

    def test_omm_downgrades_are_info_and_hit_material_failures_still_fail(self):
        # claude/omm-ab: a downgraded slot traces the reference scene (a correct image); 052606 High failed on 9.
        path = ROOT / 'tools/live_batches_staged/release_final/final/jobs/final-s8-ultra-high.json'
        job, _, _ = flow.load_job(path)
        handout = [g for g in job['expect_every'] if g['select'].startswith(r'\[rt\] scene handout')]
        self.assertTrue(handout)
        self.assertFalse([g['name'] for g in handout if 'downgraded' in g['match']])
        hit = [g for g in handout if 'hit material queries at the commit' in g['match']]
        self.assertEqual(len(hit), 1)
        import re
        line = ('[rt] scene handout: 60 frames, reserved 59, handed out 59, in order 59, fallbacks 0 (not committed 0, '
                'build failed 0, order 0, dropped 0), pairing mismatches 0, committed first 0, unused 0, users refused 0; '
                'OMM accelerated commits 50, downgraded 9; hit material queries at the commit 0, failed 0 '
                '(fallbacks: query 0, camera 0)')
        self.assertRegex(line, hit[0]['match'])
        self.assertNotRegex(line.replace('failed 0', 'failed 1'), hit[0]['match'])
        self.assertNotRegex(line.replace('query 0', 'query 2'), hit[0]['match'])
        from tools.live_batches.release_qual.analyze_final import HANDOUT_OMM
        self.assertEqual(re.search(HANDOUT_OMM, line).groups(), ('50', '9'))

    def test_pipeline_rebuild_phases_get_more_than_the_fixed_10_s(self):
        # 2026-11-21 (final-s8-ultra-high 033318): ab_off 1 -> 0 rebuilt the GI sun raygen pipeline in 11.9-12.6 s
        # in the driver; the fixed 10 s of every non-first readiness wait failed b1-...--ab-on.
        job, plan, _ = flow.load_job(ROOT / 'tools/live_batches_staged/release_final/final/jobs/final-s8-ultra-high.json')
        limits = {p['label'].rsplit('--', 1)[-1]: p['wait_log_timeout'] for p in plan if p.get('wait_log')}
        self.assertGreater(limits['ab-on'], 12.6)
        self.assertGreater(limits['settings-reset'], 12.6)
        self.assertEqual(limits['ab-off'], 10)   # ab_off=1 records no RT pass: nothing to build
        self.assertEqual(plan[0]['wait_log_timeout'], 60)   # startup

    def test_preset_reset_uses_resolved_values_and_target_pipelines(self):
        job, plan, _ = flow.load_job(HERE/'final/jobs/final-s8-ultra-high.json')
        reset = next(p for p in plan if p['label'].endswith('settings-reset'))
        state = settings(reset['set'])
        self.assertEqual(state['rt_build'], '1')
        self.assertEqual(state['ls_box'], '0')
        self.assertFalse(any('rt_local_shadows_box' in rx or 'water_compose' in rx
                             for rx in reset['wait_log']))
        wanted = pipeline_readiness.requirements(job, plan)[plan.index(reset)]
        for key in ('ls_box', 'ls_k', 'rr_post_temporal', 'rt_water_compose', 'rt_water_reflections'):
            self.assertEqual(wanted[key], [pipeline_readiness.QUIET], key)
        self.assertIn(pipeline_readiness.QUIET, reset['wait_log'])
        self.assertFalse(any('generation ' in rx or 'pipeline built' in rx for rx in reset['wait_log']))

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
