"""Control-flow fixture for gpu-opt1 water's 8x8 no-record group exit.
Numeric bit identity of target and consumed validity words; does not replace GPU replay.
"""
import random
import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))

class EmptyWaterTiles(unittest.TestCase):
    def test_zero_record_and_partial_dispatch_exact(self):
        rng = random.Random(1023)
        cases = [([0]*64, [True]*64)]
        # Each lane separately occupied plus mixed and dispatch padding; arbitrary
        # high words cannot count as validity (roughness shares this word).
        for lane in range(64):
            w=[rng.getrandbits(16)<<16 for _ in range(64)]
            w[lane] |= 1
            cases.append((w,[True]*64))
        for _ in range(1000):
            cases.append(([rng.getrandbits(32) if rng.random()<.15 else rng.getrandbits(16)<<16 for _ in range(64)],
                          [rng.random()<.8 for _ in range(64)]))
        for words,inside in cases:
            has=[i and bool(w & 65535) for i,w in zip(inside,words)]
            # Model arbitrary downstream exact computation. It must be called for
            # every populated group; empty groups cannot touch native colour.
            target=[rng.getrandbits(32) for _ in range(64)]
            def execute(skip):
                out=list(target); rec=list(words); stats=[1]+[0]*127
                if not (skip and not any(has)):
                    for i,h in enumerate(has):
                        if h:
                            rec[i]=0
                            out[i] ^= 0x1023
                            stats[1]+=1
                return struct.pack('<64I',*out),struct.pack('<64I',*rec),stats
            self.assertEqual(execute(False),execute(True))
    # test_hair_cards_warmed_before_measured_shots retired with the hair_qual cost2 plans (hair-cards-retire:
    # rt_hair_cheap / rt_hair_gi_mean / rt_hair_visibility were removed; hair_qual no longer generates jobs).

    def test_live_identity_accepts_exact_static_outputs(self):
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
        import numpy as np
        from tools.live_batches.gpu_opt1.analyze import evaluate,OFF,PAIRS
        labels=(*OFF,*(p[1] for p in PAIRS.values()))
        means={x:1.0 for x in labels}
        images={x:np.ones((2,2,3),dtype=np.float32) for x in labels}
        result=evaluate(means,images)
        self.assertTrue(all(r['energy_pass'] and r['image_pass'] and r['bit_identical_to_both_off'] for r in result.values()))

    def test_live_identity_detects_energy_and_local_pixel_changes(self):
        import numpy as np
        from tools.live_batches.gpu_opt1.analyze import evaluate,OFF,PAIRS
        labels=(*OFF,*(p[1] for p in PAIRS.values()))
        means={x:1.0 for x in labels}
        images={x:np.ones((2,2,3),dtype=np.float32) for x in labels}
        means['shot-compact']=1.01
        images['shot-water'][0,0,0]=1.01
        result=evaluate(means,images)
        self.assertFalse(result['compact']['energy_pass'])
        self.assertFalse(result['water']['image_pass'])
        self.assertTrue(result['alpha']['energy_pass'] and result['alpha']['image_pass'])

    def test_live_after_water_image_uses_png_but_scene_requires_float(self):
        import json
        import tempfile
        import numpy as np
        from PIL import Image
        from tools.live_batches.gpu_opt1.analyze import image_detail,SCENE_FS,WATER_AFTER_FS
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            Image.new('RGB',(2,2),(64,128,255)).save(folder/'detail.png')
            dumps={'d1':{'pass_fs':WATER_AFTER_FS,'slot':0,'fmt':'R11G11B10F','png':'detail.png'},
                   'd2':{'pass_fs':SCENE_FS,'slot':0,'fmt':'R11G11B10F','png':'scene.png'}}
            Image.new('RGB',(2,2),(64,128,255)).save(folder/'scene.png')
            (folder/'dumps.json').write_text(json.dumps(dumps),encoding='utf-8')
            pixels=image_detail(folder,WATER_AFTER_FS)
            np.testing.assert_array_equal(pixels[0,0],np.array([64,128,255])/255)
            self.assertEqual(pixels.shape,(2,2,3))
            with self.assertRaisesRegex(ValueError,'dump_preview_f32'):
                image_detail(folder,SCENE_FS)
            expected=np.ones((2,2,4),dtype=np.float32)*3
            expected.tofile(folder/'scene_p.f32')
            np.testing.assert_array_equal(image_detail(folder,SCENE_FS),expected[...,:3])

    def test_shader_group_uniform_exit_contract(self):
        s=(Path(__file__).resolve().parents[1]/'shaders/rt/water_compose.hlsl').read_text()
        self.assertIn('WaveActiveAnyTrue(has)',s)
        self.assertIn('if (gsWaterPresent == 0u) return;',s)
        self.assertLess(s.index('cStats.InterlockedAdd(0, 1u)'),s.index('if (gsWaterPresent == 0u) return;'))
        self.assertLess(s.index('if (gsWaterPresent == 0u) return;'),s.index('if (has) cRec.Store'))
if __name__=='__main__': unittest.main()
