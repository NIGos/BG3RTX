"""Animation census parser contracts, including declared-but-unused time."""
import struct
import tempfile
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.assets.animated_materials import time_reads, classes, compact_counts

def op(code,*a): return [(len(a)+1)<<16|code,*a]
def shader(load=True,member=0,binding=0):
    w=[0x07230203,0x10000,0,30,0]
    w+=op(15,4,1,0)+op(71,2,34,0)+op(71,2,33,binding)+op(43,3,4,member)
    w+=op(65,3,5,2,4)
    if load:w+=op(61,3,6,5)
    spv=struct.pack('<%dI'%len(w),*w)
    h=bytearray(50);h[:4]=b'SHD8';struct.pack_into('<I',h,10,len(spv))
    return bytes(h)+spv

class CensusTest(unittest.TestCase):
    def test_actual_time_load(self):self.assertEqual(time_reads(shader()),['FS'])
    def test_unused_and_other_members(self):
        self.assertEqual(time_reads(shader(False)),[])
        self.assertEqual(time_reads(shader(member=1)),[])
        self.assertEqual(time_reads(shader(binding=2)),[])
    def test_overlapping_classes(self):
        self.assertEqual(classes('ENV_Lava_Flowmap_Bounce ScrollingSpeed'),['scroll','flowmap','displacement'])
    def test_compact_bounds_and_material_join(self):
        mid='01234567-0123-0123-0123-0123456789ab'
        row=bytearray(912);row[804:840]=mid.encode()
        raw=struct.pack('<8sII',b'B3PAKS1\0',4,1)+row
        report={'materials':{mid:{'template':'scroll.lsf','classes':['scroll']}}}
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'table.bin';p.write_bytes(raw)
            result=compact_counts(p,report)
            self.assertEqual(result['classes']['scroll'],dict(rows=1,instances=1,templates=1))
            self.assertEqual(result['unresolved_ids'],[])
            for invalid in (raw[:-1],raw+b'x',b'not a table'):
                p.write_bytes(invalid)
                with self.assertRaises(ValueError):compact_counts(p,report)

if __name__=='__main__':unittest.main()
