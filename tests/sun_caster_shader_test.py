"""Offline DXC gate: every ordinary entry identical, every current entry valid.

Every file plus every GI variant request (shaders/variants.txt) compiles and passes spirv-val;
the define-free files that exist in the baseline are byte-identical to it, entry by entry.
Compiles through tools/shader_identity.py (shared cache; unchanged inputs are identical by
construction). Result: captures/sun-coverage-20261030/shader-check/result.json.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (SHADERS, compile_requests, default_baseline, default_jobs, gate_setup,  # noqa: E402
                                   identical, identity)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default=default_baseline(),
                        help='Commit of the key-off reference (default: merge-base with main)')
    parser.add_argument('--dxc', required=True)
    parser.add_argument('--spirv-val', required=True)
    parser.add_argument('--sharc', required=True)
    parser.add_argument('--jobs', type=int, default=default_jobs())
    parser.add_argument('--no-cache', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'captures/sun-coverage-20261030/shader-check'
    out.mkdir(parents=True, exist_ok=True)
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, args.spirv_val, args.no_cache)
    requests = new.requests()
    ordinary = [r for r in requests if '?' not in r and SHADERS + '/' + r in base.ids]
    try:
        same = {r: (s, log) for r, s, log in identity(base, new, ordinary, tool, cache, args.jobs, 'sun-caster ordinary')}
        cur = compile_requests(new, requests, tool=tool, cache=cache, jobs=args.jobs, label='sun-caster current', validate=True)
    finally:
        base.cleanup()
    records = []
    for r in requests:
        res = cur[r]
        if not res.ok or not res.valid:
            raise RuntimeError(f'{r}: {res.log}')
        if r in same and not identical(same[r][0]):
            raise RuntimeError(f'Ordinary byte identity failed ({same[r][0]}): {r}\n{same[r][1]}')
        records += [{'shader': r, 'entry': e.name, 'ordinary': r in same, 'sha256': e.sha} for e in res.entries]
    result = {'baseline': args.baseline, 'ordinary_identical': sum(x['ordinary'] for x in records),
              'new_compiled_validated': sum(not x['ordinary'] for x in records), 'entries': records}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"{result['ordinary_identical']} ordinary entries byte-identical; "
          f"{result['new_compiled_validated']} new entries compiled/validated")


if __name__ == '__main__':
    main()
