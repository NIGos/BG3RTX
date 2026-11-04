"""C2 ordinary-request identity and strict specialization-default equivalence.

The primary G-buffer/count and fibre passes are opt-in feature programs changed
by C2 independently of the shadow toggle; validate them separately with the GPU
bench. No baseline overlay, reference define or surviving-spec blanket freeze.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import gate_setup, identity, identical, split_request, unchanged_since
from tools.hair_retirement_check import retirement_fallback


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', default='main')
    ap.add_argument('--dxc')
    ap.add_argument('--sharc', default='auto')
    ap.add_argument('--spirv-val')
    ap.add_argument('--json')
    a = ap.parse_args()
    tool, cache, base, new = gate_setup(a.baseline, a.dxc, a.sharc, a.spirv_val)
    features = {'gi/hair_fibre.hlsl', 'rt/hair_primary_gbuffer.hlsl', 'rt/hair_primary_count.hlsl'}
    old, current = set(base.requests()), set(new.requests())
    missing = sorted(old - current)
    requests = sorted(r for r in old & current if split_request(r)[0] not in features)
    try:
        raw = identity(base, new, requests, tool, cache, 1, 'C2 ordinary')
        proofs = retirement_fallback(raw, base, new, tool, cache, 'C2 ordinary')
        unchanged_since(new)
    finally:
        base.cleanup()
        new.cleanup()
    failures = [p for p in proofs if p['path'] == 'failed']
    n = sum(identical(state) for _, state, _ in raw)
    report = dict(baseline=base.rev, requests=len(requests), byte_identical=n,
                  equivalent_at_default=len(requests)-n-len(failures), failures=failures,
                  request_delta=len(current)-len(old), missing=missing, paths=proofs,
                  feature_requests=sorted(r for r in old & current if split_request(r)[0] in features))
    if a.json:
        Path(a.json).write_text(json.dumps(report, indent=2)+'\n')
    print(f"{n}/{len(requests)} requests byte-identical, "
          f"{report['equivalent_at_default']} equivalent at default; "
          f"{len(failures)} failed; request delta {report['request_delta']}", flush=True)
    return int(bool(failures or missing or current-old or not requests))


if __name__ == '__main__':
    sys.exit(main())
