"""Read-only hv9 pattern audit against the specified historical hair evidence."""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import line_matches


def rendered_sources(plan):
    """Read emitter strings from source, then substitute sample arguments."""
    def literal(path, prefix):
        text = (ROOT / path).read_text()
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', text)
        value = next(s for s in strings if prefix in s)
        return value[value.index(prefix):]

    complete = literal("scripts/auto-run.ps1", "[motion-plan] complete phases=")
    trace = literal("scripts/auto-run.ps1", "[motion-trace] phase=")
    burst = literal("layer/dump.cpp", "[pan] burst %d complete:")
    return {
        "scripts/auto-run.ps1 complete": complete.replace('$($panPlan.Count)', str(len(plan))),
        "scripts/auto-run.ps1 trace": trace.replace('$panIndex', '5').replace('$($step.label)', 'c-strands-plus').replace('$phaseTraceIndex', '0'),
        **{f"layer/dump.cpp {n}": burst % (n, 8, 20, 'ok', '<pan_dir>') for n in (98900, 98901)},
    }


def source_contract():
    here = Path(__file__).resolve().parent
    job = json.loads((here / "jobs/hv9-visual.json").read_text())
    plan = json.loads((here / "plans/hv9-visual.json").read_text())
    lines = rendered_sources(plan)
    patterns = job['expect_runner'][2:3] + job['expect_runner'][5:6]
    for gate in job['expect_every']:
        if gate['name'].startswith('K8 '):
            patterns.extend((gate['select'], gate['match']))
            assert any(re.search(gate['select'], line) and line_matches(gate, line) for line in lines.values()), gate
    assert len(patterns) == 6
    for pattern in patterns:
        assert any(re.search(pattern, line) for line in lines.values()), pattern
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--logs", type=Path, required=True)
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    job = json.loads((here / "jobs/hv9-visual.json").read_text())
    plan = json.loads((here / "plans/hv9-visual.json").read_text())
    names = ["hv7-pass-bisect.1.160354", "hv7-pass-bisect.1.145421", "hv9-visual.1.180225"]
    sources = source_contract()
    game = {n: (args.logs / (n + ".log")).read_text(errors="replace") for n in names}
    runner = {n: (args.logs / (n + ".runner.txt")).read_text(errors="replace") for n in names}
    checked = missing = violations = 0

    def audit(label, pattern, texts, negative=False, gate=None):
        nonlocal checked, missing, violations
        checked += 1
        hits = [name for name, text in texts.items() if
                (any(re.search(gate["select"], l) and line_matches(gate, l) for l in text.splitlines())
                 if gate else re.search(pattern, text, re.I | re.M))]
        if negative:
            state = "FORBID_ABSENT" if not hits else "FORBID_HIT"
            violations += bool(hits)
        else:
            source_hits = [name for name, line in sources.items() if
                           (re.search(gate["select"], line) and line_matches(gate, line)
                            if gate else re.search(pattern, line, re.I | re.M))]
            state = "MATCH" if hits else "MATCH_SOURCE" if source_hits else "OPEN_NO_MATCH"
            missing += not (hits or source_hits)
            if not hits: hits = source_hits
        print(f"{state} {label}: {pattern} => {','.join(hits) or 'none'}")

    for kind in ("expect", "expect_runner", "forbid"):
        for i, pattern in enumerate(job.get(kind, [])):
            audit(f"{kind}[{i}]", pattern, runner if kind == "expect_runner" else game, kind == "forbid")
    for gate in job["expect_every"]:
        audit("select " + gate["name"], gate["select"], game)
        audit("match " + gate["name"], gate["match"], game, gate=gate)
    for phase in plan:
        for kind in ("wait_log", "wait_log_fresh", "wait_stats"):
            waits = phase.get(kind, [])
            for pattern in ([waits] if isinstance(waits, str) else waits):
                audit(kind + " " + phase["label"], pattern, game)
    for path, line in sources.items():
        print(f"RENDERED_SOURCE {path}: {line}")
    # Historical hits are existential syntax/producer evidence only; they do not
    # certify hv9's unvisited windows. Never pass a forbidden hit as success.
    print(f"pattern check: {checked} patterns, {missing} missing positive patterns, {violations} forbidden hits")
    return int(bool(missing or violations))


if __name__ == "__main__":
    sys.exit(main())
