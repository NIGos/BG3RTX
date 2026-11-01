"""CPU-only physical-origin wrappers, strict off-SPIR-V and RF descriptor proof.

Each Hair-origin request without the tileset: the same request without RT_HAIR_PHYSICAL_ORIGIN
is byte-identical to the baseline; the request compiles, passes spirv-val and uses the feature
image (set 0, binding 50) that the off module never uses; RF modules never use binding 12.
Compiles through tools/shader_identity.py (shared cache). No device, game, live inbox or deployment.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (compile_requests, default_baseline, default_jobs, gate_setup, identical,  # noqa: E402
                                   identity, unchanged_since)


def bindings(raw):
    words = struct.unpack('<' + 'I' * (len(raw) // 4), raw)
    decorations = {}
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 65535
        if not count:
            raise ValueError('Invalid SPIR-V instruction')
        if op == 71 and count >= 4 and words[i + 2] in (33, 34):
            decorations.setdefault(words[i + 1], {})[words[i + 2]] = words[i + 3]
        i += count
    return {(v[34], v[33]) for v in decorations.values() if 33 in v and 34 in v}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dxc', required=True)
    ap.add_argument('--sharc', required=True)
    ap.add_argument('--baseline', default=default_baseline(), help='Commit of the key-off reference (default: merge-base with main)')
    ap.add_argument('--jobs', type=int, default=default_jobs())
    ap.add_argument('--no-cache', action='store_true')
    args = ap.parse_args()
    out = ROOT / 'captures/hair-physical-origin/shaders'
    out.mkdir(parents=True, exist_ok=True)
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, no_cache=args.no_cache)
    # The old shaders/hair_origin/ wrappers: every Hair-origin variant without the tileset.
    wrappers = [l for l in new.requests() if 'RT_HAIR_PHYSICAL_ORIGIN=1;' in l and 'RT_NATIVE_TILESET=1' not in l]
    target = {w: w.replace('RT_HAIR_PHYSICAL_ORIGIN=1;', '') for w in wrappers}
    try:
        same = {r: (s, log) for r, s, log in identity(base, new, sorted(set(target.values())), tool, cache, args.jobs, 'hair-origin key-off')}
        on = compile_requests(new, wrappers, tool=tool, cache=cache, jobs=args.jobs, label='hair-origin key-on', validate=True)
        off = compile_requests(new, sorted(set(target.values())), tool=tool, cache=cache, jobs=args.jobs, label='hair-origin off')
    finally:
        base.cleanup()
    records = []
    for w in wrappers:
        t = target[w]
        state, log = same[t]
        if not identical(state):
            raise RuntimeError(f'Key-off SPIR-V changed ({state}): {t}\n{log}')
        if not on[w].ok or not on[w].valid:
            raise RuntimeError(f'{w}: {on[w].log}')
        if not off[t].ok:
            raise RuntimeError(f'{t}: {off[t].log}')
        on_b, off_b = bindings(on[w].blob(on[w].entries[0].name)), bindings(off[t].blob(off[t].entries[0].name))
        if (0, 50) not in on_b:
            raise RuntimeError(f'Feature image not used: {w}')
        if (0, 50) in off_b:
            raise RuntimeError(f'Feature image leaked into off path: {t}')
        rf = t.partition('?')[0].rsplit('/', 1)[-1] in ('rt_reflections.hlsl', 'rt_reflections_sharc.hlsl')
        if rf and ((0, 12) in on_b or (0, 12) in off_b):
            raise RuntimeError(f'Cannot omit RF binding12: {t}')
        records.append(dict(file=t, off_sha256=off[t].entries[0].sha, on_sha256=on[w].entries[0].sha, rf_binding12_unused=rf))
    unchanged_since(new)
    result = dict(baseline=args.baseline, key_off_identical=len(records), feature_compiled_validated=len(records),
                  rf_variants_binding12_unused=sum(r['rf_binding12_unused'] for r in records), entries=records)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'{len(records)} key-off SPIR-V byte-identical; {len(records)} feature variants valid; '
          f'{result["rf_variants_binding12_unused"]} RF variants omit binding12 safely')


if __name__ == '__main__':
    main()
