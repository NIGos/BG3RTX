"""Offline fixtures for edge masks, attribution ranking, missing bursts and cuts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.edge_flicker.analyze import masks, energy, rank, burst, analyze
from tools.live_batches.edge_flicker.make_job import WINDOWS
from tools.live_batches.dialogue_shadows.pair_registration import registration


class EdgeFlickerTest(unittest.TestCase):
    def setUp(self):
        self.d = np.full((64, 80), 10, 'f4')
        self.d[12:52, 20:60] = 2
        self.m = np.zeros((64, 80, 4), 'f4')
        self.m[12:52, 20:60, 3] = 3/255
        self.m[12:52, 20:25, 3] = 5/255

    def test_masks_and_local_flicker(self):
        edge, interior = masks([self.d], [self.m])
        self.assertTrue(edge[30, 21])
        self.assertTrue(interior[30, 40])
        self.assertFalse(edge[0, 0])
        self.assertFalse(np.any(edge & interior))
        frames = [np.full((64, 80, 3), .5, 'f4') for _ in range(8)]
        for i, f in enumerate(frames):
            f[edge] += .1 * (-1)**i
        self.assertGreater(energy(frames, edge)['variance'], .01)
        self.assertLess(energy(frames, interior)['variance'], 1e-10)

    def test_ranking_and_repeat_floor(self):
        rows = {label: {'rendered': {'edge': {'variance': 1.0}}} for label, _ in WINDOWS}
        rows['base-b']['rendered']['edge']['variance'] = 1.1
        self.assertIsNone(rank(rows, 'rendered')['candidate'])
        rows['ao-off']['rendered']['edge']['variance'] = .1
        self.assertEqual(rank(rows, 'rendered')['candidate'], 'ao-off')

    def test_reject_missing_transport(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'burst.json').write_text(json.dumps(dict(K=8, edge_only=True,
                sample_stride=2, valid_transport=False, samples=[])))
            with self.assertRaises(ValueError):
                burst(p)

    def test_reject_missing_run_evidence(self):
        with self.assertRaises(ValueError):
            analyze('', '', Path)

    def test_cut_rejected_without_warp(self):
        a = np.random.default_rng(17).random((64, 80, 3)).astype('f4')
        self.assertTrue(registration(a, a)['valid'])
        self.assertFalse(registration(a, np.roll(a, 9, axis=1))['valid'])

    def test_complete_same_run_fixture(self):
        from tools.live_batches.edge_flicker.analyze import HERE
        plan = json.loads((HERE/'plans/ef1.json').read_text())
        albedo = np.random.default_rng(18).integers(0, 256, (64, 80, 4), dtype='u1')
        material = np.rint(self.m*255).astype('u1')
        edge, _ = masks([self.d], [self.m])
        def stamp(t):
            return f'{int(t//3600):02}:{int(t//60)%60:02}:{int(t)%60:02}.{round(t%1*1000):03}'
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = ['00:00:00.000 [auto] scene ready', '00:00:00.001 [hot] core generation 1 loaded',
                    '00:00:00.002 [ngx] RR create fixture: Success',
                    '00:00:00.003 [rrpost] exposure held (qa_exposure_hold=1) at EV 1']
            runner, t = [], 1000
            for i, step in enumerate(plan):
                runner.append(f'{stamp(t)} [pan-plan] window {i} set={step["set"]}')
                game.append(f'{stamp(t+.01)} [cfg] settings read')
                kind, _, label = step['label'].partition('-')
                if kind in ('rendered', 'presented'):
                    d = root/step['label']
                    d.mkdir()
                    amplitude = 1 if label == 'ao-off' else 24
                    if kind == 'rendered':
                        entries = []
                        for n in range(8):
                            v = np.full(self.d.shape, 512, 'i4')
                            v[edge] += amplitude*(-1)**n
                            packed = (v | (v << 10) | (v << 20)).astype('<u4')
                            fields = [('depth', self.d, 100), ('final', packed, 64)]
                            if n in (0, 7):
                                fields += [('material', material, 37), ('albedo', albedo, 37)]
                            for name, data, fmt in fields:
                                f = f'{n}-{name}.raw'
                                data.tofile(d/f)
                                entries.append(dict(name=name, file=f, record_frame=101+n,
                                    submit_frame=102+n, command_recording=n+1, format=fmt, width=80, height=64))
                        (d/'burst.json').write_text(json.dumps(dict(K=8, edge_only=True,
                            sample_stride=2, valid_transport=True, samples=entries)))
                        game.append(f'{stamp(t+1)} [pan] burst {step["serial"]} complete: 8 frames, 20 samples, transport ok -> {d}')
                    else:
                        for n in range(16):
                            v = np.full((64,80,3), 128, 'i2')
                            v[edge] += (amplitude//4)*(-1)**n
                            Image.fromarray(v.astype('u1')).save(d/f'p{n:02}.png')
                        game += [f'{stamp(t+1)} [sl] capturing 16 presented frames to {d}',
                                 f'{stamp(t+1.01)} [sl] edge capture format 64, extent 160x128',
                                 f'{stamp(t+2)} [sl] present capture done']
                        if label != 'fg-off':
                            game.append(f'{stamp(t+2.5)} [sl] DLSS-G frame 90: status 0x0, presented per app frame 2, displayed/rendered over the last 30 frames 2.00 (batch-of-2 presents 0), api errors 0')
                t += step['seconds']
            runner += [f'{stamp(t)} [motion-plan] complete phases={len(plan)}',
                       '[window-mode] visible_unfocused_fg', 'Run ended: done']
            report = analyze('\n'.join(game), '\n'.join(runner), Path)
            self.assertEqual(report['status'], 'ATTRIBUTION_ONLY')
            self.assertEqual(report['rendered']['candidate'], 'ao-off')
            self.assertEqual(report['presented']['candidate'], 'ao-off')
            json.dumps(report, allow_nan=False)


if __name__ == '__main__':
    unittest.main()
