"""Synthetic oracle for reflection-specific consecutive-frame switching."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import unittest
import json
import tempfile
import numpy as np
from PIL import Image
from scripts.qa.pan_artifacts import coverage_mask
from scripts.qa.reflection_toggle import (debug25, derived_threshold, count_toggles,
                                         analyze_debug25, analyze_game_output, native_quantum, load_stream, load_coverage, main,
                                         export_reflection_mask, validate_manifest_frames)


class ReflectionToggleTests(unittest.TestCase):
    def test_code_decoder_excludes_rough_invalid_and_separates_delta_sign(self):
        rgb = np.array([[[[.2,0,0], [0,.3,0], [0,0,.3], [0,.25,.25], [1,1,0], [0,0,0]]]])
        s,t,v = debug25(rgb)
        np.testing.assert_allclose(s[0,0,:3],[-.2,.3,0])
        np.testing.assert_array_equal(t[0,0],[1,1,-1,0,0,1])
        np.testing.assert_array_equal(v[0,0],[1,1,1,0,0,1])

    def test_both_delta_signs_toggle_and_split_tier_transition(self):
        s = np.array([0,.4,0,-.5]).reshape(4,1,1)
        tiers = np.array([-1,1,-1,1]).reshape(4,1,1)
        r = count_toggles(s, tiers, np.ones_like(s,bool), .1, [3,4,5,6])
        self.assertEqual((r['toggles'],r['tier_1_to_x'],r['tier_x_to_1']), (3,1,2))
        self.assertEqual((r['on_to_off'],r['off_to_on']), (1,2))

    def test_threshold_derived_locally_not_polluted_by_noisy_neighbour(self):
        c = np.array([[[0,0]], [[10,.01]], [[0,0]]])
        bound, known = derived_threshold(c,np.ones_like(c,bool),.001)
        np.testing.assert_allclose(bound, [[10.002,.012]])
        self.assertTrue(known.all())

    def test_no_threshold_equality_event_and_no_disocclusion_event(self):
        s = np.array([0,.1,.4,0]).reshape(4,1,1)
        valid = np.array([1,1,0,1],bool).reshape(4,1,1)
        r=count_toggles(s,np.ones_like(s),valid,.1,[1,2,3,4])
        self.assertEqual(r['toggles'],0)
        self.assertEqual(r['valid_pixel_pairs'],1)

    def test_same_tier_delta_switches_are_reported(self):
        s=np.array([0,.4,0]).reshape(3,1,1)
        r=count_toggles(s,np.ones_like(s),np.ones_like(s,bool),.1,[0,1,2])
        self.assertEqual(r['same_tier'],2)
        self.assertEqual(r['tier_x_to_1'],0)

    def test_missing_frame_nonfinite_or_zero_precision_invalid(self):
        s=np.zeros((3,1,1));v=np.ones_like(s,bool)
        for ids,bound in [([1,2,4],.1),([1,2,3],0)]:
            with self.assertRaises(ValueError):count_toggles(s,s,v,bound,ids)
        s[0]=np.nan
        with self.assertRaises(ValueError):count_toggles(s,s,v,.1,[1,2,3])

    def test_calibration_missing_pixels_never_zero_pass(self):
        rgb=np.zeros((3,1,1,3));cv=np.zeros((3,1,1),bool)
        r=analyze_debug25(rgb,[1,2,3],rgb,[4,5,6],.001,np.ones_like(cv),cv)
        self.assertTrue(r['status'].startswith('INCOMPLETE'))
        self.assertIsNone(r['rate'])

    def test_uncovered_black_background_not_counted_as_tier1(self):
        rgb=np.zeros((3,1,2,3));rgb[1,0,:,1]=.5
        coverage=np.ones((3,1,2),bool);coverage[:,:,1]=False
        cal=np.zeros_like(rgb)
        r=analyze_debug25(rgb,[1,2,3],cal,[4,5,6],.001,coverage,coverage)
        self.assertEqual(r['toggles'],2)
        self.assertEqual(r['valid_pixel_pairs'],2)

    def test_native_quantum_increases_with_signal_exponent(self):
        rgb=np.array([[[[1,1,1],[4,4,4]]]],dtype='f4')
        np.testing.assert_allclose(native_quantum(rgb,122),[[1/32,1/8]])

    def test_game_control_cannot_claim_ssr_isolated(self):
        cal=np.ones((3,1,1,3));rgb=cal.copy();rgb[1]=2
        v=np.ones((3,1,1),bool)
        r=analyze_game_output(rgb,[1,2,3],cal,[4,5,6],.001,v,v)
        self.assertEqual(r['toggles'],2)
        self.assertFalse(r['supports_ssr_candidate'])
        self.assertIn('PROXY ONLY',r['attribution'])

    def test_manifest_cli_end_to_end_and_missed_record_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for name, active in [('turn',True),('cal',False)]:
                d=root/name;d.mkdir();samples=[]
                for frame in range(3):
                    rgb=np.zeros((1,2,4),dtype='<f2')
                    if active and frame==1:rgb[0,:,1]=.5
                    rgb.tofile(d/f'{frame}.raw')
                    samples.append(dict(name='water_compose_debug',record_frame=frame,
                                        width=2,height=1,format=97,file=f'{frame}.raw'))
                (d/'burst.json').write_text(json.dumps(dict(valid_transport=True,samples=samples)))
            masks=np.ones((3,1,2),bool)
            np.savez(root/'masks.npz',turn=masks,calibration=masks)
            self.assertEqual(main([str(root/'turn'),'--calibration',str(root/'cal'),
                                   '--coverage',str(root/'masks.npz'),'--out',str(root/'out.json')]),0)
            result=json.loads((root/'out.json').read_text())
            self.assertEqual(result['toggles'],4)
            meta=json.loads((root/'turn'/'burst.json').read_text());meta['samples'][1]['record_frame']=4
            (root/'turn'/'burst.json').write_text(json.dumps(meta))
            with self.assertRaisesRegex(ValueError,'consecutive'):load_stream(root/'turn','water_compose_debug')

    def test_coverage_matches_exact_record_frames_and_sizes(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);np.array([0,1,2,0],dtype='u1').tofile(d/'mask.raw')
            entries=[dict(name='water_coverage',record_frame=i,file='mask.raw',format=13,width=2,height=2) for i in [8,9]]
            (d/'burst.json').write_text(json.dumps(dict(valid_transport=True,samples=entries)))
            mask=load_coverage(d,[8,9],(2,2))
            self.assertEqual(int(mask.sum()),4)
            with self.assertRaisesRegex(ValueError,'exactly match'):load_coverage(d,[9,10],(2,2))
            with self.assertRaisesRegex(ValueError,'same-size'):load_coverage(d,[8,9],(1,4))

    def test_reflection_mask_union_excludes_unresolved_background_and_noncommon_records(self):
        rgb=np.zeros((3,1,5,3))
        rgb[:,:,:,2]=.3  # unresolved except known tier-1 states
        rgb[1,0,0]=[0,.5,0]  # union includes this support even when other frames unresolved
        rgb[:,0,2]=0  # background black resembles zero-delta composed: coverage must exclude
        rgb[:,0,3]=[0,.5,0]
        rgb[:,0,4]=[0,.25,.25]  # rough cyan never composed
        coverage=np.ones((3,1,5),bool);coverage[:,:,2]=False;coverage[0,0,3]=False
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'mask.png';info=export_reflection_mask(rgb,coverage,p)
            self.assertEqual(info['pixels'],1)
            decoded=coverage_mask(np.asarray(Image.open(p)),(1,5))
            np.testing.assert_array_equal(decoded,[[True,False,False,False,False]])

    def test_declared_burst_count_warmup_and_submission_are_strict(self):
        entries=[dict(record_frame=i,submit_frame=i+1) for i in range(11,19)]
        meta=dict(K=8,raw_record_frames=9,omitted_warmup_record_frame=10)
        self.assertEqual(validate_manifest_frames(meta,entries),list(range(11,19)))
        for truncated in [entries[:-1],entries[1:]]:
            with self.assertRaisesRegex(ValueError,'declared K'):validate_manifest_frames(meta,truncated)
        for field,value in [('raw_record_frames',8),('omitted_warmup_record_frame',9)]:
            wrong=dict(meta);wrong[field]=value
            with self.assertRaises(ValueError):validate_manifest_frames(wrong,entries)
        entries[0]['submit_frame']=2**64-1
        with self.assertRaisesRegex(ValueError,'valid submission'):validate_manifest_frames(meta,entries)


if __name__ == '__main__':unittest.main()
