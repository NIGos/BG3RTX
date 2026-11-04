"""Conservative merge-green path scopes and build-free tooling checks.

The classifier reads Git only. Renames include both paths so moving native code
under docs cannot hide a native deletion. Empty/unknown input always means full.
"""
import os
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
import sys

TOOL_TESTS = ('batch_jobs_lint', 'live_flow', 'queue_gates', 'live_guard',
              'fold_progress', 'merge_scope')
CAMERA_TESTS = ('camera_plan_test.ps1', 'camera_plan_fields_test.ps1')


def tooling_tests(root, paths):
    """Shared queue checks plus affected registered build-free Python CTests."""
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8-sig')
    registry = re.search(r'foreach\(py_test\s+([^)]*)\)', cmake).group(1).split()
    selected = set(TOOL_TESTS)
    tokens = {Path(p).stem.replace('-', '_') for p in paths}
    for name in registry:
        # These registered tests need DXC or the MSVC build environment. Tooling-only
        # edits lint their source; the shader and full scopes run them through CTest.
        if name in ('water_surface_shader', 'core_resource_init', 'layer_init_failure'):
            continue
        path = root / f'tests/{name}_test.py'
        text = path.read_text(encoding='utf-8-sig')
        if (f'tests/{name}_test.py' in paths or
                any(re.search(r'\b' + re.escape(token) + r'\b', text) for token in tokens)):
            selected.add(name)
    return sorted(selected)


def shader_test_regex(root):
    """Conservative source consumers, including transitive local C++ includes.

    Shader compilation also checks the built host descriptor interfaces. Keep
    shader/cache/variant contract tests even when they use generated inputs.
    """
    seen = {}

    def consumes(path, visiting=None):
        path = path.resolve()
        if path in seen:
            return seen[path]
        visiting = (visiting or set()) | {path}
        text = path.read_text(encoding='utf-8-sig', errors='replace')
        hit = bool(re.search(r'shaders[/\\]|shader_variant|\.hlsl|\.spv|spirv|variants\.txt', text))
        for include in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.M):
            target = path.parent / include
            if target.is_file() and target.resolve() not in visiting:
                hit = consumes(target, visiting) or hit
        seen[path] = hit
        return hit

    names = {'native_hit_gpu_fixture', 'frame_replay_test', 'pt_reference_test'}
    for path in (root / 'tests').glob('*_test.*'):
        if path.suffix not in ('.py', '.cpp'):
            continue
        if consumes(path) or re.search(r'shader|variant|bindings|spirv', path.stem):
            names.add(path.stem)
    return '^(' + '|'.join(re.escape(n) for n in sorted(names)) + ')$'


def classify(paths):
    scopes = []
    for path in paths:
        # Git emits repository-relative slash paths. Reject malformed input.
        if not path or path.startswith('/') or '..' in path.split('/') or '\\' in path:
            return dict(scope='full', reason=f'unclassified path: {path!r}')
        if path.startswith('docs/') or path.endswith('.md'):
            scope = 'docs'
        elif path.startswith('shaders/') or path == 'common/shader_variant.h':
            scope = 'shaders'
        elif (path.startswith('tools/live_batches/') or
              re.fullmatch(r'scripts/[^/]+\.(py|ps1)', path) or
              re.fullmatch(r'tests/[^/]+\.py', path)):
            scope = 'tooling'
        else:
            return dict(scope='full', reason=f'native/build or unclassified path: {path}')
        scopes.append(scope)
    if not scopes:
        return dict(scope='full', reason='empty change list (safe default)')
    scope = max(scopes, key=('docs', 'tooling', 'shaders').index)
    return dict(scope=scope, tooling='tooling' in scopes, reason=f'{len(paths)} changed paths; broadest required scope is {scope}')


def changed_paths(root, ref):
    result = subprocess.run(['git', 'diff', '--name-only', '--no-renames', '-z',
                             f'main...{ref}', '--'], cwd=root, check=True,
                            stdout=subprocess.PIPE)
    return [p.decode('utf-8') for p in result.stdout.split(b'\0') if p]


