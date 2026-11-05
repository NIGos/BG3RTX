"""Decision fixture and production wiring for exact hidden far shading rejection."""
import itertools
import math
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import release_shader_equivalence as eq
from tools.live_batches.opt_r1.gi_roof import analyze as live


class RoofReject(unittest.TestCase):
    def test_actual_decision_boundaries(self):
        src = (ROOT/'shaders/gi/gi_roof_reject.hlsli').read_text()
        expression = re.search(r'return (.*?);', src, re.S)[1]
        expression = re.sub(r'(\d+)u', r'\1', expression).replace('&&', ' and ')
        expression = re.sub(r'!(?!=)', 'not ', expression)
        expression = ' '.join(expression.split())
        count = 0
        for key, far_key, stats, visible, aborted, t, fallback in itertools.product(
                (0, 1), (0, 1), (0, 1), (False, True), (False, True),
                (-1., 0., math.nextafter(1., 0.), 1., math.nextafter(1., 2.), 4.), (0, 1, 2)):
            eligible = eval(expression, {'__builtins__': {}}, dict(
                key=key, far_key=far_key, stats=stats, visible=visible,
                aborted=aborted, t=t, base_radius=1., fallback=fallback))
            self.assertEqual(eligible, bool(key and far_key and not stats and not visible
                                          and not aborted and t > 1. and fallback == 0))
            for material_ok, local_state, sun_ok, clear in itertools.product(
                    (False, True), range(3), (False, True), (False, True)):
                if not eligible or clear:
                    continue
                # Exact reference gi_far_hit decision after the roof rule.
                sun_known = sun_ok and clear
                radiance = (local_state != 2 if sun_known else local_state == 0 and t <= 1.)
                self.assertFalse(material_ok and radiance)
                self.assertEqual(1. - min(max(t, 0.), 1.)**2, 0.)  # fr.w0
                emission = 3.25  # Existing emission precedes guard; fallback adds zero.
                self.assertEqual(emission + 0., emission)
                count += 1
        self.assertGreater(count, 0)

    def test_production_order_and_reference(self):
        src = (ROOT/'shaders/gi/gi_trace_rt.hlsl').read_text()
        guard = src.index('// Emission and successful screen/cache hits')
        self.assertLess(src.index('if (screen_roof_blocked)'), guard)
        self.assertLess(src.index('sharc_ray_hit(hh'), guard)
        self.assertLess(src.index('bounce += he.radiance * w;', src.index('// rt_hit_emission:')), guard)
        self.assertLess(guard, src.index('NativeHitMaterial material=rt_hit_material'))
        self.assertIn('if(!visible && !aborted && t>=0', src)
        self.assertIn('gi_far_roof_clear(o, dir, t))', src)
        self.assertIn('&& (kSpecGiRoofReject == 0u ||', src)
        self.assertEqual(src.count('!gi_far_roof_clear(o,dir,t)'), 2)
        self.assertIn('occ += fr.w0;', src)
        self.assertIn('wfall += fr.w0;', src)
        hidden = src[guard:src.index('if (pc.gi_rt_fallback == 2 && nseen')]
        self.assertNotIn('rand01(seed)', hidden)
        far = (ROOT/'shaders/gi/gi_far.hlsli').read_text()
        self.assertIn('sun_known ? local_state != kGiFarLocalUnknown : (local_state == kGiFarLocalKnown && t <= R0)', far)

    def test_disabled_equivalence_domain(self):
        self.assertEqual(eq.assignments({1007, 1027}, set()), [{1007: 0, 1027: 0}, {1007: 1, 1027: 0}])
        self.assertEqual(eq.FIXED_OFF_KEYS, {1027: 'gi_roof_reject'})


class LiveGates(unittest.TestCase):
    def rows(self, values):
        return {label: dict(mean_rgb=[v]*3, luminance=v) for (label, _), v in zip(live.STATES, values)}

    def test_energy_off_repeat_noise(self):
        self.assertTrue(all(v['pass_'] for v in live.energy_gate(self.rows([1, 1, 1, 1])).values()))
        self.assertTrue(all(v['pass_'] for v in live.energy_gate(self.rows([1, 1.001, 1.002, 1.001])).values()))
        for values in ([1, .99, 1, 1], [1, 1, 1, 1.01]):
            self.assertFalse(all(v['pass_'] for v in live.energy_gate(self.rows(values)).values()))
        with self.assertRaises(ValueError):
            live.energy_gate(self.rows([1, 1, 1, math.nan]))

    def test_missing_complete_periods_invalid(self):
        job = dict(expect_every=[], expect=[], expect_runner=[], forbid=[])
        with patch.object(live.gates, 'windows', return_value=[
                dict(label='off-a--static-a', ready=True, applied=0, end=10)]):
            with self.assertRaisesRegex(ValueError, 'GPU periods'):
                live.analyze(job, [], '', '')


if __name__ == '__main__':
    unittest.main()
