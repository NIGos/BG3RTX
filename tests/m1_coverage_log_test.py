"""Synthetic existing-log tests only. No capture, queue, game or GPU calls."""
import unittest
from scripts.qa.fg_motion_gate import coverage_quality


def fixture():
    plan = []
    for label, kind, speed, direction, alpha in [('native-A','static',0,0,1),
            ('opaque','static',0,0,0),('native-A-repeat','static',0,0,1),
            ('slow+','rotate',60,1,1),('slow-','rotate',60,-1,1),
            ('fast+','rotate',180,1,1),('fast-','rotate',180,-1,1),
            ('native-cost','static',0,0,1)]:
        plan.append(dict(label=label,kind=kind,speed=str(speed),pixels_per_second=speed,
            direction=direction,repeat_ms=0,seconds=90,motion_start=15,motion_end=75,
            set='rt_alpha_test='+str(alpha)+',rt_primary_check='+str(int('cost' not in label))))
    def stamp(t): return f'{t//3600:02}:{t//60%60:02}:{t%60:02}.000'
    log = ['00:00:01.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x18): ok']
    runner = []
    for i, step in enumerate(plan):
        runner.append(f'{stamp(i*90)} [pan-plan] window {i}')
        for offset in range(10,81,5):
            t = stamp(i*90+offset)
            match = 60 if step['label']=='opaque' else 99.5
            motion = step['pixels_per_second']/300 if step['kind']=='rotate' else 0
            log.extend([f'{t} [watch] presents 100 (displayed 200)',
                f'{t} rt passes: GPU cost 4.000 ms/frame (average of 300 frames); TLAS this frame 300, previous frame 0, none 0',
                f'{t} [sl] DLSS-G frame 600: status 0x0, presented per app frame 2, displayed/rendered over the last 120 frames 2.00'])
            if 'cost' not in step['label']:
                log.extend([f'{t} rt stats (120 frames, alpha test {"off" if step["label"]=="opaque" else "on"}): primary-ray check match {match:.3f}%, miss 0.000%',
                    f'{t} rt epoch (120 frames): GPU camera frames 120, scene snapshots 120; camera motion mean {motion:.6f} deg/frame, max {motion:.6f} deg/frame, signed yaw {motion*step["direction"]:.6f} deg/frame; scene rotation max 0.000000 deg, translation max 0.000000 m; pool hit match 99.500% of 1000, soup hit match 99.500% of 100'])
        for offset in (54,74):
            t=stamp(i*90+offset); count=i*1000+offset
            log.extend([f'{t} [alpha] last 300 frames: 10 alpha-tested draws',
                f'{t} [alpha hair] native coverage records 100; invalid/missing native state 0; unsupported native interfaces total 0; retired bounds-invalid primitives 0;',
                f'{t} [alpha scalp] native hard records 1; native camera dither records 100; invalid/missing native state 0;',
                f'{t} [alpha] this frame\'s shaders by triangles: 5a03e5b678b2d040 tex; 9e77575a9f377ec5 tex',
                f'{t} [geo] RT capture, last 300 frames: alpha region 710 MB (peak demand 661 MB), opaque peak 635 MB of a 1408 MB buffer; per frame: alpha draws made opaque (region full) 0.00, opaque draws dropped (buffer full) 0.00,',
                f'{t} [sun current] calls {count}, recorded {count}, same-submit inserted {count};',
                f'{t} sun current native-domain coverage (600 frames): certified 100 pixels, fallback 10, scene unready 0; frame/camera/rays rejected 0/0/0, aux active 0, cone escape 5, unknown candidate 5; actual selected/next ray samples 90, native white without ray 20'])
    runner.append(f'[motion-plan] complete phases={len(plan)}')
    status=dict(result='done',plan_completed=True,observed_exit_code=None,real_present=True,foreground=True,visible_unfocused=False)
    return '\n'.join(log),'\n'.join(runner),plan,status


