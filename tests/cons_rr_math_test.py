"""CPU float32 twin of rr_post resolve's geometric-mean range fix (F04)."""
import math
from pathlib import Path
import random
import struct
import unittest


def f32(value):
    try:
        return struct.unpack("f", struct.pack("f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def mean(samples, fixed):
    product, logs, zero = 1.0, 0.0, False
    for sample in samples:
        product = f32(product * sample)
        logs = f32(logs + f32(math.log2(abs(sample)) if sample else 0.0))
        zero |= sample == 0.0
    reciprocal = f32(1.0 / len(samples))
    ordinary = f32(abs(product) ** reciprocal)
    if not fixed:
        return ordinary if math.isfinite(ordinary) else 0.0
    if zero:
        return 0.0
    if math.isfinite(product) and product != 0.0:
        return ordinary
    return f32(2.0 ** f32(logs / len(samples)))


class ResolveMeanTest(unittest.TestCase):
    def test_fifteen_bright_samples(self):
        # 4x4 footprint: majority bit-5 clear, the one minority last. The
        # original loop admits all 16 because acc1.w remains zero until last.
        stencil = [0] * 15 + [1]
        groups = [[], []]
        for index, bit5 in enumerate(stencil):
            i, j = divmod(index, 4)
            if (i != 3 and j != 3) or not groups[0] or not groups[1]:
                groups[bit5].append(1000.0)
        self.assertEqual([len(g) for g in groups], [15, 1])
        self.assertEqual(mean(groups[0], False), 0.0)
        for group in groups:
            self.assertAlmostEqual(mean(group, True), 1000.0, delta=0.002)

    def test_tiny_positive_samples(self):
        samples = [f32(1e-6)] * 15
        self.assertEqual(mean(samples, False), 0.0)
        self.assertAlmostEqual(mean(samples, True), 1e-6, delta=2e-12)

    def test_zero_after_overflow_stays_zero(self):
        self.assertEqual(mean([1000.0] * 14 + [0.0], True), 0.0)
        self.assertEqual(mean([0.0] + [1000.0] * 14, True), 0.0)

    def test_ordinary_path_exactly_unchanged(self):
        rng = random.Random(4)
        for count in range(1, 17):
            for _ in range(40):
                samples = [f32(rng.uniform(0.25, 4.0)) for _ in range(count)]
                self.assertEqual(mean(samples, False), mean(samples, True))
        self.assertEqual(mean([-2.0, 8.0], False), mean([-2.0, 8.0], True))

    def test_shader_uses_twin(self):
        source = (Path(__file__).resolve().parents[1] / "shaders/rr_post.hlsl").read_text()
        self.assertIn("log2(select(col == 0.0, 1.0.xxx, abs(col)))", source)
        self.assertIn("exp2(log_sum / product.w)", source)
        self.assertIn("resolve_mean(acc1, log1, zero1)", source)
        self.assertIn("resolve_mean(acc0, log0, zero0)", source)


if __name__ == "__main__":
    unittest.main()
