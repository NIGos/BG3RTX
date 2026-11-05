import random
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def pixel(x,y):
    n=(x&7)|((y&3)<<3)
    return ((x&~7)+(n&1)+((n>>1)&2)+((n>>2)&4),
            (y&~3)+((n>>1)&1)+((n>>2)&2))

class ExactCandidates(unittest.TestCase):
    def test_bijection_edges_and_subgroups(self):
        for w,h in [(1,1),(7,9),(8,8),(17,15),(2560,1067)]:
            # Every full tile has the same bijection, so edge groups suffice.
            for gx,gy in {(0,0),((w-1)//8,(h-1)//8)}:
                got=set()
                for lane in range(64):
                    x,y=pixel(gx*8+lane%8,gy*8+lane//8)
                    self.assertEqual(lane//32,((y&7)*8+(x&7))//32)
                    if x<w and y<h: got.add((x,y))
                want={(x,y) for y in range(gy*8,min(gy*8+8,h))
                      for x in range(gx*8,min(gx*8+8,w))}
                self.assertEqual(got,want)
    def test_divergent_counter_bins(self):
        rng=random.Random(1032)
        for size in (32,64):
            for _ in range(1024):
                logical=rng.sample(range(size),rng.randrange(1,size+1))
                bins={i:rng.randrange(64) for i in logical}
                self.assertEqual(min((i<<6)|bins[i] for i in logical)&63,bins[min(logical)])
    def test_ao_half_exact(self):
        for code in range(1024):
            x=code/1023.0
            rgba=struct.pack('<4e',x,0,0,0)
            self.assertEqual(rgba[:2],struct.pack('<e',x))
    def test_ao_resource_warmup(self):
        # Compile the actual production body with a failure-injecting host.
        text=(ROOT/'core/gi.cpp').read_text()
        start=text.index('bool ensure_images(')
        brace=text.index('{',start); end=brace+1; depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}'); end+=1
        fixture=(ROOT/'tests/gi_perm_resource_fixture.cpp').read_text()
        fixture=fixture.replace('// INSERT ensure_images',text[start:end])
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'warm.cpp';source.write_text(fixture)
            exe=Path(tmp)/'warm.exe'
            result=subprocess.run(['cl.exe','/nologo','/std:c++20','/EHsc',str(source),
                                   '/Fe:'+str(exe)],cwd=tmp,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_diagnostics_remain_rgba(self):
        for f in ('pan_primary.hlsl','rt_local_shadows.hlsl'):
            s=(ROOT/'shaders/gi'/f).read_text()
            self.assertIn('gAux[',s)
        consumers=['gi_ao_apply.hlsl','rt_local_shadows.hlsl','ls_ref/ls_ref_body.hlsli']
        for f in consumers:
            s=(ROOT/'shaders/gi'/f).read_text()
            self.assertNotIn('gAux[fp].x',s)

if __name__=='__main__': unittest.main()
