"""Water surface arithmetic OFF identity and capability SPIR-V validation.

The compile-time REFERENCE=0 form proves dormant arithmetic matches the baseline
for every compose request. This is separate from raw ordinary request identity:
capability modules gain a specialization branch and therefore change raw bytes.
Raw differences must pass selective retired-feature equivalence for that same
request/reference, with only removed SpecIds 1001/1002/1005/1006/1016-1019/1021-1024 fixed at zero.
The separate C2 default comparison fixes 1026 on both sides; other live IDs remain variable.
The production module uses specialization 1025; no reference define is requested
at runtime. All compilation uses the serial cached BelowNormal shared helper.
"""
import argparse
import json
from pathlib import Path
import sys
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import compile_requests, default_baseline, gate_setup, identical, identity, split_request, unchanged_since  # noqa: E402
from tools.hair_retirement_check import retirement_fallback  # noqa: E402


def reference_off(request):
    return request + (";" if "?" in request else "?") + "WATER_SURFACE_FRESNEL_REFERENCE=0"


def counter_contract(blob):
    """Inspect the emitted production module: all 12 reason lanes have atomic sinks.

    This catches an omitted reason call, a shortened shared array and compile-time
    elimination of diagnostics when specialization 1025 defaults to zero.
    """
    words = struct.unpack(f"<{len(blob)//4}I", blob)
    instructions = []
    at = 5
    while at < len(words):
        size, opcode = words[at] >> 16, words[at] & 65535
        if not size or at + size > len(words):
            return "malformed SPIR-V"
        instructions.append((opcode, words[at+1:at+size]))
        at += size
    constants = {a[1]: a[2] for op,a in instructions if op == 43 and len(a) == 3}
    names = {a[0]: struct.pack(f"<{len(a)-1}I", *a[1:]).split(b"\0")[0].decode()
             for op,a in instructions if op == 5}
    shared = next((i for i,n in names.items() if n == "gsSurface"), None)
    if shared is None:
        return "missing gsSurface"
    array_lengths = {a[0]: constants.get(a[2]) for op,a in instructions if op == 28}
    pointer_types = {a[0]: (a[1], a[2]) for op,a in instructions if op == 32}
    variable = next((a for op,a in instructions if op == 59 and a[1] == shared), None)
    pointee = pointer_types.get(variable[0], ()) if variable else ()
    if not pointee or pointee[0] != 4 or array_lengths.get(pointee[1]) != 12:
        return "gsSurface is not a 12-word workgroup array"
    accesses = {a[1]: constants.get(a[-1]) for op,a in instructions if op in (65,66) and a[2] == shared}
    atomic_lanes = {accesses[a[2]] for op,a in instructions if op == 234 and a[2] in accesses}
    if not set(range(12)) <= atomic_lanes:
        return f"missing reason atomic lanes {sorted(set(range(12))-atomic_lanes)}"
    # Decode clean/reason bits, rather than merely carrying unused integer literals.
    bit_masks = {constants[x] for op,a in instructions if op == 199 for x in a[2:] if x in constants}
    if not {1,8,16,32,255,49152,49160} <= bit_masks:
        return f"missing marker/reason decoding masks {sorted({1,8,16,32,255,49152,49160}-bit_masks)}"
    if not any(op == 71 and len(a) == 3 and a[1:] == (1,1025) for op,a in instructions):
        return "missing specialization 1025"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default=default_baseline())
    parser.add_argument("--dxc")
    parser.add_argument("--sharc", default="auto")
    parser.add_argument("--spirv-val")
    parser.add_argument("--json")
    args = parser.parse_args()
    tool, cache, base, new = gate_setup(args.baseline, args.dxc, args.sharc, args.spirv_val)
    old = [r for r in base.requests() if split_request(r)[0] == "rt/water_compose.hlsl"
           and "WATER_HIT_SHADE_LAYOUT=1" not in split_request(r)[1]]
    current = set(new.requests())
    missing = [r for r in old if r not in current]
    requests = [r for r in old if r in current]
    off = [reference_off(r) for r in requests]
    ordinary = [r for r in requests if "WATER_SPEC_RF_EDGE=1" not in split_request(r)[1]]
    try:
        results = identity(base, new, off, tool, cache, 1, "water surface arithmetic OFF")
        raw = identity(base, new, ordinary, tool, cache, 1, "water surface raw ordinary")
        off_proofs = retirement_fallback(results, base, new, tool, cache, "arithmetic OFF")
        raw_proofs = retirement_fallback(raw, base, new, tool, cache, "raw ordinary")
        # Default specialization branches are retained and checked by spirv-val;
        # these are the actual runtime requests, not the arithmetic reference.
        on = compile_requests(new, requests, tool=tool, cache=cache, jobs=1,
                              label="water surface capability", validate=True)
        unchanged_since(new)
    finally:
        base.cleanup()
        new.cleanup()
    failed_off = [p for p in off_proofs if p['path'] == 'failed']
    failed_on = [r for r in on.values() if not r.ok or not r.valid]
    counter_failures = []
    for request, result in on.items():
        if result.ok and "WATER_SPEC_RF_EDGE=1" in split_request(request)[1]:
            failure = counter_contract(result.blob("water_compose"))
            if failure:
                counter_failures.append((request, failure))
    for request, failure in counter_failures:
        print(f"FAIL counter contract {request}: {failure}")
    failed_raw = [p for p in raw_proofs if p['path'] == 'failed']
    for p in failed_off:
        print(f"FAIL arithmetic OFF {p['request']}: {p['raw_state']}\n{p['error']}")
    for p in failed_raw:
        print(f"FAIL raw ordinary {p['request']}: {p['raw_state']}\n{p['error']}")
    for r in failed_on:
        print(f"FAIL capability {r.request}: {r.log}")
    report = {"baseline": args.baseline, "compose_requests": len(requests),
              "missing": missing, "arithmetic_off_identical": sum(identical(s) for _, s, _ in results),
              "arithmetic_off_accepted": len(results) - len(failed_off),
              "arithmetic_off_paths": off_proofs, "raw_ordinary_paths": raw_proofs,
              "production_validated": len(on) - len(failed_on),
              "raw_ordinary_requests": len(ordinary), "raw_ordinary_identical": sum(identical(s) for _, s, _ in raw),
              "raw_ordinary_accepted": len(raw) - len(failed_raw),
              "counter_contract_failures": counter_failures,
              "raw_capability_identity": "not claimed; capability modules add specialization arithmetic"}
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return int(not requests or bool(missing or failed_off or failed_on or failed_raw or counter_failures))


if __name__ == "__main__":
    sys.exit(main())
