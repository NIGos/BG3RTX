import unittest
from scripts.qa.rebase_gate import analyze


class GateTest(unittest.TestCase):
    def fixture(self, missing=False, bad=False, unreproduced=False):
        plan=[]; runner=[]; log=[]
        for i,(mode,on) in enumerate((m,o) for m in (0,100,33) for o in (0,1)):
            step={'seconds':90,'motion_start':5,'motion_end':85,'repeat_ms':mode,'set':f'rt_scene_rebase={on}'}
            plan.append(step)
            start=i*90
            def stamp(t): return f'00:{t//60:02}:{t%60:02}.000'
            runner.append(f'{stamp(start)} [pan-plan] window {i}')
            for j in range(10,81,5):
                t=stamp(start+j)
                match=99.55 if mode==0 or on or unreproduced else 30.0 if mode==33 else 89.0
                if bad and on and mode: match=98.0
                log.append(f'{t} rt stats (120 frames): primary-ray check match {match}%, miss 0.000%')
                log.append(f'{t} rt passes: GPU cost 1.000 ms/frame TLAS this frame 0, previous frame 120, none {120 if missing and on else 0}')
                log.append(f'{t} [watch] presents 100 (displayed 100)')
        return '\n'.join(log),'\n'.join(runner),plan

    def test_pass(self): self.assertEqual(analyze(*self.fixture())['gate'],'PASS')
    def test_missing_rt_never_passes(self): self.assertEqual(analyze(*self.fixture(missing=True))['gate'],'FAIL')
    def test_substatic_improvement_fails(self): self.assertEqual(analyze(*self.fixture(bad=True))['gate'],'FAIL')
    def test_no_positive_control_fails(self): self.assertEqual(analyze(*self.fixture(unreproduced=True))['gate'],'FAIL')


if __name__=='__main__': unittest.main()
