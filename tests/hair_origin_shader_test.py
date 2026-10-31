"""CPU-only physical-origin wrappers, strict off-SPIR-V and RF descriptor proof.

Reuses DXC/spirv-val and the frozen git archive approach of native_scalp_shader_test.
Two compilers maximum. No device, game, live inbox or deployment.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile
import zipfile

from shader_baseline import default_baseline

ROOT=Path(__file__).resolve().parents[1]


def bindings(raw):
    words=struct.unpack('<'+'I'*(len(raw)//4),raw);decorations={};i=5
    while i<len(words):
        count,op=words[i]>>16,words[i]&65535
        if not count:raise ValueError('Invalid SPIR-V instruction')
        if op==71 and count>=4 and words[i+2] in (33,34):
            decorations.setdefault(words[i+1],{})[words[i+2]]=words[i+3]
        i+=count
    return {(v[34],v[33]) for v in decorations.values() if 33 in v and 34 in v}


def main():
    ap=argparse.ArgumentParser(__doc__);ap.add_argument('--dxc',required=True);ap.add_argument('--sharc',required=True)
    ap.add_argument('--baseline',default=default_baseline(),help='Commit of the key-off reference (default: merge-base with main)');args=ap.parse_args()
    out=ROOT/'captures/hair-physical-origin/shaders';out.mkdir(parents=True,exist_ok=True)
    inputs=list((ROOT/'common').glob('*.h'))+list((ROOT/'shaders').rglob('*.hlsl'))+list((ROOT/'shaders').rglob('*.hlsli'))
    frozen={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    # DXC preserves relative include spelling; long archived prefixes can hit
    # Win32 MAX_PATH before resolving '..'. A private short TEMP extraction
    # keeps the exact frozen source intact; no junctions or source rewrites.
    archive=out/'baseline.zip';baseline=Path(tempfile.mkdtemp(prefix='hpo-'))
    subprocess.run(['git','archive','--format=zip','--output',str(archive),args.baseline,'shaders','common'],cwd=ROOT,check=True)
    with zipfile.ZipFile(archive) as z:z.extractall(baseline)
    validator=str(Path(args.dxc).with_name('spirv-val'+Path(args.dxc).suffix))
    def compile_one(includes,request,dest):
        # request: "<file>?NAME=VALUE;..." (common/shader_variant.h), the defines as -D.
        file,_,defs=request.partition('?');src=includes/file
        dest.parent.mkdir(parents=True,exist_ok=True);first=src.read_text().splitlines()[0]
        extra=shlex.split(first[len('// dxc:'):]) if first.startswith('// dxc:') else []
        command=[args.dxc,str(src),'-spirv','-fspv-target-env=vulkan1.3','-HV','2021','-T','cs_6_6','-E','main',
                 '-I',str(includes),'-I',args.sharc,'-D','BG3RTX=1','-D','BG3RTX_SHARC_SDK=1',
                 *[a for d in defs.split(';') if d for a in ('-D',d)],'-Fo',str(dest),*extra]
        result=subprocess.run(command,capture_output=True,text=True)
        if result.returncode:raise RuntimeError(f'{request}: {result.stdout}\n{result.stderr}')
        return dest.read_bytes()
    def check(wrapper):
        # The physical-origin request and the same request without it (the key-off path).
        # One output per request: with_suffix() would drop the hash and let the variants of
        # one file (shaders/variants.txt) overwrite each other across the two workers.
        target=wrapper.replace('RT_HAIR_PHYSICAL_ORIGIN=1;','');name=Path(wrapper.partition('?')[0]+'.'+hashlib.sha1(wrapper.encode()).hexdigest()[:12]+'.spv')
        old=compile_one(baseline/'shaders',target,out/'old'/name)
        off=compile_one(ROOT/'shaders',target,out/'off'/name)
        if old!=off:raise RuntimeError(f'Key-off SPIR-V changed: {target}')
        dest=out/'on'/name;on=compile_one(ROOT/'shaders',wrapper,dest)
        validation=subprocess.run([validator,'--target-env','vulkan1.3',str(dest)],capture_output=True,text=True)
        if validation.returncode:raise RuntimeError(f'{wrapper}: {validation.stdout}\n{validation.stderr}')
        on_bindings,off_bindings=bindings(on),bindings(off)
        if (0,50) not in on_bindings:raise RuntimeError(f'Feature image not used: {wrapper}')
        if (0,50) in off_bindings:raise RuntimeError(f'Feature image leaked into off path: {target}')
        rf=target.partition('?')[0].rsplit('/',1)[-1] in ('rt_reflections.hlsl','rt_reflections_sharc.hlsl')
        if rf and ((0,12) in on_bindings or (0,12) in off_bindings):
            raise RuntimeError(f'Cannot omit RF binding12: {target}')
        return dict(file=target,off_sha256=hashlib.sha256(off).hexdigest(),on_sha256=hashlib.sha256(on).hexdigest(),rf_binding12_unused=rf)
    # The old shaders/hair_origin/ wrappers: every Hair-origin variant without the tileset.
    wrappers=[l for l in (ROOT/'shaders/variants.txt').read_text().splitlines()
              if 'RT_HAIR_PHYSICAL_ORIGIN=1;' in l and 'RT_NATIVE_TILESET=1' not in l]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:records=list(pool.map(check,wrappers))
    if any(hashlib.sha256(Path(p).read_bytes()).hexdigest()!=sha for p,sha in frozen.items()):
        raise RuntimeError('Shader/common source changed during proof; rerun a frozen gate')
    result=dict(baseline=args.baseline,baseline_source_path=str(baseline),key_off_identical=len(records),feature_compiled_validated=len(records),
                rf_variants_binding12_unused=sum(r['rf_binding12_unused'] for r in records),entries=records)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(records)} key-off SPIR-V byte-identical; {len(records)} feature variants valid; {result["rf_variants_binding12_unused"]} RF variants omit binding12 safely')


if __name__=='__main__':main()
