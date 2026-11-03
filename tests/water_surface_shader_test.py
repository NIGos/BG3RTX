"""Water surface arithmetic OFF identity and capability SPIR-V validation.

The compile-time REFERENCE=0 form proves dormant arithmetic matches the baseline
for every compose request. This is separate from raw ordinary request identity:
capability modules gain a specialization branch and therefore change raw bytes.
The production module uses specialization 1025; no reference define is requested
at runtime. All compilation uses the serial cached BelowNormal shared helper.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import compile_requests, default_baseline, gate_setup, identical, identity, split_request, unchanged_since  # noqa: E402


def reference_off(request):
    return request + (";" if "?" in request else "?") + "WATER_SURFACE_FRESNEL_REFERENCE=0"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default=default_baseline())
    parser.add_argument("--dxc")
    parser.add_argument("--sharc", default="auto")
    parser.add_argument("--spirv-val")
    parser.add_argument("--json")
    args = parser.parse_args()
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, args.spirv_val)
    old = [r for r in base.requests() if split_request(r)[0] == "rt/water_compose.hlsl"]
    current = set(new.requests())
    missing = [r for r in old if r not in current]
    requests = [r for r in old if r in current]
    off = [reference_off(r) for r in requests]
    ordinary = [r for r in requests if "WATER_SPEC_RF_EDGE=1" not in split_request(r)[1]]
    try:
        results = identity(base, new, off, tool, cache, 1, "water surface arithmetic OFF")
        raw = identity(base, new, ordinary, tool, cache, 1, "water surface raw ordinary")
        # Default specialization branches are retained and checked by spirv-val;
        # these are the actual runtime requests, not the arithmetic reference.
        on = compile_requests(new, requests, tool=tool, cache=cache, jobs=1,
                              label="water surface capability", validate=True)
        unchanged_since(new)
    finally:
        base.cleanup()
        new.cleanup()
    failed_off = [(r, state, log) for r, state, log in results if not identical(state)]
    failed_on = [r for r in on.values() if not r.ok or not r.valid]
    failed_raw = [(r, state, log) for r, state, log in raw if not identical(state)]
    for r, state, log in failed_off:
        print(f"FAIL arithmetic OFF {r}: {state}\n{log}")
    for r, state, log in failed_raw:
        print(f"FAIL raw ordinary {r}: {state}\n{log}")
    for r in failed_on:
        print(f"FAIL capability {r.request}: {r.log}")
    report = {"baseline": args.baseline, "compose_requests": len(requests),
              "missing": missing, "arithmetic_off_identical": len(results) - len(failed_off),
              "production_validated": len(on) - len(failed_on),
              "raw_ordinary_requests": len(ordinary), "raw_ordinary_identical": len(raw) - len(failed_raw),
              "raw_capability_identity": "not claimed; capability modules add specialization arithmetic"}
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return int(not requests or bool(missing or failed_off or failed_on or failed_raw))


if __name__ == "__main__":
    sys.exit(main())
