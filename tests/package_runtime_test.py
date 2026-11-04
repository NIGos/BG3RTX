"""Check runtime source dependencies against the real package plan; never run payloads."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLAN = None
# Generated output, user configuration and developer-only directories are not inputs.
EXCLUDED = {'bg3rtx_user.ini', 'trace_terrain_packet.request', 'trace', 'bg3rtx',
            'shaders', 'common', 'captures', 'sl_logs', 'bin/hot', 'bin/hot/',
            'bin/GFSDK_Aftermath_Lib.x64.dll'}
PREFIXES = ('captures/', 'external/', '../../../external/')
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*', re.S)


def code_only(text):
    return TOKEN.sub(lambda m: m[0] if m[0].startswith('"') else '', text)


def direct_paths(code):
    """Collect root/bin paths before classifying them, including unknown extensions."""
    paths = set()
    for match in re.finditer(r'\b(root_dir|bin_dir)\s*\(\s*\)\s*\+\s*"([^"\n]+)"', code):
        path = match[2].replace('\\\\', '/').lstrip('/')
        if match[1] == 'bin_dir':
            path = 'bin/' + path
        if path not in EXCLUDED and not path.startswith(PREFIXES):
            paths.add(path)
    return paths


def runtime_files():
    required = set()
    for folder in ('layer', 'core'):
        for source in (ROOT / folder).rglob('*'):
            if source.suffix not in ('.cpp', '.h', '.inl'):
                continue
            code = code_only(source.read_text(encoding='utf-8'))
            required.update(direct_paths(code))
            # Streamline selects bin/sl (production) or bin/sl_dev at runtime,
            # then loads from that directory rather than directly from bin_dir().
            if source.name == 'streamline.cpp':
                for dll in re.findall(r'"(?:\\\\)?(sl\.[A-Za-z0-9_.-]+\.dll)"', code):
                    required.add('bin/sl/' + dll)
            # Shader requests can be passed indirectly through helper functions.
            for request in re.findall(r'"([A-Za-z0-9_./-]+\.hlsl)(?:\?[^"\n]*)?"', code):
                required.add('shaders/' + request)
    # Include dependencies are runtime inputs too, not C++ build dependencies.
    pending = [p for p in required if p.endswith('.hlsl')]
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        source = ROOT / name
        if not source.is_file():
            raise AssertionError(f'Runtime source missing: {name}')
        for delimiter, include in re.findall(r'^\s*#\s*include\s*([<"])([^">]+)[">]',
                                             source.read_text(encoding='utf-8'), re.M):
            candidates = [source.parent / include, ROOT / 'shaders' / include]
            target = next((p.resolve() for p in candidates if p.is_file()), None)
            if target is None:
                # Optional SHaRC is excluded. Shared headers also contain C++-only
                # angle system includes; quoted missing inputs must always fail.
                if include.startswith('Sharc') or delimiter == '<':
                    continue
                raise AssertionError(f'Unresolved runtime include: {name}: {include}')
            path = target.relative_to(ROOT).as_posix()
            required.add(path)
            pending.append(path)
    return required


def check_plan(plan, required):
    entries = json.loads(Path(plan).read_text(encoding='utf-8-sig'))
    destinations = {e['Destination'].replace('\\', '/') for e in entries}
    missing = sorted(required - destinations)
    if missing:
        raise AssertionError('Runtime inputs absent from package plan: ' + ', '.join(missing))
    return entries


class PackageRuntimeTest(unittest.TestCase):
    def test_source_runtime_inputs_in_package_plan(self):
        required = runtime_files()
        self.assertTrue({'shaders/rt/alpha_fs.txt', 'shaders/rt/camera_fade_fs.txt',
                         'bg3rtx.ini', 'data/assetdb/bg3rtx_assetdb.bin'} <= required)
        # A future data type must be picked up without extending a suffix whitelist.
        self.assertEqual(direct_paths(r'root_dir() + "\\data\\new.runtime";'),
                         {'data/new.runtime'})
        (ROOT / '.scratch').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='pkg-runtime-', dir=ROOT / '.scratch') as temp:
            plan = PLAN
            if plan is None:
                ps = shutil.which('pwsh') or shutil.which('powershell')
                self.assertIsNotNone(ps, 'PowerShell required for the real package plan')
                result = subprocess.run([ps, '-NoProfile', '-NonInteractive', '-ExecutionPolicy',
                                         'Bypass', '-File', str(ROOT / 'scripts/package.ps1'),
                                         '-DryRun', '-Version', 'runtime-test', '-OutDir', temp],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                plan = Path(temp) / 'BG3RTX-runtime-test/payload-list.json'
            entries = check_plan(plan, required)
            # Prove the original omission is rejected even when files.txt agrees with it.
            broken = Path(temp) / 'missing-alpha.json'
            for table in ('alpha_fs.txt', 'camera_fade_fs.txt'):
                broken.write_text(json.dumps([e for e in entries if e['Destination'].replace('\\', '/')
                                             != 'shaders/rt/' + table]), encoding='utf-8')
                with self.assertRaisesRegex(AssertionError, table):
                    check_plan(broken, required)
        print(f'Runtime package guard: {len(required)} source dependencies covered', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--plan', type=Path, help='Existing package payload-list.json (verify_rc)')
    args, remaining = parser.parse_known_args()
    PLAN = args.plan
    unittest.main(argv=[__file__, *remaining], verbosity=2)
