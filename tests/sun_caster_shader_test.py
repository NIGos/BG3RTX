"""Offline DXC gate: every ordinary entry identical, current-sun entries valid."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--baseline', default='92183e7')
    parser.add_argument('--dxc', required=True)
    parser.add_argument('--spirv-val', required=True)
    parser.add_argument('--sharc', required=True)
    args = parser.parse_args()
    out = ROOT / 'captures/sun-coverage-20261030/shader-check'
    out.mkdir(parents=True, exist_ok=True)
    archive = out / 'baseline.zip'
    subprocess.run(['git', 'archive', '--format=zip', '--output', str(archive), args.baseline,
                    'shaders', 'common'], cwd=ROOT, check=True)
    # Deep wrapper chains exceed Windows MAX_PATH in the retained evidence tree.
    # Keep the exact archived baseline source in a short, owned scratch directory.
    baseline_temp = tempfile.TemporaryDirectory(prefix='b3-')
    try:
        old = Path(baseline_temp.name)
        assert old.resolve().parent == Path(tempfile.gettempdir()).resolve()
        with zipfile.ZipFile(archive) as contents:
            contents.extractall(old)
    
        def compile_one(source, destination, include, entry):
            destination.parent.mkdir(parents=True, exist_ok=True)
            first = source.read_text(encoding='utf-8').splitlines()[0]
            extra = shlex.split(first[len('// dxc:'):]) if first.startswith('// dxc:') else []
            command = [args.dxc, str(source), '-spirv', '-fspv-target-env=vulkan1.3', '-HV', '2021',
                       '-T', 'cs_6_6', '-E', entry, '-I', str(include), '-I', args.sharc,
                       '-D', 'BG3RTX=1', '-D', 'BG3RTX_SHARC_SDK=1', '-O3', '-Fo', str(destination), *extra]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f'{source}/{entry}: {result.stdout}\n{result.stderr}')
            return hashlib.sha256(destination.read_bytes()).hexdigest()
    
        def check(item):
            relative, entry, ordinary = item
            source = ROOT / 'shaders' / relative
            dest = out / 'current' / (relative.as_posix() + '.' + entry + '.spv')
            current = compile_one(source, dest, ROOT / 'shaders', entry)
            if ordinary:
                before = compile_one(old / 'shaders' / relative,
                                     out / 'old' / (relative.as_posix() + '.' + entry + '.spv'),
                                     old / 'shaders', entry)
                if before != current:
                    raise RuntimeError(f'Ordinary byte identity failed: {relative}/{entry}')
            result = subprocess.run([args.spirv_val, '--target-env', 'vulkan1.3', str(dest)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f'{relative}/{entry}: {result.stdout}\n{result.stderr}')
            return {'shader': relative.as_posix(), 'entry': entry, 'ordinary': ordinary, 'sha256': current}
    
        items = []
        for path in sorted((ROOT / 'shaders').rglob('*.hlsl')):
            relative = path.relative_to(ROOT / 'shaders')
            entries = re.findall(r'\[numthreads\([^)]*\)\]\s*void\s+(\w+)', path.read_text(encoding='utf-8')) or ['main']
            ordinary = (old / 'shaders' / relative).exists()
            for entry in entries:
                items.append((relative, entry, ordinary))
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            records = list(pool.map(check, items))
        result = {'baseline': args.baseline, 'ordinary_identical': sum(r['ordinary'] for r in records),
                  'new_compiled_validated': sum(not r['ordinary'] for r in records), 'entries': records}
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f"{result['ordinary_identical']} ordinary entries byte-identical; "
              f"{result['new_compiled_validated']} new entries compiled/validated")
    finally:
        baseline_temp.cleanup()


if __name__ == '__main__':
    main()
