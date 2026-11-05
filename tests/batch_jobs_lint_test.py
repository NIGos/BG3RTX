"""Regression checks for the failed combined camp plan's producer and warmups."""
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow


class BatchJobsLintTests(unittest.TestCase):
    def test_opt_attr_jobs_are_static_bounded_and_ready_offline(self):
        from tools.live_batches import lint_snapshot
        from tools.live_batches.opt_r2.gi_attr.make_job import FEATURES
        snapshot = lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        for area in ('opt_r1/scene_holds', 'opt_r2/gi_attr'):
            path = next((ROOT/'tools/live_batches'/area/'jobs').glob('*.json'))
            job, plan, _ = flow.load_job(path)
            self.assertEqual(flow.lint(job, plan, [], snapshot['matches'])['problems'], [])
            self.assertLessEqual(job['timeout'], 600 if area.endswith('gi_attr') else 300)
            if area.endswith('gi_attr'):
                self.assertTrue(all(p['kind'] == 'static' and not p.get('trace_at') for p in plan))
                self.assertEqual(len(job['expect_every']), 18)
                from tools.live_batches import pipeline_readiness
                warm = [p for p in plan if p['label'].startswith('warm-')]
                self.assertEqual(len(warm), 2*len(FEATURES))
                self.assertEqual(sum(p['wait_log_timeout'] for p in warm), 240)
                first_cost = next(i for i,p in enumerate(plan) if p.get('stats_reset'))
                self.assertTrue(all(plan.index(p)<first_cost for p in warm))
                for p in warm:
                    self.assertIn(pipeline_readiness.GI_TRACE, p['wait_log'])
                    self.assertIn(pipeline_readiness.QUIET, p['wait_log_fresh'])
                    self.assertGreaterEqual(p['wait_log_timeout'], 37 if p['label'].endswith('-off') else 3)
                states = dict((p['label'], s) for p, s in zip(plan, flow.phase_states(job, plan)))
                for name, key, off in FEATURES:
                    a, on, b = [states[name+'--'+label] for label in ('off-a', 'on', 'off-b')]
                    # stats_reset is observation policy; each rendering contrast
                    # changes only the intended feature, including its dependencies.
                    self.assertEqual({k for k in a if a[k] != on[k]}, {key})
                    self.assertEqual(a, b)
                    self.assertEqual(a[key], off)
            else:
                repeat = next(p for p in plan if p['label'] == 'warm-key-0')
                self.assertFalse(repeat.get('wait_log'))

    def test_readiness_snapshot_extension_is_strictly_additive(self):
        from tools.live_batches import lint_snapshot
        data = flow.read(lint_snapshot.PATH)
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp)/'snapshot.json'
            flow.write(target, data)
            self.assertEqual(lint_snapshot.extend_readiness([], target), 0)
            self.assertEqual(flow.read(target), data)
        self.addCleanup(flow.readiness_budget.configure, [])

    def test_quiet_release_logging_keeps_active_and_final_job_statistics(self):
        from scripts.queue_gates import phase_states
        paths = flow.active_job_files() | set(flow.job_files(ROOT / 'tools/live_batches/release_qual/final'))
        self.assertTrue(paths)
        for path in sorted(paths):
            job, plan, _ = flow.load_job(path)
            self.assertEqual(flow.settings(job.get("set", "")).get("detailed_logging"), "1", path.name)
            for phase, state in zip(plan, phase_states(job, plan)):
                with self.subTest(job=path.name, window=phase['label']):
                    # common/log_stats.h: each job retains its original producer cadences
                    # and every pattern/expect, including release qualification at dev=0.
                    self.assertTrue(any(int(state.get(k, 0)) != 0 for k in ('detailed_logging', 'dev', 'rt_stats', 'ls_stats')) or
                                    int(state.get('rt_stats_period_frames', 0)) > 0)

    def test_detailed_logging_does_not_change_pipeline_readiness_query(self):
        from tools.live_batches import readiness_budget
        self.addCleanup(readiness_budget.configure, [])
        readiness_budget.configure([])
        phase = {'wait_log': ['pipeline ready']}
        expected = readiness_budget.estimate({'save': 'fixture'}, phase, {'rr': '1'}, True)
        readiness_budget.FROZEN = True
        self.assertEqual(readiness_budget.estimate({'save': 'fixture'}, phase,
                         {'rr': '1', 'detailed_logging': '1'}, True), expected)

    def test_retained_combined_fixture_has_cross_job_pipeline_readiness(self):
        from tools.live_batches.combined_20261103.make_jobs import FIXTURES
        for name in FIXTURES:
            job, plan, _ = flow.load_job(
                ROOT / 'tools/live_batches/combined_20261103/jobs' / (name + '.json'))
            self.assertTrue(any(p['label'].endswith('settings-reset') for p in plan))
            self.assertEqual(flow.pipeline_readiness.problems(job, plan), [])

    def test_rejected_scheduling_retired_without_deleting_fixtures(self):
        from tools.live_batches.regenerate_active import BATCHES
        from tools.live_batches.combined_20261103.make_jobs import PARTITIONS
        self.assertFalse(PARTITIONS)
        self.assertFalse(set(BATCHES) & {'cov_paks_compact', 'gpu_opt1', 'gpu_opt2', 'water_offscreen', 'combined_20261103'})
        self.assertTrue((ROOT / 'tools/live_batches/combined_20261103/jobs').is_dir())

    def test_scene_wait_timeout_gates_exclude_captures_and_diagnostics(self):
        from tools.live_batches.regenerate_active import active_paths
        from scripts.queue_gates import line_matches, SCENE_WAIT_BAD, phase_states, scene_wait_eligible
        paths = list(active_paths()) + sorted((ROOT / 'tools/live_batches/tileset_qual/jobs').glob('*.json'))
        paths += [ROOT / 'tools/live_batches/gi_1ray_qual/jobs/gi1-ultra-1ray.json']
        for path in paths:
            job, plan, _ = flow.load_job(path)
            excluded = {p['label'] for p, state in zip(plan, phase_states(job, plan))
                        if not scene_wait_eligible(p, state)}
            for gate in job.get('expect_every', []):
                if 'scene wait:' in gate.get('select', '') and '0 timeouts' in gate.get('match', ''):
                    with self.subTest(job=path.name, gate=gate['name']):
                        self.assertTrue(gate.get('windows'))
                        self.assertFalse(excluded.intersection(gate['windows']))
                        self.assertFalse(line_matches(gate, SCENE_WAIT_BAD))
        gi = flow.read(paths[-1])
        gates = [g for g in gi['expect_every'] if 'scene wait:' in g.get('select', '')]
        self.assertEqual(len(gates), 2)
        for gate in gates:
            self.assertEqual(gate['windows'], ['cost-off-a', 'cost-on-a', 'cost-off-b', 'cost-on-b'])

    def test_active_combined_chunks_cover_each_source_once(self):
        from tools.live_batches.combined_20261103.make_jobs import PARTITIONS
        from tools.live_batches.regenerate_active import active_paths
        sources = []
        for path in active_paths():
            if path.parent.parent.name != 'combined_20261103':
                continue
            manifest = flow.read(path.parent.parent / (path.stem + '.manifest.json'))
            for segment in manifest['slices']:
                source = Path(segment['source'])
                source = source if source.is_absolute() else ROOT / source
                sources.append(source.relative_to(ROOT / 'tools/live_batches').as_posix())
                original, plan, _ = flow.load_job(source)
                self.assertEqual(segment['job']['expect_every'], original['expect_every'])
                self.assertEqual(segment['plan'], plan)
        self.assertCountEqual(sources, [source for group in PARTITIONS.values() for source in group])

    @patch.object(flow, 'corpus', side_effect=AssertionError('CTest must not read live corpus'))
    @patch.object(flow, 'default_results', side_effect=AssertionError('CTest must not find live results'))
    def test_active_inventory_passes_current_preflight(self, *_):
        from tools.live_batches.regenerate_active import active_paths
        paths = set(active_paths())
        self.assertEqual(paths, flow.active_job_files())
        self.assertTrue(paths)
        from tools.live_batches import lint_snapshot
        index = lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        self.addCleanup(flow.TRACE_EXPORT.clear)
        for path in sorted(paths):
            with self.subTest(job=path.name):
                job, plan, _ = flow.load_job(path)
                patterns = set(job.get('forbid', []) + job.get('expect', []))
                self.assertFalse(patterns - index['matches'].keys(), 'refresh lint snapshot for new regexes')
                result = flow.lint(job, plan, [], index['matches'])
                self.assertEqual(result['problems'], [])
                self.assertEqual(flow.launch_problems(job, result['duration']), [])
                self.assertLessEqual(result['duration']['seconds'] + result['duration']['expected_readiness_seconds'], 900)

    def test_compose_abort_share_preserves_invalid_and_rejects_excess(self):
        from scripts.queue_gates import line_matches
        from tools.live_batches.water_surface_fresnel.make_job import abort_bound
        job = flow.read(ROOT / 'tools/live_batches/water_surface_fresnel/jobs/wsf1-s8-interface-column.json')
        gate = next(g for g in job['expect_every'] if g['name'] == 'compose tiers')
        line = ('compose (120 frames, 119 composed): rays 234155/frame: '
                'screen hit (composed) 15.0%, hidden 6.5%, aborted 1, invalid 0')
        self.assertTrue(line_matches(gate, line))
        self.assertFalse(line_matches(gate, line.replace('aborted 1', 'aborted 1000')))
        self.assertFalse(line_matches(gate, line.replace('invalid 0', 'invalid 1')))
        self.assertFalse(line_matches(gate, line.replace('234155/frame', '0/frame')))
        self.assertFalse(line_matches(gate, line.replace('rays ', 'missing ')))
        bound = abort_bound([dict(path='valid.log', lines=[line, line.replace('invalid 0', 'invalid 1')])])
        self.assertEqual(bound['samples'], 1)
        self.assertEqual(bound['max'], 1 / (120 * (234155 - .5)))
        equal = dict(gate, share=dict(gate['share'], max=bound['max']))
        self.assertTrue(line_matches(equal, line))
        self.assertFalse(line_matches(equal, line.replace('aborted 1', 'aborted 2')))

    def test_frozen_readiness_rejects_unknown_queries(self):
        from tools.live_batches import lint_snapshot
        lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        self.addCleanup(flow.TRACE_EXPORT.clear)
        with self.assertRaisesRegex(ValueError, 'refresh lint snapshot'):
            flow.readiness_budget.estimate(dict(save='unknown'), dict(wait_log=['new producer']), {}, True)

    def test_frozen_snapshot_covers_jobs_outside_active_inventory(self):
        from tools.live_batches import lint_snapshot
        index = lint_snapshot.load()
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        self.addCleanup(flow.TRACE_EXPORT.clear)
        paths = set(flow.job_files(ROOT / 'tools/live_batches')) - flow.active_job_files()
        self.assertTrue(paths)
        for path in sorted(paths):
            with self.subTest(job=path.name):
                job, plan, _ = flow.load_job(path)
                patterns = set(job.get('forbid', []) + job.get('expect', []))
                self.assertFalse(patterns - index['matches'].keys())
                # merge-green checks changed jobs even when they are inactive.
                # Both sizing and generator timeout queries must be frozen.
                flow.duration_budget(job, plan)
                flow.readiness_budget.timeouts(job, plan)



if __name__ == '__main__':
    unittest.main()
