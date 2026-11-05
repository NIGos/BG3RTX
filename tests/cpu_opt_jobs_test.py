"""CPU receipt normalization and inn job contracts, without a game/queue."""
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.live_batches.opt_r3.cpu import analyze, make_job
from tools.live_batches import live_flow as flow


class CpuJobsTests(unittest.TestCase):
    def test_rendered_denominator_and_transition_rejection(self):
        plan = [dict(label='off-a--static-a', frames=600, stats_reset=True)]
        runner = ('10:00:00.000 [pan-plan] window 0\n'
                  '10:00:02.100 [motion-ready] phase=0 stats_reset=7 frame=100\n'
                  '10:00:18.000 [motion-plan] complete')
        log = ('10:00:00.100 [cfg] settings read\n'
               '10:00:01.000 [watch] presents 1 (displayed 2) | submit CPU 999 ms | descriptor tracker CPU 999 ms | alpha descriptors 999 ms / 2 calls\n'
               '10:00:02.000 [stats] reset token=7 frame=100\n')
        for n, presents in enumerate((60, 120, 180)):
            stamp = f'10:00:{6+4*n:02d}.000 '
            log += stamp+f'[watch] presents {presents} (displayed {2*presents}) | submit CPU {presents/2} ms | descriptor tracker CPU {presents/4} ms | alpha descriptors 0 ms / 0 calls\n'
            log += stamp+f'[cpu] bvh_build phases, ms/build over 120: decide {999 if n == 0 else 1}, per-draw BLAS keys 2, TLAS instances 3\n'
            log += stamp+'[cpu] capture hooks over 120 frames, ms/frame (all threads): total 8.000; driver draw (XFB) 0.250\n'
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'log').write_text(log)
            (p/'run').write_text(runner)
            row = analyze.analyze(p/'log', p/'run', plan)['off-a--static-a']
        self.assertEqual(row['submit inclusive (driver included)']['median'], .5)
        self.assertEqual(row['descriptor tracker']['median'], .25)
        self.assertEqual(row['alpha clone driver updates']['median'], 0)
        self.assertEqual(row['bvh decide'], dict(median=1, min=1, max=1, n=2))
        self.assertEqual(row['capture hook bodies (driver draw excluded)']['median'], 7.75)

    def test_only_cpu_toggle_and_quiet_readiness(self):
        job, plan, _ = flow.load_job(make_job.HERE/'jobs/opt-cpu1-inn.json')
        self.assertEqual(job['window_mode'], 'visible_unfocused_fg')
        self.assertEqual([flow.settings(p['set'])['cpu_opt1'] for p in plan if p.get('frames')], ['0','1','0','1'])
        for p in plan:
            self.assertNotIn('trace_at', p)
            self.assertFalse(any('generation' in rx or 'built' in rx for rx in p.get('wait_log', [])))
            if p.get('frames'):
                self.assertEqual(p['frames'], 600)
                self.assertTrue(p['stats_reset'])
        self.assertEqual(job['set'], flow.read(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')['set'].replace('cpu_timing=1,', 'cpu_opt1=0,cpu_timing=1,'))


if __name__ == '__main__':
    unittest.main()
