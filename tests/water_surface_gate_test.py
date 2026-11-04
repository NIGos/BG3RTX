"""The float gate rejects missing, pre-water, nonfinite and mismatched colour evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.water_surface_gate import post_colour


class FloatGate(unittest.TestCase):
    def test_evidence(self):
        colour = dict(pass_fs='0000000000000000', slot=0, w=32, h=16,
                      fmt='B10G11R11_UFLOAT_PACK32', mean=[1, 2, 3], nonfinite=0)
        scene = dict(colour, pass_fs='5c3fdabc00000000', mean=[100, 100, 100])
        with tempfile.TemporaryDirectory() as directory:
            cap = Path(directory)
            def evaluate(rows):
                (cap / 'dumps.json').write_text(json.dumps(rows))
                return post_colour(cap)
            self.assertAlmostEqual(evaluate(dict(d1=scene, d2=colour)), 1.8596)
            for changed in [dict(nonfinite=1), dict(mean=[float('nan'), 1, 2]),
                            dict(mean=[-1, 1, 2]), dict(w=64), dict(slot=1),
                            dict(pass_fs='5c3fdabc00000000')]:
                with self.subTest(changed=changed), self.assertRaises(ValueError):
                    evaluate(dict(d1=scene, d2=dict(colour, **changed)))
            missing = dict(colour)
            del missing['nonfinite']
            with self.assertRaises(ValueError):
                evaluate(dict(d1=scene, d2=missing))
            with self.assertRaises(ValueError):
                evaluate(dict(d1=colour))


if __name__ == '__main__':
    unittest.main()
