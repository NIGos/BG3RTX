import unittest
import json
import tempfile
from pathlib import Path
import numpy as np
from PIL import Image
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
from scripts.qa.fg_static_effects import analyze,compare_debug,same_pose


class StaticEffectsTest(unittest.TestCase):
    def arrays(self):
        return np.full((3,64,64,3),.5),np.full((3,64,64,3),.4)
    def test_identical_effect_passes(self):
        debug,albedo=self.arrays()
        r,*_=compare_debug(debug,debug,albedo,albedo)
        self.assertTrue(r['pass'])
    def test_effect_missing_in_fg_on_is_flagged(self):
        debug,albedo=self.arrays()
        r,*_=compare_debug(debug,np.zeros_like(debug),albedo,albedo)
        self.assertFalse(r['pass']); self.assertGreater(r['flagged_pixels'],1000)
    def test_independent_noise_uses_measured_repeat_variance(self):
        debug,albedo=self.arrays(); rng=np.random.default_rng(71)
        a=debug+rng.normal(0,.02,debug.shape); b=debug+rng.normal(0,.02,debug.shape)
        r,*_=compare_debug(a,b,albedo,albedo)
        self.assertTrue(r['pass'])
    def test_no_stable_gbuffer_never_passes(self):
        debug,albedo=self.arrays()
        with self.assertRaises(ValueError): compare_debug(debug,debug,albedo,np.ones_like(albedo))
    def test_native_precision_and_actual_pose_change(self):
        a=[0,0,1,100,20,-300]
        b=list(a); b[3]=float(np.nextafter(np.float32(100),np.float32(np.inf)))
        self.assertTrue(same_pose([a,b]))
        b[3]+=.03
        self.assertFalse(same_pose([a,b]))
        self.assertFalse(same_pose([]))
    def test_captured_trace_pipeline_and_missing_effect_counter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); plan=[]; log=[]; runner=[]
            stamp=lambda t:f'{t//3600:02}:{t//60%60:02}:{t%60:02}.000'
            for view in ('normal','sun','local','GI','reflections','RR'):
                for fg in (2,0):
                    i=len(plan); start=i*60
                    plan.append(dict(kind='static',view=view,fg=fg,variant='baseline',seconds=60,
                        motion_start=5,motion_end=55,repeat_ms=0,set='rt_scene_rebase=0'))
                    runner.append(f'{stamp(start)} [pan-plan] window {i}')
                    for j in range(10,56,5):
                        t=stamp(start+j)
                        log += [f'{t} primary-ray check match 99.200%, miss 0.000%',
                            f'{t} rt passes: GPU cost 1.0 ms/frame TLAS this frame 0, previous frame 120, none 0',
                            f'{t} [watch] presents 100 (displayed {200 if fg==2 else 100})',
                            f'{t} DLSS-G frame 600: status 0x0, presented per app frame {2 if fg==2 else 1}, displayed/rendered over the last 60 frames 2.00',
                            f'{t} rt epoch (120 frames): GPU camera frames 120, scene snapshots 120; camera motion mean 0.000000 deg/frame, max 0.000000 deg/frame, signed yaw 0.000000 deg/frame; scene rotation max 0.000000 deg, translation max 0.000000 m; pool hit match 99.2% of 1000, soup hit match 99.2% of 100; lighting forward [0, 0, 1], origin [100, 20, -300]',
                            f'{t} rt stats (120 frames): material classes: non-standard 0%',
                            f'{t} ls stats (120 frames): 1.00M px/frame; lights per pixel: listed 1',
                            f'{t} rt stats (120 frames): hybrid GI 1.00M rays/frame',
                            f'{t} rf stats (120 frames): 1.00M px/frame',
                            f'{t} [ngx] gpu RR: guides 0.001 ms + evaluate 2.000 ms']
                    if view=='normal': continue
                    for repeat,j in enumerate((20,30,40)):
                        path=root/f'capture-{i}-{repeat}'; path.mkdir()
                        Image.fromarray(np.full((64,64,3),128,np.uint8)).save(path/'image.png')
                        dumps={}
                        for n,(fs,fmt,slot) in enumerate([('aa4efa4d','RG16F',0),('aa4efa4d','RGBA8',1),
                            ('aa4efa4d','RGBA8',2),('aa4efa4d','RGBA8',3),('d1669194','R11G11B10F',0),('d25b8086','RGB10A2',0)]):
                            dumps[f'd{n}']=dict(pass_fs=fs,fmt=fmt,slot=slot,min=[0,0,0,0],max=[1,1,1,1],
                                png='image.png',w=64,h=64,nonfinite=0)
                        (path/'dumps.json').write_text(json.dumps(dumps)); (path/'done.txt').write_text('ok')
                        log.append(f'{stamp(start+j)} [trace] frame 10 written: 1 submits, 6 images -> {path}')
            runner.append(f'[motion-plan] complete phases={len(plan)}')
            r=analyze('\n'.join(log),'\n'.join(runner),plan,root/'sheets')
            self.assertEqual(r['gate'],'PASS',r['reasons'])
            self.assertEqual(len(list((root/'sheets').glob('*.png'))),5)
            without_rf='\n'.join(line for line in log if 'rf stats' not in line)
            r=analyze(without_rf,'\n'.join(runner),plan,root/'missing-sheets')
            self.assertEqual(r['gate'],'FAIL')
            self.assertTrue(any('reflections not proved' in reason for reason in r['reasons']))


if __name__=='__main__': unittest.main()
