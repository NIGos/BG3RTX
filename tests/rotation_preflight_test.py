import unittest
import re
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
from scripts.qa.rotation_preflight import qualify


class PreflightTest(unittest.TestCase):
    def fixture(self):
        plan=[]; log=['[crash] exception diagnostics active independently of FG; minidump armed']; runner=[]
        for i in range(4):
            plan.append({'kind':'static','seconds':90,'repeat_ms':0,'motion_start':5,'motion_end':85,
                         'set':f'rt_scene_rebase={i%2},rt_primary_check=1'})
            def stamp(t): return f'00:{t//60:02}:{t%60:02}.000'
            runner.append(f'{stamp(i*90)} [pan-plan] window {i}')
            for j in range(10,81,5):
                t=stamp(i*90+j)
                log.extend([f'{t} primary-ray check match 99.200%, miss 0.000%',
                    f'{t} rt passes: GPU cost 15.000 ms/frame TLAS this frame 0, previous frame 300, none 0',
                    f'{t} [watch] presents 100 (displayed 100)',
                    f'{t} rt epoch (120 frames): GPU camera frames 120, scene snapshots 120; camera motion mean 0.000000 deg/frame, max 0.000000 deg/frame, signed yaw 0.000000 deg/frame; scene rotation max 0.000000 deg, translation max 0.000000 m; pool hit match 99.200% of 100, soup hit match 99.200% of 100; camera translation motion max 0.000000 m/frame, precision bound 0.000100 m/frame'])
        runner.append('[motion-plan] complete phases=4')
        return '\n'.join(log),'\n'.join(runner),plan,{'result':'done','plan_completed':True,
            'observed_exit_code':None,'foreground':True,'real_present':True}
    def test_complete_static_run_passes(self): self.assertEqual(qualify(*self.fixture())['gate'],'PASS')
    def test_premature_exit_fails(self):
        log,runner,plan,status=self.fixture(); status['observed_exit_code']=0
        self.assertEqual(qualify(log,runner,plan,status)['gate'],'FAIL')
    def test_exception_fails_even_after_recovery(self):
        log,runner,plan,status=self.fixture()
        self.assertEqual(qualify(log+'\n[crash] exception 0xc0000005',runner,plan,status)['gate'],'FAIL')
    def test_static_camera_drift_fails(self):
        log,runner,plan,status=self.fixture()
        self.assertEqual(qualify(log.replace('max 0.000000 deg/frame','max 0.100000 deg/frame'),runner,plan,status)['gate'],'FAIL')
    def test_empty_rt_and_gpu_windows_fail(self):
        log,runner,plan,status=self.fixture()
        inactive=re.sub(r'TLAS this frame \d+, previous frame \d+', 'TLAS this frame 0, previous frame 0', log)
        self.assertEqual(qualify(inactive,runner,plan,status)['gate'],'FAIL')
        empty=log.replace('GPU camera frames 120, scene snapshots 120','GPU camera frames 0, scene snapshots 0')
        self.assertEqual(qualify(empty,runner,plan,status)['gate'],'FAIL')
    def test_unfocused_requires_visibility_and_no_activation(self):
        log,runner,plan,status=self.fixture()
        status.update(foreground=False,visible_unfocused=True,unfocused_checks=20,
                      unfocused_visible_min_pixels=100,unfocused_violation=None)
        self.assertEqual(qualify(log,runner,plan,status)['gate'],'PASS')
        for change in ({'unfocused_visible_min_pixels':0},{'unfocused_checks':0},
                       {'unfocused_violation':'activated'},{'foreground':True}):
            self.assertEqual(qualify(log,runner,plan,{**status,**change})['gate'],'FAIL')


if __name__=='__main__': unittest.main()
