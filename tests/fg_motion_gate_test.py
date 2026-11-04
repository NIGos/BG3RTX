import unittest
import re
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
from scripts.qa.fg_motion_gate import quality


def fixture(no_generation=False,missing=False,reference_bad=True,unreproduced=False,no_motion=False):
    plan=[]
    def add(kind,fg,variant,speed=0,direction=0):
        plan.append(dict(kind=kind,fg=fg,variant=variant,speed=str(speed),direction=direction,pixels_per_second=speed,
            seconds=60,motion_start=5,motion_end=55,repeat_ms=(100 if speed==60 else 33) if kind=='pan' else 0,
            set='rt_scene_rebase='+str(int(variant=='rebase'))))
    for variant in ('baseline','rebase','current_destroy_only'):
        for fg in (2,0): add('static',fg,variant)
    for kind in ('rotate','pan'):
        for speed in (60,180):
            for direction in ((-1,1) if kind=='rotate' else (1,)):
                for variant in ('baseline','rebase','current_destroy_only'):
                    for fg in (2,0): add(kind,fg,variant,speed,direction)
    log=[]; runner=[]
    def stamp(t): return f'{t//3600:02}:{t//60%60:02}:{t%60:02}.000'
    for i,s in enumerate(plan):
        runner.append(f'{stamp(i*60)} [pan-plan] window {i}')
        moving=s['kind']!='static'; fg=s['fg']
        if moving and fg==2 and s['variant']=='baseline':
            for j,mode in ((20,'on'),(26,'off'),(29,'on')):
                log.append(f'{stamp(i*60+j)} [sl] slDLSSGSetOptions(mode {mode}, frames to generate 1, flags 0x18): ok')
        for j in range(10,56,5):
            t=stamp(i*60+j)
            match=70 if moving and fg==2 and s['variant']=='baseline' and not unreproduced else 99.2
            if reference_bad and fg==0 and moving: match=30
            motion=s['pixels_per_second']/300 if s['kind']=='rotate' and not no_motion else 0
            origin=.1 if s['kind']=='pan' and not no_motion else 0
            log.extend([
                f'{t} primary-ray check match {match}%, miss 0.000%',
                f'{t} rt passes: GPU cost 15.000 ms/frame TLAS this frame 0, previous frame 300, none {120 if missing and fg==2 else 0}',
                f'{t} [watch] presents 100 (displayed {200 if fg==2 and not no_generation else 100})',
                f'{t} [sl] DLSS-G frame 600: status 0x0, presented per app frame {2 if fg==2 and not no_generation else 1}, displayed/rendered over the last 60 frames 2.00',
                f'{t} rt epoch (120 frames): GPU camera frames 120, scene snapshots 120; camera motion mean {motion:.6f} deg/frame, max {motion:.6f} deg/frame, signed yaw {motion*s["direction"]:.6f} deg/frame; scene rotation max 0.100000 deg, translation max 0.100000 m; pool hit match {match}% of 1000, soup hit match {match}% of 100; camera translation motion max {origin:.6f} m/frame, precision bound 0.000001 m/frame',
                f'{t} rt stats (120 frames): material classes: non-standard 0%',
                f'{t} ls stats (120 frames): 1.00M px/frame; lights per pixel: listed 1',
                f'{t} rt stats (120 frames): hybrid GI 1.00M rays/frame',
                f'{t} rf stats (120 frames): 1.00M px/frame',
                f'{t} [ngx] gpu RR: guides 0.001 ms + evaluate 2.000 ms'])
    runner.append(f'[motion-plan] complete phases={len(plan)}')
    return '\n'.join(log),'\n'.join(runner),plan


class FGMotionTest(unittest.TestCase):
    def test_fg_on_decides_even_if_fg_off_match_is_bad(self):
        r=quality(*fixture())
        self.assertEqual(r['gate'],'PASS',r['invalid'])
        self.assertEqual(r['deciding_fg'],2)
        self.assertEqual(r['candidates']['current_destroy_only']['gate'],'PASS')
    def test_requested_fg_without_generation_never_passes(self): self.assertEqual(quality(*fixture(no_generation=True))['gate'],'FAIL')
    def test_missing_rt_never_passes(self): self.assertEqual(quality(*fixture(missing=True))['gate'],'FAIL')
    def test_no_fg_on_positive_control_fails(self): self.assertEqual(quality(*fixture(unreproduced=True))['gate'],'FAIL')
    def test_ignored_posted_input_fails(self): self.assertEqual(quality(*fixture(no_motion=True))['gate'],'FAIL')
    def test_incomplete_matrix_fails_without_exception(self):
        log,runner,plan=fixture()
        self.assertEqual(quality(log,'',plan)['gate'],'FAIL')
    def test_destroy_key_needs_observed_toggle_reduction(self):
        log,runner,plan=fixture()
        log='\n'.join(line for line in log.splitlines() if 'slDLSSGSetOptions' not in line)
        r=quality(log,runner,plan)
        self.assertEqual(r['candidates']['rebase']['gate'],'PASS')
        self.assertEqual(r['candidates']['current_destroy_only']['gate'],'FAIL')
    def test_fg_off_reference_must_really_stop_generating(self):
        log,runner,plan=fixture()
        self.assertEqual(quality(log.replace('presents 100 (displayed 100)','presents 100 (displayed 200)'),runner,plan)['gate'],'FAIL')
    def test_primary_only_without_applied_effects_never_passes(self):
        log,runner,plan=fixture()
        log='\n'.join(line for line in log.splitlines() if 'rf stats' not in line)
        self.assertEqual(quality(log,runner,plan)['gate'],'FAIL')
    def test_matrix_can_cross_midnight(self):
        log,runner,plan=fixture()
        def shift(text):
            def stamp(m):
                t=(int(m[1])*3600+int(m[2])*60+int(m[3])+23*3600+45*60)%(24*3600)
                return f'{t//3600:02}:{t//60%60:02}:{t%60:02}.000'
            return re.sub(r'(\d\d):(\d\d):(\d\d)\.000',stamp,text)
        r=quality(shift(log),shift(runner),plan)
        self.assertEqual(r['gate'],'PASS',r['invalid'])
    def test_lightweight_dispatch_counts_preserve_primary_gate(self):
        log,runner,plan=fixture()
        lines=[]
        for line in log.splitlines():
            if any(tag in line for tag in ('material classes:','ls stats','hybrid GI','rf stats')): continue
            lines.append(line)
            if 'primary-ray check' in line:
                lines.append(line[:12]+' rt applied (120 frames): sun 120 GI 120 local 120 reflections 120 (recorded dispatches; GPU timings separate)')
        self.assertEqual(quality('\n'.join(lines),runner,plan)['gate'],'PASS')
        self.assertEqual(quality('\n'.join(lines).replace('reflections 120','reflections 0'),runner,plan)['gate'],'FAIL')


if __name__=='__main__': unittest.main()
