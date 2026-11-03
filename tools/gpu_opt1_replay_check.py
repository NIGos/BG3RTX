"""Exact GPU output gate for gpu-opt1; windowless replay, never the game.

Uses the existing synthetic replay fixtures and production GI shader. Compiles only
through shader_identity, then specializes the same module OFF / each key / combined.
A second small shader calls production hit normal/position functions on transformed
pool geometry. --prepare-only writes fixtures without compiling or using the GPU.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import Cache, SourceTree, Toolchain, compile_requests, lower_priority, split_request
from tools.spec_cascade_check import opt
from tools.gpu_opt1_water_fixture import WATER_REQUEST, WATER_SHADER, WATER_OUTPUTS, water_fixture

OUTPUTS = ('giRaw.raw', 'giRawDepth.raw', 'rtstats.bin')
STATES = {'off': {1021: 0, 1022: 0, 1023: 0}, 'compact': {1021: 1, 1022: 0, 1023: 0},
          'alpha': {1021: 0, 1022: 1, 1023: 0}, 'water': {1021: 0, 1022: 0, 1023: 1},
          'combined': {1021: 1, 1022: 1, 1023: 1}}
GI_REQUEST = 'gi/gi_trace_rt_hit_direct.hlsl'
HIT_REQUEST = 'gpu_opt1_hit_fixture.hlsl'
HIT_SHADER = r'''
#define RT_HIT_MATERIAL 1
#include "gi/gi_common.hlsli"
#include "rt/rt_common.hlsli"
#include "rt/native_hit_material.hlsli"
#include "rt/native_hit_direct.hlsli"
[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID){
 uint2 id=tid.xy;if(any(id>=pc.half))return;
 float2 uv=(float2(gi_fp(id))+0.5)/float2(pc.full);
 float3 ray=ray_at(uv),dir=normalize(ray);RtCone cone={0.0,0.01};
 RtHit h=rt_closest_hit(0.0.xxx,dir,10.0,cone);
 if(h.t>=0.0 && !h.aborted){
  float3 n=rt_hit_normal(h),p=native_hit_position(h,dir*h.t);
  giRaw[id]=float4(n,h.t);giRawDepth[id]=dot(p,float3(0.25,0.5,0.125));
 }else{giRaw[id]=float4(0,0,0,h.aborted?-2:-1);giRawDepth[id]=0;}
}
'''


def run(args):
    p = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError(f"exit {p.returncode}: {' '.join(map(str,args))}\n{p.stdout[-6000:]}{p.stderr[-3000:]}")
    return p.stdout


def pool_fixture(src, dst):
    """Rigid binary-exact rotation/translation; pool normals and hit position use o2w."""
    shutil.copytree(src, dst, dirs_exist_ok=True)
    soup = bytearray((dst / 'soup.bin').read_bytes())
    # world = (local.y + 1/4, local.x - 1/2, -local.z + 1/8)
    for at in range(0, len(soup), 64):
        x, y, z = struct.unpack_from('<3f', soup, at)
        struct.pack_into('<3f', soup, at, y + .5, x - .25, .125 - z)
    (dst / 'pool.bin').write_bytes(soup)
    info = bytearray((dst / 'instinfo.bin').read_bytes())
    for at in range(0, len(info), 16):
        base, meta, rec, tris = struct.unpack_from('<4I', info, at)
        struct.pack_into('<4I', info, at, base, (meta & 255) | 256, 0, tris)
    (dst / 'instinfo.bin').write_bytes(info)
    instances = bytearray((dst / 'instances.bin').read_bytes())
    matrix = (0,1,0,.25, 1,0,0,-.5, 0,0,-1,.125)
    for at in range(0, len(instances), 64):
        struct.pack_into('<12f', instances, at, *matrix)
    (dst / 'instances.bin').write_bytes(instances)
    manifest = (dst / 'manifest.txt').read_text(encoding='utf-8')
    manifest = re.sub(r'^buffer 1 8 \d+.*$', f'buffer 1 8 {len(soup)} pool.bin', manifest, flags=re.M)
    (dst / 'manifest.txt').write_text(manifest, encoding='utf-8')


def alpha_fixture(src, dst, alpha):
    """Real plain alpha predicate: native UV/footprint, cutoff and stochastic policy."""
    shutil.copytree(src, dst, dirs_exist_ok=True)
    info = bytearray((dst/'instinfo.bin').read_bytes())
    base, meta, _, tris = struct.unpack_from('<4I',info)
    struct.pack_into('<4I',info,0,base,meta|1024,16,tris)
    (dst/'instinfo.bin').write_bytes(info)
    record=bytearray(224)
    struct.pack_into('<4I',record,0,1,64,3,0)
    struct.pack_into('<4I',record,16,0,tris,2,1)
    struct.pack_into('<2I2f',record,32,0,3,.5,.5)
    struct.pack_into('<2f',record,48,1,1)
    (dst/'alpharec.bin').write_bytes(record)
    (dst/'alpha.rgba32f').write_bytes(struct.pack('<4f',1,1,1,alpha))
    instances=bytearray((dst/'instances.bin').read_bytes())
    flags=struct.unpack_from('<I',instances,52)[0]
    struct.pack_into('<I',instances,52,flags & ~(4<<24))
    (dst/'instances.bin').write_bytes(instances)
    manifest=(dst/'manifest.txt').read_text(encoding='utf-8')
    manifest=re.sub(r'^buffer 1 0 \d+.*$', 'buffer 1 0 224 alpharec.bin',manifest,flags=re.M)
    manifest=re.sub(r'^(image 1 2 1 .* )page.rgba32f$',r'\1alpha.rgba32f',manifest,flags=re.M)
    (dst/'manifest.txt').write_text(manifest,encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/gpu_opt1_replay')
    ap.add_argument('--replay-exe', type=Path, default=ROOT / 'bin/frame_replay.exe')
    ap.add_argument('--pack', action='append', type=Path, default=[])
    ap.add_argument('--prepare-only', action='store_true')
    ap.add_argument('--jobs', type=int, default=1)
    a = ap.parse_args()
    lower_priority()
    a.out.mkdir(parents=True, exist_ok=True)
    fixtures = a.out / 'fixtures'
    for name, flags in [('lit', []), ('dark', ['--dark'])]:
        print(run([a.replay_exe, '--synthesize', fixtures / name, *flags]), flush=True)
    pool_fixture(fixtures / 'lit', fixtures / 'pool')
    alpha_fixture(fixtures/'lit',fixtures/'alpha-solid',1.0)
    alpha_fixture(fixtures/'lit',fixtures/'alpha-clear',.25)
    packs = [('lit', fixtures / 'lit', GI_REQUEST), ('dark', fixtures / 'dark', GI_REQUEST),
             ('pool', fixtures / 'pool', HIT_REQUEST),
             ('alpha-solid',fixtures/'alpha-solid',GI_REQUEST),
             ('alpha-clear',fixtures/'alpha-clear',GI_REQUEST)]
    for kind in ('empty','mixed','mixed-debug','invalid'):
        dst=water_fixture(fixtures/'lit',fixtures/('water-'+kind),kind)
        packs.append(('water-'+kind,dst,WATER_REQUEST))
    packs += [(f'pack{i}', p.resolve(), GI_REQUEST) for i, p in enumerate(a.pack)]
    if a.prepare_only:
        print(f'prepared {len(packs)} fixtures; no compile or GPU dispatch', flush=True)
        return 0
    tool = Toolchain()
    base = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    files = {p: base.read(p) for p in base.ids}
    files['shaders/' + HIT_REQUEST] = HIT_SHADER.encode()
    files['shaders/' + split_request(WATER_REQUEST)[0]] = WATER_SHADER.encode()
    tree = SourceTree.memory(files, label='gpu-opt1 fixtures').with_sharc(tool.sharc)
    try:
        requests = sorted({r for _, _, r in packs})
        compiled = compile_requests(tree, requests, tool=tool, cache=Cache(), jobs=a.jobs, validate=True,
                                    label='gpu-opt1 replay')
        for r in requests:
            if not compiled[r].ok or compiled[r].valid is False:
                raise RuntimeError(f'compile {r}: {compiled[r].log}')
        modules = {}
        for r in requests:
            for state, assignment in STATES.items():
                dst = a.out / (Path(split_request(r)[0]).stem + '-' + state + '.spv')
                assigned = opt(tool, compiled[r].path(), dst,
                               '--set-spec-const-default-value=' + ' '.join(f'{k}:{v}' for k,v in assignment.items()))
                frozen = dst.with_suffix('.frozen.spv')
                opt(tool, assigned, frozen, '--freeze-spec-const', '--fold-spec-const-op-composite', '-O')
                modules[r,state] = frozen
        report = {}
        for name, pack, request in packs:
            sums = {}
            outputs=WATER_OUTPUTS if request==WATER_REQUEST else OUTPUTS
            for state in STATES:
                out = a.out / 'outputs' / name / state
                print(f'[gpu-opt1 replay] {name}/{state}', flush=True)
                log = run([a.replay_exe, '--pack', pack, '--spv', modules[request,state], '--out', out])
                (out / 'replay.log').write_text(log,encoding='utf-8')
                sums[state] = {f: hashlib.sha256((out/f).read_bytes()).hexdigest() for f in outputs}
            reference=a.out/'outputs'/name/'off'
            participation={}
            if request==WATER_REQUEST:
                words=struct.unpack('<128I',(reference/'waterStats.bin').read_bytes())
                if words[0]!=1:
                    raise RuntimeError(f'{name}: compose not dispatched exactly once')
                if name=='water-empty':
                    native=struct.pack('<I',(15<<6)|((15<<6|32)<<11)|((16<<5)<<22))*17*13
                    if (reference/'waterTarget.raw').read_bytes()!=native or any(words[1:]):
                        raise RuntimeError('water-empty: native colour or empty counters changed')
                else:
                    if not words[1] or not words[2]:
                        raise RuntimeError(f'{name}: no owned water records; INVALID')
                    if name=='water-invalid' and not words[4]:
                        raise RuntimeError('water-invalid: no invalid owned records; INVALID')
                    if name!='water-invalid' and not words[7]:
                        raise RuntimeError(f'{name}: no reflection rays; INVALID')
                participation={'dispatches':words[0],'records':words[1],'owned':words[2],
                               'invalid':words[4],'rays':words[7]}
            else:
                stats=(reference/'rtstats.bin').read_bytes()
                def counter(i):return sum(struct.unpack_from('<64I',stats,i*64*4))
                if name.startswith('alpha-'):
                    candidates=counter(80)
                    if not candidates:
                        raise RuntimeError(f'{name}: no alpha candidates; INVALID')
                    participation['alpha_candidates']=candidates
                if name in ('lit','dark'):
                    if not counter(282) or not counter(283):
                        raise RuntimeError(f'{name}: no qualified hidden hits; INVALID')
                    if (counter(285)>0)!=(name=='lit'):
                        raise RuntimeError(f'{name}: unexpected positive-light participation')
                    participation={'material_lookups':counter(282),'qualified':counter(283),'lit':counter(285)}
                if name=='pool':
                    data=(reference/'giRaw.raw').read_bytes()
                    rgba=list(struct.iter_unpack('<4e',data))
                    hits=sum(1 for x,y,z,t in rgba if t>0 and x*x+y*y+z*z>0)
                    if hits==0 or not all(all(math.isfinite(v) for v in row) for row in rgba):
                        raise RuntimeError('pool: no finite transformed normals/hits; INVALID')
                    participation['transformed_pool_hits']=hits
            print(f'[gpu-opt1 replay] {name} participation: {participation}',flush=True)
            exact = {s: sums[s] == sums['off'] for s in STATES if s != 'off'}
            report[name] = {'exact':exact, 'participation':participation, 'sha256':sums}
            print(f'[gpu-opt1 replay] {name}: {exact}', flush=True)
        (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        return 0 if all(all(v['exact'].values()) for v in report.values()) else 1
    finally:
        tree.cleanup()


if __name__ == '__main__':
    sys.exit(main())
