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
    def test_compact_generator_confines_stats_and_warms_both_values(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(compact, 'HERE', Path(folder)):
            (Path(folder) / 'plans').mkdir()
            (Path(folder) / 'jobs').mkdir()
            job, plan = compact.make()
        self.assertEqual(plan, flow.read(ROOT / 'tools/live_batches/cov_paks_compact/plans/compact_camp.json'))
        self.assertEqual(job, flow.read(ROOT / 'tools/live_batches/cov_paks_compact/jobs/cp4-compact-camp-join.json'))
        warm = []
        for phase in plan:
            state = dict(part.split('=', 1) for part in phase['set'].split(','))
            self.assertEqual(state['rt_stats'], '1' if phase['label'].startswith('observe-') else '0')
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
