"""Offline fixtures for edge masks, attribution ranking, missing bursts and cuts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.edge_flicker.analyze import masks, energy, rank, burst, analyze, presents, registered_run, registered_endpoints, dialogue_metrics, shifted_mask, registration_structure, scaled_shift
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

    def test_curved_depth_is_not_an_edge_and_is_scale_invariant(self):
        y, x = np.indices(self.d.shape)
        curve = 4 - np.sqrt(np.maximum(0, 4-((x-40)/20)**2-((y-32)/20)**2))
        for scale in (.001, 1, 1000):
            d = np.where(self.d == 2, curve, 10)*scale
            edge, interior = masks([d.astype('f4')], [self.m])
            self.assertFalse(edge[30, 40])
            self.assertTrue(interior[30, 40])
            self.assertTrue(edge[12, 40])
            self.assertLess(edge.mean(), .2)

    def test_present_numbers_continue_across_captures(self):
        for start in (16, 128):
            with tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp)
                for n in reversed(range(start, start+16)):
                    Image.fromarray(np.full((3, 4, 3), n-start, 'u1')).save(p/f'p{n}.png')
                frames = presents(p)
                self.assertEqual(len(frames), 16)
                self.assertAlmostEqual(float(frames[15][0, 0, 0]), 15/255)
                (p/f'p{start+7}.png').unlink()
                with self.assertRaises(ValueError):
                    presents(p)

    def test_per_frame_registration_excludes_cuts_without_joining_gaps(self):
        a = np.random.default_rng(21).random((64, 80, 3))
        cut = np.roll(a, 9, axis=1)
        frames = [a, a, a, a, cut, a, a, cut, a]
        keep, report = registered_run(a, frames)
        self.assertEqual(keep, [0, 1, 2, 3])
        self.assertEqual(report['excluded'], [4, 5, 6, 7, 8])
        keep, _ = registered_run(a, [a*.5, a*2, 1-a, a])
        self.assertEqual(keep, [0, 1, 2, 3])
        with self.assertRaises(ValueError):
            registered_run(a, [a, a, cut, a])

    def test_window_offset_and_subtitle_exclusion(self):
        a = np.random.default_rng(31).random((160, 384, 3))
        # Corresponds to the top of the subtitle region, y=590 at 1920x800.
        stop = 118
        frames = []
        for i in range(6):
            f = np.roll(a, (3, 7), axis=(0, 1))
            f[stop:] = np.random.default_rng(i).random(f[stop:].shape)*50
            frames.append(f)
        keep, report = registered_run(a, frames)
        self.assertEqual(keep, list(range(6)))
        self.assertEqual(report['shift_px'], (3, 7))
        self.assertEqual(report['subtitle_top'], stop)
        self.assertEqual(registration_structure(a).shape, (118, 384))
        self.assertEqual(scaled_shift((3, 7), (160, 384), (320, 768)), (6, 14))

    def test_hair_residual_sparkle_and_rigid_control(self):
        frames = [np.full((160, 384, 3), .5, 'f4') for _ in range(8)]
        for i, f in enumerate(frames):
            f[4:66, 128:176] += .02*(-1)**i
        m = dialogue_metrics(frames, (0, 0))
        self.assertAlmostEqual(m['hair_box']['residual_x1000'], 40, places=3)
        self.assertEqual(m['hair_box']['sparkle_percent'], 100)
        self.assertAlmostEqual(m['hair_box']['mean_code'], .5, places=5)
        self.assertLess(m['rigid_control']['residual_x1000'], 1e-5)
        shifted = [np.roll(f, (2, -4), axis=(0, 1)) for f in frames]
        self.assertAlmostEqual(dialogue_metrics(shifted, (2, -4))['hair_box']['residual_x1000'], 40, places=3)
        # Pixels shifted out of bounds are discarded, never wrapped into a ROI.
        mask = np.zeros((5, 5), bool)
        mask[0, 0] = True
        self.assertFalse(shifted_mask(mask, (-1, 0)).any())

    def test_cut_endpoint_does_not_contaminate_baseline_masks(self):
        skin = self.m.copy()
        cut = np.zeros_like(skin)
        cut[..., 3] = 5/255
        self.assertIs(registered_endpoints([skin, cut], [0, 1, 2])[0], skin)
        self.assertEqual(len(registered_endpoints([skin, cut], [0, 1, 2])), 1)
        with self.assertRaisesRegex(ValueError, 'endpoint support'):
            registered_endpoints([skin, cut], [2, 3, 4])

    def test_batch_warmups_and_crash_policy(self):
        root = Path(__file__).resolve().parents[1]/'tools/live_batches'
        smoke = json.loads((root/'smoke_ultra/plans/su1_s8.json').read_text())
        self.assertEqual(smoke[0]['seconds'], 45)
        self.assertEqual(smoke[0]['motion_start'], 45)
        water = json.loads((root/'water_offscreen/plans/wo1_s8.json').read_text())
        self.assertIn('rt_water_hit_shade=2', water[0]['set'].split(','))
        job = json.loads((root/'edge_flicker/jobs/ef1-dialogue3-attribution.json').read_text())
        self.assertIn(r'\[crash\] exception 0x', job['forbid'])
        self.assertFalse(any('mesh pool full' in f for f in job['forbid']))
        plan = json.loads((root/'edge_flicker/plans/ef1.json').read_text())
        self.assertTrue(all(s['registration_policy'] == 'per-frame-exclude-unregistered' for s in plan))

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
                            depth = np.roll(self.d, 9, axis=1) if n == 7 else self.d
                            fields = [('depth', depth, 100), ('final', packed, 64)]
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
                            v = np.random.default_rng(22).integers(110, 146, (64,80,3), dtype='i2')
                            v[edge] += (amplitude//4)*(-1)**n
                            if n >= 12:
                                v = np.roll(v, 9, axis=1)
                            Image.fromarray(v.astype('u1')).save(d/f'p{128+n}.png')
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
            self.assertEqual(report['windows']['base-a']['registration']['rendered']['excluded'], [7])
            self.assertEqual(report['windows']['base-a']['registration']['presented']['excluded'], [12, 13, 14, 15])
            self.assertEqual(report['rendered']['candidate'], 'ao-off')
            self.assertEqual(report['presented']['candidate'], 'ao-off')
            json.dumps(report, allow_nan=False)


if __name__ == '__main__':
    unittest.main()
