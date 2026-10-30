import unittest
from scripts.qa.pan_artifacts import fg_burst_evidence


class FG(unittest.TestCase):
    def fixture(self):
        return '\n'.join(f'00:01:00.{n*40:03} [sl] DLSS-G frame {100+n}: status 0x0, presented per app frame 2, displayed/rendered over the last 1 frames 2.00, api errors 0' for n in range(16))

    def test_exact_present_cadence_distinct_from_recording(self):
        r=fg_burst_evidence(list(range(100,116)),self.fixture())
        self.assertTrue(r['fg_active_every_captured_frame']);self.assertAlmostEqual(r['presentation_fps'],25)

    def test_missing_or_duplicate_frame_is_not_fg_proof(self):
        log=self.fixture();ids=list(range(100,116))
        self.assertFalse(fg_burst_evidence(ids,log.replace('frame 101:','other 101:'))['fg_active_every_captured_frame'])
        self.assertFalse(fg_burst_evidence(ids,log+'\n'+log.splitlines()[0])['fg_active_every_captured_frame'])

    def test_disabled_status_errors_and_no_rows(self):
        ids=list(range(100,116));log=self.fixture()
        for old,new in [('frame 2,','frame 1,'),('status 0x0','status 0x1'),('api errors 0','api errors 1')]:
            self.assertFalse(fg_burst_evidence(ids,log.replace(old,new))['fg_active_every_captured_frame'])
        self.assertIsNone(fg_burst_evidence(ids,'')['presentation_fps'])

    def test_nonmonotonic_timestamps_do_not_invent_fps(self):
        self.assertIsNone(fg_burst_evidence(list(range(100,116)),self.fixture().replace('00:01:00.600','00:00:59.000'))['presentation_fps'])


if __name__=='__main__':unittest.main()
