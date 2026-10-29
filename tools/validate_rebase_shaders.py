"""Prove ordinary SPIR-V unchanged and validate every default-off rebase variant."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path('C:/VulkanSDK/1.4.357.0/Bin')


def compile_shader(source, output):
    first = source.read_text().splitlines()[0]
    extra = first.removeprefix('// dxc:').split() if first.startswith('// dxc:') else []
    include = ROOT/'external/sharc-1.8.3/include'
    if include.exists():
        extra += ['-I', str(include), '-D', 'BG3RTX_SHARC_SDK=1']
    subprocess.run([str(SDK/'dxc.exe'),str(source),'-spirv','-fspv-target-env=vulkan1.3',
                    '-HV','2021','-T','cs_6_6','-E','main','-I',str(ROOT/'shaders'),
                    '-D','BG3RTX=1','-O3',*extra,'-Fo',str(output)],check=True,capture_output=True,text=True)
    return hashlib.sha256(output.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--baseline',type=Path,default=ROOT/'captures/p1-pan-flashes/rebase-spv-before')
    ap.add_argument('--out',type=Path,default=ROOT/'captures/p1-pan-flashes/rebase-shader-check.json')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='bg3-rebase-check-') as tmp:
        def ordinary(baseline):
            target = Path(tmp)/baseline.name
            current = compile_shader(ROOT/'shaders/gi'/f'{baseline.stem}.hlsl',target)
            original = hashlib.sha256(baseline.read_bytes()).hexdigest()
            return {'shader':baseline.stem,'sha256':current,'byte_identical':current==original}
        def variant(source):
            target = Path(tmp)/f'rebase-{source.stem}.spv'
            digest = compile_shader(source,target)
            result = subprocess.run([str(SDK/'spirv-val.exe'),'--target-env','vulkan1.3',
                                     '--scalar-block-layout',str(target)],capture_output=True,text=True)
            return {'shader':source.stem,'sha256':digest,'valid':result.returncode==0,
                    'diagnostics':result.stdout+result.stderr}
        with ThreadPoolExecutor(8) as pool:
            normals = list(pool.map(ordinary,sorted(args.baseline.glob('*.spv'))))
            variants = list(pool.map(variant,sorted((ROOT/'shaders/gi/rebase').glob('*.hlsl'))))
    ok = len(normals)==21 and all(x['byte_identical'] for x in normals) and len(variants)==20 and all(x['valid'] for x in variants)
    report = {'pass':ok,'ordinary':normals,'variants':variants}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2))
    print(f'{sum(x["byte_identical"] for x in normals)}/{len(normals)} ordinary shaders byte-identical; '
          f'{sum(x["valid"] for x in variants)}/{len(variants)} rebase variants SPIR-V-valid')
    for item in variants:
        if not item['valid']: print(item['shader'],item['diagnostics'])
    raise SystemExit(0 if ok else 1)


if __name__ == '__main__': main()
