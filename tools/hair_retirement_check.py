"""Selective rejected-feature retirement and C2 default equivalence, preserving other specializations.

Uses the retirement gate's canonical comparator, never a blanket spec-constant freeze.
Callers must keep the same request (including arithmetic reference defines) on both sides.
"""
import re
import subprocess
import tempfile
from pathlib import Path

from tools.shader_identity import Progress, compile_requests, identical
from tools.spec_cascade_check import opt, canonical, differ


def defaults_off(tool, src, dst, ids):
    asm = subprocess.run([str(tool.dxc.with_name('spirv-dis.exe')), '--raw-id', '--no-header', str(src)],
                         capture_output=True, text=True, check=True).stdout
    retired = set(re.findall(r'OpDecorate\s+(%\d+)\s+SpecId\s+(?:' + '|'.join(str(i) for i in ids) + r')\b', asm))
    lines = []
    for line in asm.splitlines():
        if any(re.search(r'OpDecorate\s+' + re.escape(i) + r'\s+SpecId\b', line) for i in retired):
            continue
        match = re.match(r'\s*(%\d+) = OpSpecConstant\s+(%\d+)\s+0\s*$', line)
        if match and match[1] in retired:
            line = line.replace('OpSpecConstant', 'OpConstant', 1)
        elif any(re.match(r'\s*' + re.escape(i) + r'\s*=', line) for i in retired):
            raise RuntimeError('retired constant has unexpected type/default: ' + line)
        lines.append(line)
    source = dst.with_suffix('.spvasm')
    source.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    subprocess.run([str(tool.dxc.with_name('spirv-as.exe')), '--target-env', 'vulkan1.3', str(source), '-o', str(dst)],
                   capture_output=True, text=True, check=True)
    return dst


def retired_off(tool, src, dst):
    return defaults_off(tool, src, dst, (1001, 1002, 1005, 1006, 1016, 1017, 1018, 1019, 1021, 1022, 1023, 1024))


def shadow_off(tool, src, dst):
    # Only C2 (1026) and M2.3 (1044) are fixed; every other surviving specialization remains variable.
    return defaults_off(tool, src, dst, (1026, 1044))


def compare_retired(tool, baseline, current, tmp):
    """Match every validated entry with retired baseline IDs and C2 default only fixed."""
    if not baseline.ok or not baseline.valid or not current.ok or not current.valid:
        return 'retirement comparison requires compiled, validated modules'
    names = [e.name for e in baseline.entries]
    if not names or set(names) != {e.name for e in current.entries}:
        return 'retirement module entry mismatch'
    for i, entry in enumerate(names):
        a, b = tmp / f'{i}_base.spv', tmp / f'{i}_new.spv'
        a.write_bytes(baseline.blob(entry))
        b.write_bytes(current.blob(entry))
        a = retired_off(tool, a, tmp / f'{i}_retired.spv')
        a = shadow_off(tool, a, tmp / f'{i}_base_shadow.spv')
        b = shadow_off(tool, b, tmp / f'{i}_new_shadow.spv')
        # Retired empty-water voting leaves an unused GroupNonUniformVote declaration.
        # Trim unused capabilities symmetrically, as check_one does; executable instructions
        # and every surviving specialization remain part of the exact comparison.
        a = opt(tool, a, tmp / f'{i}_base_opt.spv', '--fold-spec-const-op-composite',
                '-O', '--remove-unused-interface-variables', '--trim-capabilities')
        b = opt(tool, b, tmp / f'{i}_new_opt.spv', '--fold-spec-const-op-composite',
                '-O', '--remove-unused-interface-variables', '--trim-capabilities')
        delta = differ(canonical(tool, a), canonical(tool, b))
        if delta:
            return f'{entry}: {delta}'
    return ''


def retirement_fallback(rows, base, new, tool, cache, label):
    """Return per-request proof paths; only raw DIFFERENT may use selective equivalence."""
    changed = [r for r, state, _ in rows if state == 'DIFFERENT']
    compiled = [compile_requests(tree, changed, tool=tool, cache=cache, jobs=1,
                                 validate=True, label=label + ' retirement ' + tree.label)
                for tree in (base, new)] if changed else []
    proofs = []
    with Progress(label + ' proof paths', len(rows), every=30, verb='checked') as progress:
        for request, state, log in rows:
            path = 'raw identity' if identical(state) else 'failed'
            if state == 'DIFFERENT':
                with tempfile.TemporaryDirectory(prefix='b3-feature-retire-') as tmp:
                    log = compare_retired(tool, compiled[0][request], compiled[1][request], Path(tmp))
                if not log:
                    path = 'retirement/default equivalence (1001/1002/1005/1006/1016-1019/1021-1024=0 baseline; 1026/1044=0 both)'
            print(f'{label}: {path}: {request}' + (f' ({state}: {log})' if path == 'failed' else ''), flush=True)
            proofs.append({'request': request, 'path': path, 'raw_state': state, 'error': log})
            progress.step(failed=path == 'failed')
    return proofs