class M1CoverageTest(unittest.TestCase):
    def test_valid_logs_cannot_invent_pose_or_roi(self):
        report=coverage_quality(*fixture())
        self.assertEqual(report['log_gate'],'PASS',report['invalid'])
        self.assertEqual(report['gate'],'INVALID')
        self.assertEqual(report['pose_roi']['gate'],'UNAVAILABLE')
        self.assertTrue(report['phases'][1]['opaque_control'])
        self.assertFalse(report['phases'][-1]['coverage_primary_required'])

    def test_cli_writes_fail_closed_report(self):
        import json
        import subprocess
        import sys
        import tempfile
        from pathlib import Path
        log,runner,plan,status=fixture()
        with tempfile.TemporaryDirectory() as scratch:
            root=Path(scratch)
            for name,value in [('log',log),('runner',runner),('plan',json.dumps(plan)),('status',json.dumps(status))]:
                (root/name).write_text(value,encoding='utf-8')
            args=[sys.executable,'-m','scripts.qa.fg_motion_gate','--coverage']
            for name in ('log','runner','plan','status','out'):
                args.extend(['--'+name,str(root/name)])
            result=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,1,result.stderr)
            report=json.loads((root/'out').read_text(encoding='utf-8'))
            self.assertEqual(report['log_gate'],'PASS')
            self.assertEqual(report['gate'],'INVALID')
            self.assertIn('automatic pose/ROI UNAVAILABLE',result.stdout)

    def changed(self,old,new):
        log,runner,plan,status=fixture()
        return coverage_quality(log.replace(old,new),runner,plan,status)

    def test_attempted_rays_plus_white_are_not_coverage(self):
        report=self.changed('certified 100 pixels','certified 20 pixels')
        self.assertEqual(report['log_gate'],'INVALID')
        self.assertTrue(any('native-white' in reason for reason in report['invalid']))

    def test_missing_rt_is_invalid(self):
        self.assertEqual(self.changed('this frame 300, previous frame 0, none 0','this frame 0, previous frame 0, none 300')['log_gate'],'INVALID')

    def test_stale_rt_is_not_same_frame_proof(self):
        self.assertEqual(self.changed('this frame 300, previous frame 0','this frame 0, previous frame 300')['log_gate'],'INVALID')

    def test_ignored_coverage_toggle_is_invalid(self):
        self.assertEqual(self.changed('alpha test off','alpha test on')['log_gate'],'INVALID')

    def test_capacity_overflow_is_invalid(self):
        self.assertEqual(self.changed('region full) 0.00','region full) 1.00')['log_gate'],'INVALID')

    def test_rounded_zero_does_not_hide_draw_warning(self):
        log,runner,plan,status=fixture()
        log+='\n00:00:55.000 [geo] WARNING alpha-tested region full: one draw made OPAQUE'
        self.assertEqual(coverage_quality(log,runner,plan,status)['log_gate'],'INVALID')

    def test_no_actual_generation(self):
        self.assertEqual(self.changed('displayed 200','displayed 100')['log_gate'],'INVALID')

    def test_no_native_records(self):
        self.assertEqual(self.changed('native coverage records 100','native coverage records 0')['log_gate'],'INVALID')

    def test_bounds_invalid(self):
        self.assertEqual(self.changed('bounds-invalid primitives 0','bounds-invalid primitives 1')['log_gate'],'INVALID')

    def test_unready_packet(self):
        self.assertEqual(self.changed('scene unready 0','scene unready 1')['log_gate'],'INVALID')

    def test_target_shader_missing(self):
        self.assertEqual(self.changed('5a03e5b678b2d040','0000000000000000')['log_gate'],'INVALID')

    def test_no_actual_rotation(self):
        log,runner,plan,status=fixture()
        import re
        log=re.sub(r'camera motion mean [\d.]+ deg/frame, max [\d.]+ deg/frame, signed yaw [-\d.]+',
                   'camera motion mean 0.000000 deg/frame, max 0.000000 deg/frame, signed yaw 0.000000',log)
        self.assertEqual(coverage_quality(log,runner,plan,status)['log_gate'],'INVALID')

    def test_deciding_primary_regression_is_failure(self):
        log,runner,plan,status=fixture()
        lines=[]
        for line in log.splitlines():
            if line.startswith('00:05:') or line.startswith('00:06:'):line=line.replace('match 99.500%','match 98.000%')
            lines.append(line)
        report=coverage_quality('\n'.join(lines),runner,plan,status)
        self.assertEqual(report['log_gate'],'FAIL',report['invalid'])
        self.assertEqual(report['gate'],'FAIL')

    def test_counter_window_cannot_straddle_phase(self):
        # No eligible complete 600-frame window after the settling interval.
        report=self.changed('coverage (600 frames)','coverage (1800 frames)')
        self.assertEqual(report['log_gate'],'INVALID')


if __name__=='__main__': unittest.main()
