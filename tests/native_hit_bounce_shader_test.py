"""Affected reference variants only: old direct SPIR-V identity + two-surface validation.

For gi/rt_hit_two_bounce.hlsl and each of its variant requests: the ordinary rt_hit_direct
request of the same flags is byte-identical to the baseline, and the two-bounce request compiles
and passes spirv-val. CPU compiler tools only (tools/shader_identity.py, shared cache).
No Vulkan device/game/queue calls. Result: captures/opaque-two-bounce/shader-result.json.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (compile_requests, default_baseline, default_jobs, gate_setup, identical,  # noqa: E402
                                   identity, unchanged_since)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dxc', type=Path, required=True)
    parser.add_argument('--baseline', default=default_baseline(), help='Commit of the key-off reference (default: merge-base with main)')
    parser.add_argument('--jobs', type=int, default=default_jobs())
    parser.add_argument('--no-cache', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'captures/opaque-two-bounce'
    out.mkdir(parents=True, exist_ok=True)
    tool, cache, base, new = gate_setup(args.baseline, dxc=args.dxc, no_cache=args.no_cache)
    # The ordinary two-bounce entry and every variant request of it (shaders/variants.txt).
    sources = ['gi/rt_hit_two_bounce.hlsl'] + [l for l in new.requests() if l.startswith('gi/rt_hit_two_bounce.hlsl?')]
    ordinary = {s: s.replace('rt_hit_two_bounce.hlsl', 'rt_hit_direct.hlsl') for s in sources}
    try:
        same = {r: (s, log) for r, s, log in identity(base, new, list(ordinary.values()), tool, cache, args.jobs, 'two-bounce key-off')}
        on = compile_requests(new, sources, tool=tool, cache=cache, jobs=args.jobs, label='two-bounce key-on', validate=True)
        off = compile_requests(new, list(ordinary.values()), tool=tool, cache=cache, jobs=args.jobs, label='two-bounce ordinary')
    finally:
        base.cleanup()
    records = []
    for s in sources:
        state, log = same[ordinary[s]]
        if not identical(state):
            raise RuntimeError(f'Ordinary direct SPIR-V changed ({state}): {ordinary[s]}\n{log}')
        if not on[s].ok or not on[s].valid:
            raise RuntimeError(s + '\n' + on[s].log)
        records.append(dict(file=s, ordinary_sha256=off[ordinary[s]].entries[0].sha, feature_sha256=on[s].entries[0].sha))
    unchanged_since(new)
    result = dict(baseline=args.baseline, ordinary_identical=len(records), feature_validated=len(records), entries=records,
                  game_launched=False, gpu_executed=False)
    (out / 'shader-result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'{len(records)} ordinary direct shaders byte-identical; {len(records)} two-surface variants compile/validate')


if __name__ == '__main__':
    main()
