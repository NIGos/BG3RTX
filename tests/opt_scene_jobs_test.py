"""Noise-derived energy bands and fail-closed scene receipts."""
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.opt_r1.scene_analyze import energy_gate, analyze


class SceneGates(unittest.TestCase):
    def rows(self, values):
        return [dict(mean_rgb=[v]*3,luminance=v) for v in values]

    def test_identical_energy_zero_noise_passes_exactly(self):
        result=energy_gate(self.rows([1,1,1,1]))
        self.assertTrue(all(v['pass_'] and v['band']==0 for v in result.values()))

    def test_darkening_cannot_pass_zero_noise(self):
        result=energy_gate(self.rows([1,1,.99,1]))
        self.assertTrue(all(not v['pass_'] for v in result.values()))

    def test_off_repeat_variation_sets_band(self):
        result=energy_gate(self.rows([.999,1,1.0005,1.001]))
        self.assertTrue(all(v['pass_'] and v['band']>0 for v in result.values()))
        self.assertAlmostEqual(result['r']['delta'],0)

    def test_nonfinite_energy_invalid(self):
        with self.assertRaises(ValueError):
            energy_gate(self.rows([1,1,math.nan,1]))

    def test_energy_change_above_measured_noise_fails(self):
        result=energy_gate(self.rows([1,1.001,2,1.002]))
        self.assertTrue(all(not v['pass_'] for v in result.values()))

    def test_empty_receipts_never_pass(self):
        job=dict(expect_every=[],expect=[],expect_runner=[],forbid=[],candidate=dict(key='rt_scene_compact_holds'))
        with self.assertRaises(KeyError):
            analyze(job, [], '', '')


class MissingReceipts(unittest.TestCase):
    """Exercise missing observations independently of QA's own tested math."""
    def fixture(self, missing=None, unready=None):
        wins=[]
        lines=[]
        for i,label in enumerate(('off-a','off-b','on','off-c')):
            for j,kind in enumerate(('cost','energy','qa')):
                start=i*30+j*10
                window=label+'--static-a' if kind=='cost' else kind+'-'+label
                wins.append(dict(label=window,applied=start,end=start+10,stats_reset=start,ready=window!=unready))
                if missing==(label,kind):
                    continue
                if kind=='cost':
                    convert='.8' if label=='on' else '1.0'
                    total='1.8' if label=='on' else '2.0'
                    receipt=(f'scene build GPU static median convert+extract {convert} ms total median {total} ms; '
                        'gi: trace pass x GPU median 3.0 ms; ls: local-light shadow pass GPU median 1.0 ms; '
                        'rf: reflection pass GPU median 1.0 ms')
                    lines.extend([(start+1,receipt),(start+2,receipt)])
                elif kind=='energy':
                    lines.append((start+1,'[trace] frame 10 written: x -> capture'))
                else:
                    lines.append((start+1,f'[pan] burst {100+i} complete: 8 frames, 20 samples, transport ok -> burst'))
        job=dict(expect_every=[],expect=[],expect_runner=[],forbid=[],candidate=dict(serial_base=100))
        return job,wins,lines

    def run_receipt(self, missing=None, unready=None, qa_pass=True):
        job,wins,lines=self.fixture(missing,unready)
        colour=np.zeros((16,16,3),dtype='f4')
        capture=dict(codes=[colour]*8,albedo=[colour]*2)
        with patch('tools.live_batches.opt_r1.scene_analyze.gates.check',return_value=[]), \
             patch('tools.live_batches.opt_r1.scene_analyze.gates.stamped_lines',return_value=lines), \
             patch('tools.live_batches.opt_r1.scene_analyze.gates.windows',return_value=wins), \
             patch('tools.live_batches.opt_r1.scene_analyze.energy',return_value=dict(mean_rgb=[1]*3,luminance=1,dimensions=[16,16])), \
             patch('tools.live_batches.opt_r1.scene_analyze.burst',return_value=capture), \
             patch('tools.live_batches.opt_r1.scene_analyze.det.view_shift',return_value=(0,0,1,1)), \
             patch('tools.live_batches.opt_r1.scene_analyze.det.animated',return_value=np.zeros((16,16),bool)), \
             patch('tools.live_batches.opt_r1.scene_analyze.det.temporal',return_value=dict(pass_=qa_pass,**{'pass':qa_pass})):
            return analyze(job, [], '', '')

    def test_complete_receipt_passes(self):
        self.assertEqual(self.run_receipt()['status'],'PASS')

    def test_missing_cost_never_passes(self):
        result=self.run_receipt(missing=('on','cost'))
        self.assertEqual(result['status'],'FAIL')
        self.assertTrue(any('GPU periods' in e for e in result['errors']))

    def test_missing_float_export_invalid(self):
        with self.assertRaisesRegex(ValueError,'float pre-post export'):
            self.run_receipt(missing=('on','energy'))

    def test_missing_qa_burst_invalid(self):
        with self.assertRaisesRegex(ValueError,'rendered K8 burst'):
            self.run_receipt(missing=('on','qa'))

    def test_unacknowledged_window_invalid(self):
        with self.assertRaisesRegex(ValueError,'not acknowledged'):
            self.run_receipt(unready='on--static-a')

    def test_qa_failure_never_passes(self):
        self.assertEqual(self.run_receipt(qa_pass=False)['status'],'FAIL')



class TonightReceipts(unittest.TestCase):
    def test_real_complete_period_text_contract(self):
        from scripts.queue_gates import stamped_lines
        from tools.live_batches.ab_receipts import gpu_samples
        from tools.live_batches.opt_r1.scene_analyze import EXTRA
        # Reset ack plus real producer syntax; complete_period_lines yields strings.
        log = ("01:50:00.000 [stats] reset token=8 frame=3438\n"
               "01:50:02.000 scene build GPU (60 frames): median convert+extract 1.978 ms total median 3.090 ms\n"
               "01:50:04.000 scene build GPU (60 frames): median convert+extract 2.044 ms total median 3.110 ms\n"
               )
        lines = sorted(stamped_lines(log))
        window = dict(applied=6600, stats_reset=6600, end=6605)
        self.assertEqual(gpu_samples(lines, window, EXTRA['convert']), [1.978, 2.044])

    def test_post_diagnostics_are_optional_and_mask_is_scaled(self):
        import tempfile, json
        from tools.live_batches.ab_receipts import burst, moving_mask
        samples = []
        for frame in range(8):
            names = ['depth','final','rr_post_ac','rr_post_eb']
            if frame in (0,7): names += ['material','albedo']
            samples += [dict(name=name, file='unused', record_frame=frame, submit_frame=frame,
                             command_recording=1) for name in names]
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory/'burst.json').write_text(json.dumps(dict(K=8,edge_only=True,sample_stride=2,
                                                                valid_transport=True,samples=samples)))
            with patch('tools.live_batches.edge_flicker.analyze.decode',return_value=np.zeros((4,4,4),dtype='f4')):
                result = burst(directory)
                self.assertEqual(len(result['codes']),8)
                (directory/'burst.json').write_text(json.dumps(dict(K=8,edge_only=True,sample_stride=2,
                    valid_transport=True,samples=samples[:-1])))
                with self.assertRaisesRegex(ValueError,'incomplete frame'):
                    burst(directory)
        albedo = [np.zeros((4,4,3),dtype='f4')]*2
        self.assertEqual(moving_mask(albedo,albedo,(8,8)).shape,(8,8))

if __name__=='__main__':
    unittest.main()
