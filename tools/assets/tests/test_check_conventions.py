import math
import unittest

from tools.assets.check_conventions import F, box_ramps, inside_trigger, point_incident, stem


def trig(**kw):
    t = dict(pos=(0.0, 0.0, 0.0), scale=1.0, rot=(0.0, 0.0, 0.0, 1.0), extents=(2.0, 2.0, 2.0), height=3.0,
             radius=1.0, shape=2, points=[])
    t.update(kw)
    return t


def box(**kw):
    l = dict(box=(2.0, 2.0, 4.0), att_start=0.0, att_end=0.0, att_side=0.0, att_fn=0)
    l.update(kw)
    return l


class InsideTriggerTests(unittest.TestCase):
    def test_box_is_centred_full_size(self):
        self.assertTrue(inside_trigger(trig(), (0.99, -0.99, 0.5)))
        self.assertFalse(inside_trigger(trig(), (1.01, 0.0, 0.0)))

    def test_box_rotation_and_scale(self):
        s = math.sqrt(0.5)   # 90 degrees about y: local x is world -z
        t = trig(extents=(4.0, 2.0, 2.0), rot=(0.0, s, 0.0, s))
        self.assertTrue(inside_trigger(t, (0.0, 0.0, 1.9)))
        self.assertFalse(inside_trigger(t, (1.9, 0.0, 0.0)))
        self.assertTrue(inside_trigger(trig(scale=2.0), (1.9, 0.0, 0.0)))

    def test_prism_extrudes_upwards(self):
        t = trig(shape=3, points=[(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)])
        self.assertTrue(inside_trigger(t, (0.0, 2.9, 0.0)))
        self.assertFalse(inside_trigger(t, (0.0, -0.1, 0.0)))
        self.assertFalse(inside_trigger(t, (1.1, 1.0, 0.0)))

    def test_sphere(self):
        self.assertTrue(inside_trigger(trig(shape=1, radius=2.0), (1.0, 1.0, 1.0)))
        self.assertFalse(inside_trigger(trig(shape=1, radius=1.0), (1.0, 1.0, 0.0)))


class BoxRampTests(unittest.TestCase):
    def test_no_attenuation_peaks_at_one(self):
        self.assertEqual(box_ramps(box()), (1.0, False, False))

    def test_side_wider_than_half_width_never_reaches_one(self):
        peak, jump, side = box_ramps(box(att_side=2.0))   # W/2 = 1: edge ramp reaches 0.5 at the centre
        self.assertAlmostEqual(peak, 0.25)
        self.assertFalse(jump)
        self.assertTrue(side)

    def test_overlapping_z_ramps_jump(self):
        peak, jump, _ = box_ramps(box(att_start=3.0, att_end=3.0))
        self.assertEqual(peak, 1.0)   # just below z = start/D the start ramp is ~1
        self.assertTrue(jump)

    def test_start_longer_than_depth(self):
        peak, _, _ = box_ramps(box(att_start=8.0, att_fn=1))   # linear to 0.5 at the far face, squared
        self.assertAlmostEqual(peak, 0.25)

    def test_curve_index_past_three_is_dark(self):
        self.assertEqual(box_ramps(box(att_fn=4))[0], 0.0)


class PointTermTests(unittest.TestCase):
    def light(self, flags, r=4.0):
        return dict(pos=(0.0, 0.0, 0.0), radius=r, color=(1.0, 1.0, 1.0), intensity=1000.0, type=0, flags=flags)

    def test_flat_falloff_has_no_inverse_square(self):
        flat = self.light(F["flat_falloff"])
        a, b = point_incident(flat, (0.5, 0, 0), b13=1.0), point_incident(flat, (1.0, 0, 0), b13=1.0)
        self.assertAlmostEqual(a / b, (1 - (0.25 / 16) ** 2) / (1 - (1 / 16) ** 2), places=6)
        sq = self.light(0)
        self.assertAlmostEqual(point_incident(sq, (0.5, 0, 0), b13=1.0) / point_incident(sq, (1.0, 0, 0), b13=1.0),
                               4 * (1 - (0.25 / 16) ** 2) / (1 - (1 / 16) ** 2), places=5)

    def test_pre_expose_takes_b13_and_range_is_exclusive(self):
        pe = self.light(F["pre_expose"])
        self.assertAlmostEqual(point_incident(pe, (1, 0, 0), b13=0.5) / point_incident(self.light(0), (1, 0, 0), b13=0.5),
                               0.5)
        self.assertEqual(point_incident(pe, (4.0, 0, 0)), 0.0)

    def test_template_stem(self):
        self.assertEqual(stem("Light_Underdark_Fill_Lava_012"), "Light_Underdark_Fill_Lava")
        self.assertEqual(stem("Light_A"), "Light_A")


if __name__ == "__main__":
    unittest.main()
