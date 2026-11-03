"""Offline trace-budget estimator check on the existing synthetic lit replay pack.
Checks matched production trace pixels, not RR/composite or real-scene qualification.
Run serially after other shader work. Small fixture copies only; never copies game captures.
"""
import json
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools import shader_identity as si

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--frames",type=int,default=32)
    args=ap.parse_args()
    if args.frames<4 or args.frames%4: ap.error("--frames must be a multiple of four")
    out=ROOT/'build/gpu-opt2-offline'
    out.mkdir(parents=True,exist_ok=True)
    exe=ROOT/'bin/frame_replay.exe'
    source=ROOT/'build/frame_replay_test/lit'
    if not source.exists():
        subprocess.run([str(exe),'--self-test',str(ROOT/'build/frame_replay_test')],check=True)
    request='gi/gi_trace_rt_hit_direct.hlsl'
    tree=si.SourceTree.worktree(ROOT)
    result=si.compile_requests(tree,[request],jobs=1)[request]
    assert result.ok,result.log
    spv=result.path()
    means={0:[],1:[]}; maxdiff=0.0
    source_hash=hashlib.sha256(b"".join(f.name.encode()+f.read_bytes() for f in sorted(source.iterdir()) if f.is_file())).hexdigest()
    harness_hash=hashlib.sha256(exe.read_bytes()).hexdigest()
    for frame in range(args.frames):
        data=[]
        for shift in (0,1):
            pack=out/f'pack-{frame}-{shift}'
            shutil.copytree(source,pack,dirs_exist_ok=True)
            push=bytearray((pack/'push.bin').read_bytes())
            w,h=struct.unpack_from('<II',push)
            hw,hh=((w+1)//2,(h+1)//2) if shift else (w,h)
            struct.pack_into('<II',push,8,hw,hh)
            struct.pack_into('<I',push,16,frame)
            struct.pack_into('<I',push,32,1)
            struct.pack_into('<I',push,132,shift)
            (pack/'push.bin').write_bytes(push)
            # Frame is both the random seed and the native material epoch. Update this
            # synthetic fixture's header and populated rows together, never a real pack.
            records=bytearray((pack/'records.bin').read_bytes())
            struct.pack_into('<I',records,0,frame)
            for at in range(16,len(records)-255,256):
                if struct.unpack_from('<I',records,at)[0]:
                    struct.pack_into('<I',records,at+28,frame)
            (pack/'records.bin').write_bytes(records)
            lines=[]
            for line in (pack/'manifest.txt').read_text().splitlines():
                t=line.split()
                if t and t[0]=='dispatch': line=f'dispatch {(hw+7)//8} {(hh+7)//8} 1'
                if t and t[0]=='output': line=' '.join(t[:4]+[str(hw),str(hh)])
                lines.append(line)
            (pack/'manifest.txt').write_text('\n'.join(lines)+'\n')
            run=out/f'run-{frame}-{shift}'
            key=hashlib.sha256((source_hash+harness_hash+result.digest).encode()+records+push+(pack/'manifest.txt').read_bytes()).hexdigest()
            stamp=run/'input.sha256'
            cached=stamp.exists() and stamp.read_text()==key and (run/'giRaw.raw').exists()
            if not cached:
                proc=subprocess.run([str(exe),'--pack',str(pack),'--spv',str(spv),'--out',str(run)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
                (out/f'run-{frame}-{shift}.log').write_text(proc.stdout)
                if proc.returncode: raise RuntimeError(proc.stdout)
                stamp.write_text(key)
            raw=np.fromfile(run/'giRaw.raw',dtype='<f2').astype(np.float64).reshape(hh,hw,4)
            assert np.isfinite(raw).all()
            assert raw[...,:3].sum()>0,"invalid fixture: zero energy (check material epoch)"
            means[shift].append(float(np.mean(raw[...,:3]@[.2126,.7152,.0722])))
            data.append(raw)
            print(f'[gpu-opt2] {frame*2+shift+1}/{args.frames*2} replay dispatches complete (cached {cached})',flush=True)
        selected=data[0][(frame>>1)&1::2,frame&1::2]
        maxdiff=max(maxdiff,float(np.max(np.abs(selected-data[1]))))
    full=np.array(means[0]);half=np.array(means[1]);delta=half-full
    # Sampling phases rotate with independent time seeds; report a four-standard-error band.
    se=float(delta.std(ddof=1)/np.sqrt(len(delta)))
    relative=float(delta.mean()/full.mean())
    report=dict(frames=args.frames,discarded_measurement='Initial seed sweep was INVALID: material epochs did not track frame seeds; corrected before this report',scope='Synthetic lit pack, production GI trace only; no real-scene, reflection or RR qualification',full_mean=float(full.mean()),half_mean=float(half.mean()),relative_mean_error=relative,paired_standard_error=se,four_standard_error_relative_band=4*se/float(full.mean()),matched_pixel_max_abs_error=maxdiff,mean_within_noise=abs(float(delta.mean()))<=4*se,finite=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    return 0 if report['mean_within_noise'] and maxdiff==0 else 1
if __name__=='__main__': raise SystemExit(main())
