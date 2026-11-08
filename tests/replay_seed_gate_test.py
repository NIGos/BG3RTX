"""tools/replay_seed_gate.py: equal draws pass; an energy bias, a local bias and a counter bias fail."""
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import replay_seed_gate as gate  # noqa: E402

W, H, N = 64, 32, 16


def write_run(out: Path, bias=0.0, local=0.0, counter_bias=0):
    rng = np.random.default_rng(7)
    base = rng.uniform(0.2, 0.8, (H * W, 4))
    index, means, counters = [], ['side,set,seed,output,c0,c1,c2,c3'], ['side,set,seed,counter,total']
    seed = 0
    for key in ('A0', 'A1', 'B0', 'B1'):
        draws = []
        for _ in range(N):
            seed += 1
            img = base + rng.normal(0.0, 0.05, base.shape)
            if key[0] == 'B':
                img += bias
                img[:8, 0] += local   # 8 of 2048 pixels
            img = img.astype(np.float16).astype(np.float64)
            draws.append(img)
            means.append(f'{key[0]},{key[1]},{seed},giRaw,' + ','.join(f'{v:.12g}' for v in img.mean(axis=0)))
            total = int(rng.poisson(10000)) + (counter_bias if key[0] == 'B' else 0)
            counters.append(f'{key[0]},{key[1]},{seed},9,{total}')
        d = np.stack(draws)
        d.mean(axis=0).astype(np.float32).tofile(out / f'seed_{key}_giRaw.mean.f32')
        d.var(axis=0).astype(np.float32).tofile(out / f'seed_{key}_giRaw.var.f32')
        index.append(f'{key}_giRaw {W} {H} 4 {N}')
    (out / 'seed_index.txt').write_text('\n'.join(index) + '\n')
    (out / 'seed_means.csv').write_text('\n'.join(means) + '\n')
    (out / 'seed_counters.csv').write_text('\n'.join(counters) + '\n')


class SeedGateTests(unittest.TestCase):
    def run_gate(self, **kw):
        with tempfile.TemporaryDirectory() as d:
            write_run(Path(d), **kw)
            return gate.evaluate(Path(d))

    def test_equal_draws_pass(self):
        r = self.run_gate()
        self.assertTrue(r['passed'], r)

    def test_energy_bias_fails(self):
        r = self.run_gate(bias=0.01)
        self.assertFalse(r['outputs']['giRaw']['channels'][0]['energy_ok'])

    def test_local_bias_fails_pixels_not_energy(self):
        r = self.run_gate(local=0.5)
        c = r['outputs']['giRaw']['channels'][0]
        self.assertFalse(c['pixels_ok'])
        self.assertTrue(r['outputs']['giRaw']['channels'][1]['pixels_ok'])

    def test_counter_bias_fails(self):
        r = self.run_gate(counter_bias=500)
        self.assertFalse(r['counters']['passed'])

    def test_encoding_step_tolerance(self):
        m = np.array([[1.0]])
        step = gate.encoding_step(m, True)
        self.assertEqual(float(step[0, 0]), float(np.spacing(np.float16(1.0))))
        # A one-step deterministic difference (zero variance) is not an exceedance.
        f = gate.exceed_fraction(m, np.zeros_like(m), m + step, np.zeros_like(m), 8, True)
        self.assertEqual(float(f[0]), 0.0)


if __name__ == '__main__':
    unittest.main()
