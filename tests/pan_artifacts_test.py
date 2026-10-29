"""Synthetic temporal oracle: flash, persistent shadow, disocclusion, reprojection."""
import unittest
import json
import tempfile
from pathlib import Path
import numpy as np
from scripts.qa.pan_artifacts import (transient, reproject, camera, decode, load_burst,
                                     scene_camera_origin, texture_compatible, static_camera,
                                     calibration_noise, storage_quantum, positive_control_status)


class PanTests(unittest.TestCase):
    def test_mixed_command_recordings_are_invalid(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            np.ones((2, 3), dtype='<f4').tofile(root/'image.raw')
            samples = [dict(name=name, file='image.raw', record_frame=9,
                            command_recording=rec, submit_frame=10,
                            width=3, height=2, format=100)
                       for name, rec in [('depth', 100), ('final', 101)]]
            (root/'burst.json').write_text(json.dumps(dict(valid_transport=True, samples=samples)))
            with self.assertRaisesRegex(ValueError, 'mixed command recordings'):
                load_burst(root)

    def test_positive_control_needs_both_symptoms_and_comparable_cadence(self):
        control = dict(role='static-evaluation', calibrated_pixel_frames=1000,
                       primary_match_fraction=[.995], effects=dict(luma=dict(pixel_frames=1)))
        pan = dict(role='panning', calibrated_pixel_frames=1000, burst_fps=20,
                   primary_match_fraction=[.4], effects=dict(luma=dict(pixel_frames=50)))
        self.assertTrue(positive_control_status([control, pan]).startswith('REPRODUCED'))
        pan['burst_fps'] = 5
        self.assertTrue(positive_control_status([control, pan]).startswith('INCOMPLETE'))
        pan['burst_fps'] = 20; pan['primary_match_fraction'] = [.999]
        self.assertEqual(positive_control_status([control, pan]), 'NOT_REPRODUCED')
        self.assertTrue(positive_control_status([pan]).startswith('INCOMPLETE'))

    def test_noisy_point_cannot_disable_detection_elsewhere(self):
        noise = np.ones((16, 5, 5), dtype='f4')
        noise[::2, 0, 0] = 0
        valid = np.ones_like(noise, dtype=bool)
        bound, known = calibration_noise(noise, valid, 1/255)
        test = np.ones_like(noise); test[6:8, 3, 3] = .1
        flags, count = transient(test, valid & known[None], bound)
        self.assertEqual(count, 1)
        self.assertEqual(int(flags[:, 3, 3].sum()), 2)
        valid[:, 2, 2] = False
        _, known = calibration_noise(noise, valid, 1/255)
        self.assertFalse(known[2, 2])

    def test_precision_uses_actual_capture_format(self):
        self.assertEqual(storage_quantum(64, 1), 1/1023)
        self.assertEqual(storage_quantum(37, 1), 1/255)
        self.assertEqual(storage_quantum(97, 4), 2**-8)

    def test_stationary_jitter_is_not_a_pan(self):
        c = (np.eye(3), np.ones(2), np.zeros(2), np.zeros(3))
        self.assertTrue(static_camera([c, c], [0, .85]))
        self.assertFalse(static_camera([c, (*c[:3], np.array([.001, 0, 0]))], [0, .1]))
        self.assertFalse(static_camera([c, c], [0, 3]))
        self.assertFalse(static_camera([c, c], [None, None]))

    def test_texture_ramp_sampling_and_material_change(self):
        y, x = np.indices((5, 9))
        a = np.repeat((x[..., None]/32).astype('f4'), 3, axis=-1)
        b = a + .5/32
        self.assertTrue(texture_compatible(a, b, y, x).all())
        self.assertFalse(texture_compatible(a, b+.5, y, x).any())

    def test_native_packed_footer_is_not_camera_colour(self):
        pose = np.zeros((1, 56, 1), dtype='f4')
        pose[0, 52:55, 0] = [12, 13, 14]
        pose.view('u4')[0, 55, 0] = 0xffffffff
        np.testing.assert_array_equal(scene_camera_origin(pose), [12, 13, 14])
        pose[0, 53, 0] = np.nan
        with self.assertRaisesRegex(ValueError, 'camera'):
            scene_camera_origin(pose)

    def test_scene_matrix_nonfinite_is_invalid(self):
        pose = np.zeros((1, 56, 1), dtype='f4')
        pose[0, 34, 0] = np.inf
        with self.assertRaisesRegex(ValueError, 'camera'):
            scene_camera_origin(pose)

    def test_missing_final_is_invalid_even_with_complete_frame_indices(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            np.ones((2, 3), dtype='<f4').tofile(root/'depth.raw')
            samples = [dict(name='depth', file='depth.raw', record_frame=i,
                            submit_frame=i, width=3, height=2, format=100)
                       for i in range(16)]
            (root/'burst.json').write_text(json.dumps(dict(valid_transport=True, samples=samples)))
            with self.assertRaisesRegex(ValueError, 'missing.*final'):
                load_burst(root)

    def test_packed_hdr_special_values_remain_nonfinite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'hdr.raw'
            np.array([31 << 6, (31 << 6) | 1], dtype='<u4').tofile(path)
            rgb = decode(path, dict(width=2, height=1, format=122))
            self.assertTrue(np.isinf(rgb[0, 0, 0]))
            self.assertTrue(np.isnan(rgb[0, 1, 0]))
            np.testing.assert_array_equal(rgb[0, :, 1:], 0)

    def test_short_flashes_and_persistent_shadow(self):
        for duration in (1,2,3):
            a = np.ones((16,3,4),dtype='f4')
            a[5:5+duration,1,1] = .1
            a[4:10,2,2] = .1
            mask, count = transient(a,np.ones_like(a,dtype=bool),.2)
            self.assertEqual(count,1)
            self.assertEqual(int(mask.sum()),duration)
            self.assertFalse(mask[:,2,2].any())

    def test_disocclusion_and_no_recovery_are_not_flash(self):
        a = np.ones((16,2,2),dtype='f4')
        a[5,0,0] = 0
        a[12:,1,1] = 0
        good = np.ones_like(a,dtype=bool); good[4,0,0] = False
        mask,count = transient(a,good,.1)
        self.assertEqual(count,0)
        self.assertFalse(mask.any())

    def test_camera_header_exact_bits(self):
        values = np.arange(48,dtype='<f4')/7
        raw = np.frombuffer(values.tobytes(),dtype='u1').reshape(48,4)
        p = np.zeros((2,50,4),dtype='f4'); p[0,:48] = raw
        _,scale,bias,origin = camera(p)
        np.testing.assert_array_equal(scale,values[12:14])
        np.testing.assert_array_equal(origin,values[16:19])

    def test_reprojection_translation(self):
        c = (np.eye(3),np.array([.5,.5]),np.zeros(2),np.zeros(3))
        d = np.ones((8,10))
        iy,ix,z,valid,motion = reproject(d,c,c)
        y,x = np.indices(d.shape)
        np.testing.assert_array_equal(ix,x)
        np.testing.assert_array_equal(iy,y)
        np.testing.assert_allclose(motion,0,atol=1e-12)
        moved = (*c[:3],np.array([0,.1,0]))
        _,ix,_,_,_ = reproject(d,c,moved)
        np.testing.assert_array_equal(ix[:,1:],x[:,1:]-1)


if __name__ == '__main__':
    unittest.main()
