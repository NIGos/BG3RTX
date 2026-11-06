"""Statistical replay gates: empirical bounds, signed bias, incomplete data and timing."""
import csv
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools import replay_numeric as numeric

class ReplayNumericTests(unittest.TestCase):
    def row(self, pair, value=0):
        return dict(pair=pair,different_pixels=value,max_abs=value,mean_abs_diff=value,
            abs_mean_diff=value,mean_diff=value)

    def test_nearest_rank_small_sample(self):
        self.assertEqual(numeric.percentile99(range(40)),39)
        self.assertEqual(numeric.percentile99(range(100)),98)

    def test_each_pair_and_signed_bias(self):
        control={('specular',0):[self.row(i,1) for i in range(40)]}
        candidate={('specular',0):[self.row(i,0) for i in range(40)]}
        self.assertTrue(numeric.compare(control,candidate)['passed'])
        candidate[('specular',0)][39]['abs_mean_diff']=2
        report=numeric.compare(control,candidate)
        self.assertFalse(report['passed'])
        self.assertEqual(report['channels'][0]['exceeded_pairs']['abs_mean_diff'],1)
        candidate[('specular',0)][39]['abs_mean_diff']=1
        candidate[('specular',0)][39]['different_pixels']=2
        self.assertFalse(numeric.compare(control,candidate)['passed'])

    def test_incomplete_duplicate_nonfinite_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'numeric.csv'
            fields='pair,output,channel,pixels,different_pixels,max_abs,mean_diff,mean_abs_diff,nonfinite\n'
            valid='1,specular,0,10,0,0,0,0,0\n'
            p.write_text(fields+valid)
            self.assertRaises(ValueError,numeric.read_numeric,p)
            p.write_text(fields+valid*40)
            self.assertRaises(ValueError,numeric.read_numeric,p)
            p.write_text(fields+valid.replace(',0,0,0,0,0\n',',0,nan,0,0,0\n'))
            self.assertRaises(ValueError,numeric.read_numeric,p,1)

    def test_timing_confidence_matches_cpp(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'ab.csv'
            with p.open('w',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=['delta_ms','A_ms','B_ms','counters_equal'])
                writer.writeheader()
                for i in range(40):writer.writerow(dict(delta_ms=i,A_ms=1,B_ms=i+1,counters_equal=1))
            summary=numeric.timing(p)
            self.assertEqual(summary['ci95_ms'],[13,26])
            self.assertEqual(summary['median_ms'],19.5)

if __name__=='__main__':unittest.main()
