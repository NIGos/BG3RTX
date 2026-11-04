"""The float gate rejects missing, pre-water, nonfinite and mismatched colour evidence."""
import json
from array import array
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.water_surface_gate import post_colour, regional_colour


class FloatGate(unittest.TestCase):
    def test_paired_regional_float(self):
        with tempfile.TemporaryDirectory() as directory:
            cap = Path(directory)
            scene = dict(image='i1', slot=0, w=4, h=2, pass_fs='5c3fdabc00000000')
            before = dict(scene, pass_fs='c4e92ccdb08dd777', pass_ordinal=3, png='before.png')
            after = dict(before, png='after.png')
            dumps = dict(d1=scene, d2=before, d3=after)
            events = [dict(op='pass'), dict(op='draw', fs=before['pass_fs']),
                      dict(op='end', dumps=['d2']), dict(op='post_core', dumps=['d3'])]
            (cap/'dumps.json').write_text(json.dumps(dumps))
            (cap/'frame.json').write_text(json.dumps(dict(submits=[dict(cmds=[dict(events=events)])])))
            data = array('f', [1,2,3,0]*4 + [3,2,1,0]*4)
            path = cap/'after_p.f32'
            path.write_bytes(data.tobytes())
            roi = dict(rim=(0,0,4,1), sea=(0,1,4,2))
            rows = regional_colour(cap, roi, (4,2))
            self.assertAlmostEqual(rows['rim']['luminance'], 1.8596)
            self.assertAlmostEqual(rows['sea']['luminance'], 2.1404)
            self.assertEqual(rows['rim']['pixels'], 4)
            with self.assertRaises(ValueError):
                regional_colour(cap, roi, (4,3))
            for replacement in [b'', data.tobytes()[:-4], data.tobytes()+b'1234']:
                path.write_bytes(replacement)
                with self.assertRaises(ValueError):
                    regional_colour(cap, roi, (4,2))
            path.write_bytes(data.tobytes())
            with self.assertRaises(ValueError):
                regional_colour(cap, dict(bad=(0,0,5,1)), (4,2))
            data[0] = float('nan')
            path.write_bytes(data.tobytes())
            with self.assertRaises(ValueError):
                regional_colour(cap, roi, (4,2))
            path.unlink()
            with self.assertRaises(OSError):
                regional_colour(cap, roi, (4,2))
            path.write_bytes(array('f', [1,2,3,0]*8).tobytes())
            after['image'] = 'i2'
            (cap/'dumps.json').write_text(json.dumps(dumps))
            with self.assertRaises(ValueError):
                regional_colour(cap, roi, (4,2))
            after['image'] = 'i1'
            after['pass_ordinal'] = 4
            (cap/'dumps.json').write_text(json.dumps(dumps))
            with self.assertRaises(ValueError):
                regional_colour(cap, roi, (4,2))

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
