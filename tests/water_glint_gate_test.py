"""Nontrivial same-run glint energy/residual gate refusal fixtures."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.water_glint_gate import energy_gate, residual_gate, burst_residual


class GlintGateTests(unittest.TestCase):
    def test_filter_mean_pass_and_dimming_rejected(self):
        off = [.999, 1, 1.001]
        self.assertTrue(energy_gate([off, off, off, off])['pass_energy'])
        self.assertFalse(energy_gate([off, [.8]*3, off, [.8]*3])['pass_energy'])

    def test_on_noise_cannot_inflate_off_bound(self):
        result = energy_gate([[1]*3, [0.1, 1, 100], [1]*3, [1]*3])
        self.assertEqual(result['bound'], 0)
        self.assertFalse(result['pass_energy'])

    def test_missing_nonfinite_or_zero_energy_invalid(self):
        for phases in ([[[1]*3]*3], [[1]*3, [1]*3, [1]*3, [float('nan')]*3],
                       [[1]*3, [1]*3, [1]*3, [0]*3]):
            with self.assertRaises(ValueError):
                energy_gate(phases)

    def test_every_on_direction_beats_off_repeat_spread(self):
        scores = {f'{d}{i}': v for d in 'EQ' for i, v in enumerate((1, .5, 1.1, .6))}
        self.assertTrue(residual_gate(scores)['pass_residual'])
        scores['Q3'] = 1
        self.assertFalse(residual_gate(scores)['pass_residual'])

    def test_missing_or_nonfinite_residual_invalid(self):
        with self.assertRaises(ValueError):
            residual_gate({})
        with self.assertRaises(ValueError):
            residual_gate({f'{d}{i}': float('nan') for d in 'EQ' for i in range(4)})

    def test_consecutive_raw_burst_and_exact_coverage_required(self):
        import json
        import tempfile
        import numpy as np
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            samples = []
            for i in range(8):
                np.ones((1, 2, 4), dtype='<f2').tofile(root/f'{i}.raw')
                np.ones((1, 2), dtype='u1').tofile(root/f'{i}.mask')
                samples += [dict(name='water_game', record_frame=i, file=f'{i}.raw',
                                 format=97, width=2, height=1),
                            dict(name='water_coverage', record_frame=i, file=f'{i}.mask',
                                 format=13, width=2, height=1)]
            meta = dict(K=8, valid_transport=True, samples=samples)
            (root/'burst.json').write_text(json.dumps(meta))
            self.assertEqual(burst_residual(root)['residual'], 0)
            samples[-1]['record_frame'] = 9
            (root/'burst.json').write_text(json.dumps(meta))
            with self.assertRaises(ValueError):
                burst_residual(root)


if __name__ == '__main__':
    unittest.main()
