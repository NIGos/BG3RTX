"""Exercise the real merge gate against disposable Git repositories, no game/build."""
import os
from pathlib import Path
import subprocess
import tempfile

gate = Path(__file__).resolve().parents[1] / 'scripts' / 'merge-green.ps1'
scratch = gate.parents[1] / 'build'
scratch.mkdir(exist_ok=True)
env = dict(os.environ, GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='core.hooksPath',
           GIT_CONFIG_VALUE_0=str(gate.parent / 'no-hooks'))

def run(args, cwd, check=True):
    return subprocess.run(args, cwd=cwd, env=env, text=True, capture_output=True,
                          check=check)

def git(root, *args):
    return run(['git', *args], root).stdout.strip()

def call(root, ref):
    return run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                str(gate), '-Ref', ref, '-MainRoot', str(root)], root, False)

def write(root, path, text):
    dest = root / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text)

for mode in ('invalid-ref', 'build-fail', 'shader-fail', 'qa-fail', 'success'):
    with tempfile.TemporaryDirectory(prefix='merge-gate-', dir=scratch) as tmp:
        root = Path(tmp)
        git(root, 'init', '-q', '-b', 'main')
        git(root, 'config', 'user.name', 'Merge gate fixture')
        git(root, 'config', 'user.email', 'fixture@example.invalid')
        write(root, 'base.txt', 'base\n')
        git(root, 'add', '.')
        git(root, 'commit', '-qm', 'base')
        before = git(root, 'rev-parse', 'HEAD')
        git(root, 'switch', '-qc', 'feature')
        write(root, 'scripts/build.ps1', 'param([switch]$Test)\nexit ' + ('1' if mode == 'build-fail' else '0'))
        write(root, 'tools/compile_shaders.py', 'raise SystemExit(' + ('1' if mode == 'shader-fail' else '0') + ')\n')
        write(root, 'tests/qa_detectors_test.py', 'raise SystemExit(' + ('1' if mode == 'qa-fail' else '0') + ')\n')
        write(root, 'scripts/test-tree.ps1', "Set-Content -LiteralPath 'refreshed.txt' -Value done\n")
        git(root, 'add', '.')
        git(root, 'commit', '-qm', 'feature fixture')
        git(root, 'switch', '-q', 'main')
        result = call(root, 'no-such-ref' if mode == 'invalid-ref' else 'feature')
        merged = git(root, 'rev-parse', 'HEAD') != before
        assert result.returncode == (0 if mode == 'success' else 1), (mode, result.stdout, result.stderr)
        assert merged == (mode == 'success'), (mode, result.stdout, result.stderr)
        assert not (root / '.git/MERGE_HEAD').exists(), mode
        assert not git(root, 'status', '--porcelain', '--untracked-files=no'), mode
        assert (root / 'refreshed.txt').exists() == (mode == 'success'), mode
        if mode == 'success':
            assert len(git(root, 'show', '-s', '--format=%P').split()) == 2
            again = call(root, 'feature')
            assert again.returncode == 0 and 'ALREADY MERGED' in again.stdout, (again.stdout, again.stderr)
        print(mode, 'PASS')
