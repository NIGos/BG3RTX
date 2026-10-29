import unittest
from scripts.qa.fg_log_audit import audit


class AuditTest(unittest.TestCase):
    def test_flags_are_not_toggles_and_legacy_identity_is_unknown(self):
        log='\n'.join([
            '09:30:00.000 [sl] slDLSSGSetOptions(mode off, frames to generate 1, flags 0x18): ok',
            '09:30:01.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x18): ok',
            '09:30:02.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x19): ok',
            '09:30:03.000 [sl] a tagged image was destroyed; waiting for the passes to show it again',
            '09:30:04.000 [sl] slDLSSGSetOptions(mode off, frames to generate 1, flags 0x19): ok'])
        r=audit(log)
        self.assertEqual(r['options_calls'],4)
        self.assertEqual(r['mode_transitions'],2)
        self.assertEqual(r['off_preceded_by_destroy_since_previous_options'],1)
        self.assertTrue(r['decisions'][-1]['invalid_input'].startswith('UNKNOWN'))

    def test_no_destroy_is_not_invented_and_gpu_line_numbers_are_exact(self):
        log='\n'.join([
            '09:30:00.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x18): ok',
            '09:30:01.000 [sl] slDLSSGSetOptions(mode off, frames to generate 1, flags 0x18): ok',
            '09:30:02.000 [core] rt passes: GPU cost 4.269 ms/frame'])
        r=audit(log)
        self.assertEqual(r['off_preceded_by_destroy_since_previous_options'],0)
        self.assertEqual(r['gpu_lines'][0]['line'],3)
    def test_sdk_focus_disablement_is_not_an_invalid_input(self):
        log='\n'.join([
            '09:30:00.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x18): ok',
            '09:30:01.000 [sl] DLSS-G disabled: window not focused',
            '09:30:01.000 [sl] DLSS-G interpolation state changed from enabled to disabled (mode=sl::DLSSGMode::eOn, numFramesToGenerate=1)'])
        r=audit(log)
        self.assertEqual(r['mode_transitions'],0)
        self.assertEqual(r['sdk_transitions'][0]['reason'],'window not focused')
    def test_existing_readiness_identifies_ui_stale_and_ready_cooldown(self):
        def check(gate,ready):
            return audit('\n'.join([
                '09:30:00.000 [sl] slDLSSGSetOptions(mode on, frames to generate 1, flags 0x18): ok',
                f'09:30:01.000 [sl] readiness frame 100: requested 2 last option 2 host running 1 ready {ready} stable 0 gate {gate};',
                '09:30:01.000 [sl] slDLSSGSetOptions(mode off, frames to generate 1, flags 0x18): ok']))['decisions'][-1]
        self.assertEqual(check('0x40',0)['invalid_input'],'UI stale')
        self.assertTrue(check('0x80',1)['invalid_input'].startswith('NONE missing/stale'))


if __name__=='__main__': unittest.main()
