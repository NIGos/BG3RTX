import unittest
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
from scripts.qa.rebase_gate import analyze


class GateTest(unittest.TestCase):
    def fixture(self, missing=False, bad=False, unreproduced=False):
        plan=[]; runner=[]; log=[]
        for i,(mode,on) in enumerate((m,o) for m in (0,100,33) for o in (0,1)):
            step={'seconds':90,'motion_start':5,'motion_end':85,'repeat_ms':mode,'set':f'rt_scene_rebase={on}'}
            plan.append(step)
            start=i*90
            def stamp(t): return f'00:{t//60:02}:{t%60:02}.000'
            runner.append(f'{stamp(start)} [pan-plan] window {i}')
            for j in range(10,81,5):
                t=stamp(start+j)
                match=99.55 if mode==0 or on or unreproduced else 30.0 if mode==33 else 89.0
                if bad and on and mode: match=98.0
                log.append(f'{t} rt stats (120 frames): primary-ray check match {match}%, miss 0.000%')
                log.append(f'{t} rt passes: GPU cost 1.000 ms/frame TLAS this frame 0, previous frame 120, none {120 if missing and on else 0}')
                log.append(f'{t} [watch] presents 100 (displayed 100)')
        return '\n'.join(log),'\n'.join(runner),plan

    def test_pass(self): self.assertEqual(analyze(*self.fixture())['gate'],'PASS')
    def test_missing_rt_never_passes(self): self.assertEqual(analyze(*self.fixture(missing=True))['gate'],'FAIL')
    def test_substatic_improvement_fails(self): self.assertEqual(analyze(*self.fixture(bad=True))['gate'],'FAIL')
    def test_no_positive_control_fails(self): self.assertEqual(analyze(*self.fixture(unreproduced=True))['gate'],'FAIL')

    def rotation_fixture(self, no_motion=False, same_direction=False, missing_pose=False, early_exit=False):
        plan=[{'kind':'static','speed':'static','direction':0,'pixels_per_second':0,'repeat_ms':0,
               'motion_start':5,'motion_end':85,'seconds':90,'set':f'rt_scene_rebase={on}'} for on in (0,1)]
        plan += [{'kind':'rotate','speed':label,'direction':direction,'pixels_per_second':speed,'repeat_ms':0,
                  'motion_start':5,'motion_end':85,'seconds':90,'set':f'rt_scene_rebase={on}'}
                 for label,speed in [('slow',60),('fast',180)] for direction in (-1,1) for on in (0,1)]
        runner=[]; log=[]
        def stamp(t): return f'{t//3600:02}:{t//60%60:02}:{t%60:02}.000'
        for i,step in enumerate(plan):
            runner.append(f'{stamp(i*90)} [pan-plan] window {i}')
            on=step['set'].endswith('1'); moving=step['kind']=='rotate'
            for j in range(10,81,5):
                t=stamp(i*90+j); match=99.2 if on or not moving else 70
                mean=step['pixels_per_second']/300 if moving and not no_motion else 0
                yaw=mean*(1 if same_direction else step['direction'])
                log.append(f'{t} rt stats (120 frames): primary-ray check match {match}%, miss 0.000%')
                log.append(f'{t} rt passes: GPU cost 15.000 ms/frame TLAS this frame 0, previous frame 300, none 0')
                log.append(f'{t} [watch] presents 100 (displayed 100)')
                log.append(f'{t} rt epoch (120 frames): GPU camera frames 120, scene snapshots {0 if missing_pose else 120}; camera motion mean {mean:.6f} deg/frame, max {mean:.6f} deg/frame, signed yaw {yaw:.6f} deg/frame; scene rotation max 0.100000 deg, translation max 0.100000 m; pool hit match {match}% of 1000, soup hit match {match}% of 100; misses remain in the global primary check')
        if not early_exit: runner.append(f'[motion-plan] complete phases={len(plan)}')
        return '\n'.join(log),'\n'.join(runner),plan

    def test_rotation_passes_at_static_99_2(self): self.assertEqual(analyze(*self.rotation_fixture())['gate'],'PASS')
    def test_input_without_gpu_rotation_fails(self): self.assertEqual(analyze(*self.rotation_fixture(no_motion=True))['gate'],'FAIL')
    def test_same_measured_direction_fails(self): self.assertEqual(analyze(*self.rotation_fixture(same_direction=True))['gate'],'FAIL')
    def test_rotation_missing_scene_pose_fails(self): self.assertEqual(analyze(*self.rotation_fixture(missing_pose=True))['gate'],'FAIL')
    def test_rotation_exit_before_completion_fails(self): self.assertEqual(analyze(*self.rotation_fixture(early_exit=True))['gate'],'FAIL')


if __name__=='__main__': unittest.main()
