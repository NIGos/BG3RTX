import json
from pathlib import Path
import re
import runpy
import unittest

ROOT=Path(__file__).resolve().parents[1]


class FGPlanTest(unittest.TestCase):
    def setUp(self): self.generated=runpy.run_path(str(ROOT/'scripts/prepare-p1-fg-matrix.py'))
    def test_checked_in_plans_are_generated_and_paired(self):
        static,motion=self.generated['plans']()
        for name,plan in (('static',static),('motion',motion)):
            self.assertEqual(json.loads((ROOT/f'tests/fixtures/p1_fg_{name}_plan.json').read_text()),plan)
            self.assertEqual({s['fg'] for s in plan},{0,2})
        modes={(s['kind'],s['speed'],s['direction'],s['variant'],s['fg']) for s in motion}
        for kind,speed,direction,variant,fg in modes: self.assertIn((kind,speed,direction,variant,2-fg),modes)
        self.assertEqual({s['pixels_per_second'] for s in motion if s['kind']=='rotate'},{60,120,240})
    def test_fg_is_cold_loaded_on_and_reference_does_not_use_unfocused(self):
        jobs=[json.loads((ROOT/f'tests/fixtures/p1_fg_{name}_job.json').read_text()) for name in ('static','motion')]
        for job in jobs:
            self.assertIn('fg=2',job['set'].split(',')); self.assertNotIn('-Foreground',job['args']); self.assertIn('-RealPresent',job['args'])
            self.assertEqual(job['window_mode'],'visible_unfocused_fg')
            self.assertTrue(job['post_gate'])
            for key in ('patterns','expect','expect_runner','forbid'):
                for pattern in job[key]: re.compile(pattern)
        self.assertEqual(jobs[1]['requires_success_of'],[jobs[0]['name']])
        self.assertLess(sum(j['timeout'] for j in jobs),90*60)
    def test_variant_keys_and_static_debug_repeats(self):
        static,motion=self.generated['plans']()
        for s in motion:
            values=dict(v.split('=',1) for v in s['set'].split(','))
            self.assertEqual(int(values['fg']),s['fg'])
            self.assertEqual(int(values['rt_scene_rebase']),int(s['variant']=='rebase'))
            self.assertEqual(int(values['fg_current_destroy_only']),int(s['variant']=='current_destroy_only'))
        for view in ('sun','local','GI','reflections','RR'):
            views=[s for s in static if s['view']==view]
            self.assertEqual({s['fg'] for s in views},{0,2})
            self.assertTrue(all(len(s['trace_at'])==3 and s['kind']=='static' for s in views))


if __name__=='__main__': unittest.main()
