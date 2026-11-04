"""Regression checks for the failed combined camp plan's producer and warmups."""
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches.cov_paks_compact import make_jobs as compact
from tools.live_batches import live_flow as flow


class BatchJobsLintTests(unittest.TestCase):
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

    def test_compact_generator_confines_stats_and_warms_both_values(self):
        from functools import partial
        self.addCleanup(flow.readiness_budget.configure, [])
        self.addCleanup(flow.fps_windows.configure, [])
        self.addCleanup(flow.TRACE_EXPORT.clear)
        with tempfile.TemporaryDirectory() as folder, patch.object(compact, 'HERE', Path(folder)), \
                patch.object(flow, 'tighten_files', partial(flow.tighten_files, frozen=True)), \
                patch.object(flow, 'corpus', side_effect=AssertionError('CTest must not read live corpus')):
            (Path(folder) / 'plans').mkdir()
            (Path(folder) / 'jobs').mkdir()
            job, plan = compact.make()
        self.assertEqual(plan, flow.read(ROOT / 'tools/live_batches/cov_paks_compact/plans/compact_camp.json'))
        expected = flow.read(ROOT / 'tools/live_batches/cov_paks_compact/jobs/cp4-compact-camp-join.json')
        # The fixture retains derived counts/rates, not machine-specific log paths.
        for model in expected['live_flow'].get('fps_windows', []):
            model.pop('logs', None)
        self.assertEqual(job, expected)
        warm = []
        for phase in plan:
            state = dict(part.split('=', 1) for part in phase['set'].split(','))
            self.assertEqual(state['rt_stats'], '1' if phase['label'].startswith(('observe-', 'producer-sanity-')) else '0')
            if phase['label'].startswith('warm'):
                warm.append(state['rt_hit_material_paks'])
        self.assertEqual(set(warm), {'0', '1'})
        self.assertTrue(plan[0]['wait_log_timeout'])
        self.assertTrue(any('prefetched' in pattern for pattern in plan[0]['wait_log']))
        # None of these cold-start readiness requirements can be met by the
        # earlier "building in the background" or transient cache count line.
        premature = '[shaders] cache: 47 hits, 31 compiled\n' + '\n'.join(
            'gi: ' + name + ' building in the background' for name in
            ['gi/gi_trace_rt_hit_sun_em.hlsl?x', 'rt/water_reflect.hlsl?WATER_HIT_SHADE_LAYOUT=1'])
        self.assertFalse(any(re.search(pattern, premature) for pattern in plan[0]['wait_log']))

    def test_scaled_camp_warm_requires_distinct_ready_pipelines(self):
        plan = flow.read(ROOT / 'tools/live_batches/gpu_opt2/plans/camp.json')
        full = plan[0]['wait_log'][0]
        self.assertFalse(re.search(full, 'gi: gi/rt_reflections.hlsl?RT_TRACE_HALF=1 generation 1 spec cascade=0'))
        self.assertFalse(re.search(full, 'gi: gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1 generation 1 spec cascade=1'))
        self.assertTrue(re.search(full, 'gi: gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1 generation 1 spec cascade=0'))
        warm = next(phase for phase in plan if phase['label'] == 'warmup-scaled')
        previous = 'gi: gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1 generation 1, pipeline 10 ms'
        self.assertFalse(re.search(warm['wait_log'][0], previous))
        ready = 'gi: gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1;RT_TRACE_HALF=1 generation 1, pipeline 10 ms'
        self.assertTrue(re.search(warm['wait_log'][0], ready))
        self.assertTrue(any('rf_resolve' in pattern for pattern in warm['wait_log']))


if __name__ == '__main__':
    unittest.main()
