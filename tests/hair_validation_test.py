"""Synthetic regression gates; no Vulkan, game, saves or Git writes."""
import json
import re
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.qa import hair
from tools.live_batches.hair_validation import analyze as suite
from tools.live_batches.hair_validation.make_job import make, job
from tools.live_batches.combine import combine


class HairValidation(unittest.TestCase):
    def setUp(self):
        self.c = np.zeros((48, 48), bool)
        self.c[8:40, 8:40] = True
        self.roi = np.ones_like(self.c)
        self.depth = np.ones(self.c.shape)
        self.rep = dict(coverage_loss=[0,0,0], iou_loss=[0,0,0])

    def visual(self, s=None, v=None, depth=None):
        return hair.visual(self.c, self.c if s is None else s,
                           np.zeros_like(self.c) if v is None else v, self.roi,
                           self.depth if depth is None else depth, self.depth*2,
                           1e-6, np.zeros_like(self.depth), self.rep)

    def test_same_hair_passes(self):
        self.assertTrue(all(r['status']=='PASS' for r in self.visual()))

    def test_bald_and_large_gap_fail(self):
        s = self.c.copy(); s[8:36] = False
        for mask in (np.zeros_like(s), s):
            self.assertEqual(self.visual(mask)[0]['status'], 'FAIL')

    def test_silhouette_shift_fails(self):
        self.assertEqual(self.visual(np.roll(self.c, 16, axis=1))[1]['status'], 'FAIL')

    def test_duplicate_cards_fail(self):
        self.assertEqual(self.visual(v=self.c)[-1]['status'], 'FAIL')

    def test_depth_nan_behind_and_coplanar(self):
        for value in (np.nan, 3., -1.):
            self.assertEqual(self.visual(depth=self.depth*value)[2]['status'], 'FAIL')
        r = self.visual(v=self.c, depth=self.depth*2)
        self.assertEqual(r[3]['status'], 'FAIL')

    def test_empty_baseline_invalid(self):
        self.c[:] = False
        with self.assertRaises(ValueError): self.visual()

    def test_repeat_bounds_not_tuned(self):
        _, lo = hair.envelope([0,0,0], 1/1024)
        _, hi = hair.envelope([-.01,0,.01], 1/1024)
        self.assertEqual(lo, 1/1024)
        self.assertGreater(hi, lo)
        with self.assertRaises(ValueError): hair.envelope([0,0], 0)

    def test_warp_sign_subpixel_and_borders(self):
        image = np.tile(np.arange(8.), (8,1))
        mv = np.zeros((8,8,2)); mv[...,0] = -.5
        out, valid = hair.warp(image,mv)
        self.assertEqual(out[3,3], 2.5)
        self.assertFalse(valid[3,0])
        mv[:] = np.nan
        self.assertFalse(hair.warp(image,mv)[1].any())

    def temporal(self, frames, masks, mv, fresh=None):
        return hair.temporal(frames, masks, mv, np.zeros_like(masks[1:]),
                             dict(reprojection=[0,0,0], trailing=[0,0,0], flicker=[0,0,0]),
                             1/1024, fresh_frames=frames if fresh is None else fresh,
                             registered_frames=frames)

    def test_temporal_static_and_flicker(self):
        frames = np.ones((4,48,48,3))*.5
        masks = np.repeat(self.c[None],4,axis=0)
        mv = np.zeros((3,48,48,2))
        self.assertTrue(all(r['status']=='PASS' for r in self.temporal(frames,masks,mv)))
        frames[1::2] = 1
        self.assertEqual(self.temporal(frames,masks,mv)[-1]['status'], 'FAIL')

    def test_ghost_detects_vacated_hair(self):
        masks = np.repeat(self.c[None],4,axis=0)
        masks[1:,8:16,:] = False
        fresh = np.repeat(masks[...,None],3,axis=-1).astype(float)
        frames = fresh.copy(); frames[1:,8:16,8:40] = .5
        result = self.temporal(frames,masks,np.zeros((3,48,48,2)),fresh)
        self.assertEqual(result[1]['status'],'FAIL')

    def test_energy_drop_and_missing_pt(self):
        r=hair.lighting(self.depth,self.depth*.5,self.c,[0,0,0],1e-6)
        self.assertEqual(r[0]['status'],'FAIL')
        self.assertEqual(r[1]['status'],'INVALID')

    def test_validation_attribution_and_missing_activation(self):
        r=suite.log_checks('', 'GPU-assisted validation enabled\nValidation Error VUID-test pipeline hair_primary 0x123', ['hair_primary'])
        self.assertEqual(next(c for c in r if c['name']=='gpuav_our_pipelines')['status'],'FAIL')
        self.assertEqual(suite.log_checks('')[-1]['status'],'INVALID')

    def test_crash_and_pool_regressions(self):
        text='VK_ERROR_DEVICE_LOST\n[fault] read invalid at 0x2343e2e000\n[crash] exception 0xc0000005\nmesh pool full\ninvalid/missing native state 2'
        self.assertTrue(all(r['status']=='FAIL' for r in suite.log_checks(text)[:4]))

    def test_missing_cohorts_are_invalid(self):
        checks,_=suite.performance([],1)
        self.assertEqual([c['status'] for c in checks],['INVALID']*3)

    def test_complete_manifest_reports_and_missing_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp); (p/'game.log').write_text('VK_ERROR_DEVICE_LOST'); (p/'runner.log').write_text('')
            r=suite.analyze(dict(run_id='test',game_log='game.log',runner_log='runner.log'),p)
            suite.write_report(r,p/'report')
            self.assertEqual(json.loads((p/'report/summary.json').read_text())['status'],'FAIL')
            self.assertTrue((p/'report/report.md').exists())

    def test_plans_and_combiner(self):
        for validation in (False,True):
            plan=make(validation); j=job('test','Test Dialogue1',plan,validation)
            self.assertEqual(len(plan),len({p['label'] for p in plan}))
            self.assertTrue(j['expect'] and j['patterns'])
            self.assertTrue(all(p.get('turn_mode')=='keys' for p in plan if p['kind']=='rotate'))
            self.assertEqual('-Validation' in j['args'],validation)
            healthy='[hair primary] frame 1: 1 grooms published; last 300 frames: 1 publishes, 1 prepass, 1 G-buffer and 1 velocity strand draws, 0 pipelines refused'
            self.assertFalse(any(re.search(pattern,healthy) for pattern in j['forbid']))
        with self.assertRaises(ValueError): combine([[{'label':'x'}],[{'label':'x'}]])

    def test_end_to_end_and_bald_failure_survives_missing_motion(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'game').write_text('[alpha hair] invalid/missing native state 0; retired bounds-invalid primitives 0;\n1 G-buffer and 1 velocity strand draws, 0 pipelines refused')
            (p/'runner').write_text('[motion-plan] complete phases=28\n[hair-reload] selected save reload complete')
            (p/'validation').write_text('GPU-assisted validation enabled')
            arrays=dict(character_ids=self.roi.astype(int),cards=self.c,strands=self.c,
                        visible_cards=np.zeros_like(self.c),strand_depth=self.depth,
                        scalp_depth=self.depth*2,footprint_m=np.zeros_like(self.depth),
                        cards_linear=self.depth,strands_linear=self.depth,pt=self.depth)
            for stream in ('rendered','presented'):
                frames=np.ones((4,48,48,3))*.5
                arrays.update({stream+'_frames':frames,stream+'_fresh':frames,
                               stream+'_registered':frames,stream+'_character_ids':self.roi.astype(int),
                               stream+'_masks':np.repeat(self.c[None],4,axis=0),
                               stream+'_mv':np.zeros((3,48,48,2)),
                               stream+'_disoccluded':np.zeros((3,48,48),bool)})
            np.savez(p/'arrays.npz',**arrays)
            repeats=dict(self.rep,energy_delta=[0,0,0],pt_error=[0,0,0])
            for stream in ('rendered','presented'):
                repeats[stream]=dict(reprojection=[0,0,0],trailing=[0,0,0],flicker=[0,0,0])
            manifest=dict(run_id='synth',game_log='game',runner_log='runner',validation_log='validation',
                          observed=dict(characters_enter_leave=True,lod_transitions=True,repeated_toggles=True,smoke_ultra=True),
                          required_windows=['on-a','on-b'],
                          windows=[dict(name=n,run_id='synth',registered=True,arrays='arrays.npz',
                                        depth_precision_m=1e-6,linear_quantum=1e-6,code_quantum=1/1024,repeats=repeats)
                                   for n in ('on-a','on-b')],
                          performance=[dict(characters=n,window='cost',exclusive=True,diagnostics=False,
                                            contention=False,frame_ms=[10.]*100,frame_budget_ms=1000/60,
                                            hair_gpu_ms=.5,off_gpu_ms=[.5,.5,.5],timer_quantum_ms=.001,
                                            vram_mb=100,device_budget_mb=200) for n in (1,5,10)])
            r=suite.analyze(manifest,p)
            self.assertEqual(r['status'],'PASS',r)
            arrays['strands']=np.zeros_like(self.c)
            del arrays['rendered_mv']
            np.savez(p/'arrays.npz',**arrays)
            r=suite.analyze(manifest,p)
            self.assertEqual(r['status'],'FAIL')
            self.assertTrue(any(c['name'].endswith('missing_hair') and c['status']=='FAIL' for c in r['checks']))


if __name__=='__main__': unittest.main()