def tooling_checks(root, paths):
    # Syntax checks never import a job generator or execute a changed script.
    for path in paths:
        source = root / path
        if not source.is_file():  # deletions are covered by the regression suite
            continue
        if source.suffix == '.py':
            ast.parse(source.read_text(encoding='utf-8-sig'), filename=path)
        elif source.suffix == '.json':
            json.loads(source.read_text(encoding='utf-8-sig'))
        elif source.suffix == '.ps1':
            code = ('$e=$null; [System.Management.Automation.Language.Parser]::ParseFile('
                    '$args[0],[ref]$null,[ref]$e) | Out-Null; '
                    'if($e){$e | Out-String | Write-Host; exit 1}')
            # -File wrapper avoids PowerShell -Command argument quoting ambiguities.
            import tempfile
            with tempfile.TemporaryDirectory() as tmp:
                wrapper = Path(tmp) / 'parse.ps1'
                wrapper.write_text(code, encoding='utf-8')
                subprocess.run(['powershell', '-NoProfile', '-File', str(wrapper),
                                str(source)], check=True, cwd=root)
    changed = {root / p for p in paths}
    jobs = {p for p in changed if p.is_file() and p.suffix == '.json' and p.parent.name == 'jobs'}
    for plan in changed:
        if plan.parent.name == 'plans':
            # Plans are shared by sibling jobs; validate each actual consumer.
            for job in (plan.parent.parent / 'jobs').glob('*.json'):
                data = json.loads(job.read_text(encoding='utf-8-sig'))
                args = data.get('args', [])
                if '-MovePlan' in args and Path(args[args.index('-MovePlan') + 1]).name == plan.name:
                    jobs.add(job)
    # Only runnable jobs are linted: active in the lint inventory, or new on this branch.
    # Historical jobs are evidence, never launched again; editing them (e.g. when a rejected
    # key is removed) must not re-lint them under newer rules.
    inventory = root / 'tools/live_batches/duration_lint.json'
    if jobs and inventory.is_file():
        entries = json.loads(inventory.read_text(encoding='utf-8-sig'))
        key = lambda p: os.path.normcase(os.path.normpath(str(p)))
        active = {key(root / Path(k.replace('\\', '/'))) for k, v in entries.items() if v.get('active')}

        def on_main(job):
            rel = job.relative_to(root).as_posix()
            return subprocess.run(['git', 'cat-file', '-e', f'main:{rel}'], cwd=root,
                                  stderr=subprocess.DEVNULL).returncode == 0

        historical = {j for j in jobs if key(j) not in active and on_main(j)}
        for job in sorted(historical):
            print(f'historical job, not linted: {job.relative_to(root)}', flush=True)
        jobs -= historical
    if jobs:
        sys.path.insert(0, str(root))
        from tools.live_batches import live_flow as flow, lint_snapshot
        index = lint_snapshot.load()
        for path in sorted(jobs):
            print(f'changed job lint: {path.relative_to(root)}', flush=True)
            job, plan, _ = flow.load_job(path)
            patterns = set(job.get('forbid', []) + job.get('expect', []))
            missing = patterns - index['matches'].keys()
            if missing:
                raise ValueError(f'{path}: refresh frozen lint evidence for {sorted(missing)}')
            result = flow.lint(job, plan, [], index['matches'])
            problems = result['problems'] + flow.launch_problems(job, result['duration'])
            if problems:
                raise ValueError(f'{path}: {problems}')
    # This shared suite exercises the queue and active job corpus, including
    # transitive imports. Run the CTest source commands directly: no stale build
    # registry, configure, C++ executable, DXC, or live QA invocation is needed.
    for name in tooling_tests(root, paths):
        print(f'tooling CTest: {name}_test', flush=True)
        subprocess.run([sys.executable, str(root / f'tests/{name}_test.py')],
                       cwd=root, check=True)
    for name in CAMERA_TESTS:
        print(f'tooling check: {name}', flush=True)
        subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                        '-File', str(root / 'tests' / name)], cwd=root, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', required=True)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--run', choices=('docs', 'tooling'))
    parser.add_argument('--shader-tests', action='store_true')
    args = parser.parse_args()
    if args.shader_tests:
        print(shader_test_regex(args.root))
        return
    paths = changed_paths(args.root, args.ref)
    if args.run == 'tooling':
        tooling_checks(args.root, paths)
    elif args.run == 'docs':
        print('docs: no repository-wide Markdown/link checker is registered; no build required', flush=True)
    else:
        print(json.dumps(classify(paths)))


if __name__ == '__main__':
    main()
