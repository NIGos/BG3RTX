"""Affected reference variants only: old direct SPIR-V identity + two-surface validation.
CPU compiler tools only. No Vulkan device/game/queue calls; at most 2 compilers.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--dxc',type=Path,required=True)
    parser.add_argument('--baseline',default='90d7930')
    args=parser.parse_args()
    out=ROOT/'captures/opaque-two-bounce';out.mkdir(parents=True,exist_ok=True)
    frozen={p:hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('shaders','common') for p in (ROOT/folder).rglob('*') if p.is_file()}
    archive=out/'baseline.zip'
    subprocess.run(['git','archive','--format=zip','--output',str(archive),args.baseline,'shaders','common'],cwd=ROOT,check=True)
    baseline=Path(tempfile.mkdtemp(prefix='bg3-bounce-'))
    with zipfile.ZipFile(archive) as data:data.extractall(baseline)
    validator=args.dxc.with_name('spirv-val'+args.dxc.suffix)
    def compile_one(source,destination,includes):
        destination.parent.mkdir(parents=True,exist_ok=True)
        first=source.read_text(encoding='utf-8').splitlines()[0]
        options=shlex.split(first[len('// dxc:'):]) if first.startswith('// dxc:') else []
        result=subprocess.run([str(args.dxc),str(source),'-spirv','-fspv-target-env=vulkan1.3','-HV','2021','-T','cs_6_6','-E','main','-I',str(includes),'-D','BG3RTX=1','-Fo',str(destination),*options],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(str(source)+'\n'+result.stdout+result.stderr)
        return destination.read_bytes()
    def check(source):
        relative=source.relative_to(ROOT/'shaders');ordinary=relative.with_name('rt_hit_direct.hlsl')
        old=compile_one(baseline/'shaders'/ordinary,out/'old'/ordinary.with_suffix('.spv'),baseline/'shaders')
        off=compile_one(ROOT/'shaders'/ordinary,out/'off'/ordinary.with_suffix('.spv'),ROOT/'shaders')
        if old!=off:raise RuntimeError('Ordinary direct SPIR-V changed: '+str(ordinary))
        destination=out/'on'/relative.with_suffix('.spv')
        feature=compile_one(source,destination,ROOT/'shaders')
        result=subprocess.run([str(validator),'--target-env','vulkan1.3',str(destination)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(str(source)+'\n'+result.stdout+result.stderr)
        return dict(file=relative.as_posix(),ordinary_sha256=hashlib.sha256(off).hexdigest(),feature_sha256=hashlib.sha256(feature).hexdigest())
    sources=sorted((ROOT/'shaders').rglob('rt_hit_two_bounce.hlsl'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:records=list(pool.map(check,sources))
    if any(hashlib.sha256(p.read_bytes()).hexdigest()!=digest for p,digest in frozen.items()):raise RuntimeError('Source changed during shader gate')
    result=dict(baseline=args.baseline,ordinary_identical=len(records),feature_validated=len(records),entries=records,game_launched=False,gpu_executed=False)
    (out/'shader-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(str(len(records))+' ordinary direct shaders byte-identical; '+str(len(records))+' two-surface variants compile/validate')
if __name__=='__main__':main()
