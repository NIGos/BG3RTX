"""Read-only ts1 evidence extraction. Cross-launch deltas never imply a quality pass."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates
from tools.m4_promotion_gate import Run

HERE = Path(__file__).resolve().parent


def producer_lines(lines, window, rx):
    """Accept the first acknowledged period; skip straddling legacy periods."""
    return queue_gates.complete_period_lines(
        [(t, line) for t, line in lines if re.search(rx, line)], window)


def extract(state, log, runner):
    job = json.loads((HERE / 'jobs' / f'ts1-native-tileset-{state}.json').read_text())
    plan_path = HERE / 'plans' / f'ts1-native-tileset-{state}.json'
    plan = json.loads(plan_path.read_text())
    game = log.read_text(encoding='utf-8-sig', errors='replace')
    motion = runner.read_text(encoding='utf-8-sig', errors='replace')
    failures = queue_gates.check(job['expect_every'], game, motion, plan)
    for rx in job['expect']:
        if not re.search(rx, game, re.M):
            failures.append('missing expect: ' + rx)
    for rx in job['expect_runner']:
        if not re.search(rx, motion, re.M):
            failures.append('missing runner: ' + rx)
    for rx in job['forbid']:
        if re.search(rx, game, re.M):
            failures.append('forbid: ' + rx)
    lines = queue_gates.stamped_lines(game)
    windows = queue_gates.windows(motion, plan, lines)
    costs, coverage = {}, []
    for w in windows:
        if w['label'].startswith('cost-'):
            for key, rx in [('rt_ms', r'rt passes: GPU cost ([0-9.]+) ms/frame'),
                            ('gi_ms', r'gi: GPU cost ([0-9.]+) ms/frame')]:
                samples = [float(re.search(rx, line)[1]) for line in producer_lines(lines, w, rx)]
                costs[key] = statistics.median(samples) if samples else None
                if not samples:
                    failures.append('missing complete cost samples: ' + key)
        if w['label'].startswith('diag-'):
            # Retain complete producer lines; include both coverage numerator and denominator.
            for rx in [r'\[alpha tileset\] records', r'hit material tileset \(',
                       r'rt stats \(\d+ frames\): hybrid GI', r'hit material \(\d+ frames\): attempted']:
                coverage += producer_lines(lines, w, rx)
    coverage_metrics = {}
    for key, rx in [('gi_hit_pct', r'hybrid GI [0-9.]+M rays/frame, ([0-9.]+)% hit'),
                    ('gi_screen_pct', r'of the hits ([0-9.]+)% shaded from the screen'),
                    ('gi_hidden_pct', r'([0-9.]+)% on screen but hidden'),
                    ('gi_offscreen_pct', r'([0-9.]+)% off screen'),
                    ('tileset_complete_records', r'records complete (\d+)'),
                    ('tileset_unknown_records', r'records complete \d+ unknown (\d+)'),
                    ('tileset_family2_acquisitions', r'qualified family2 (\d+)')]:
        values = [float(m[1]) for line in coverage if (m := re.search(rx, line))]
        if values:
            coverage_metrics[key] = statistics.median(values)
    shares = [100 * int(m[2]) / int(m[1]) for line in coverage
              if (m := re.search(r'hit material \(\d+ frames\): attempted ([1-9]\d*) qualified (\d+)', line))]
    if shares:
        coverage_metrics['material_qualified_pct'] = statistics.median(shares)
    # Run's shared float reader expects the normal sibling runner file.
    if runner.resolve() != log.with_suffix('.runner.txt').resolve():
        raise ValueError('runner must be the log sibling .runner.txt for the shared float reader')
    run = Run(log, plan=plan_path)
    energy = run.scene_colour('shot-' + state)
    if run.failed_drift():
        failures.append(str(run.drift))
    if not energy or isinstance(energy[0], str) or not math.isfinite(energy[0]) or energy[0] < 0 or energy[1]:
        failures.append('missing/invalid/nonfinite float pre-post scene mean: ' + str(energy))
    if energy and not isinstance(energy[0], str) and (energy[2] is None or not math.isfinite(energy[2]) or energy[2] <= 0):
        failures.append('missing/invalid pre-exposure; independent launch energy cannot be normalized')
    return dict(state=state, evidence_status='INVALID' if failures else 'VALID_TECHNICAL',
                quality_status='AWAITING_USER_LOOK', failures=failures, gpu=costs,
                coverage=coverage, coverage_metrics=coverage_metrics,
                float_pre_post=energy, log=str(log), runner=str(runner),
                source='dumps.json scene colour float mean (5c3fdabc slot 0), never PNG or bloom',
                limitations='Separate launches; view/exposure/noise may differ. No matched Dialogue3 PT reference. No quality pass or equal-energy gate.')


def compare(off, on):
    if off['evidence_status'] != 'VALID_TECHNICAL' or on['evidence_status'] != 'VALID_TECHNICAL':
        raise ValueError('both launches must have valid technical evidence')
    a, b = off['float_pre_post'], on['float_pre_post']
    return dict(energy_signed_delta=b[0]-a[0],
                energy_relative_delta=b[0]/a[0]-1 if a[0] else None,
                exposure_normalized_relative_delta=(b[0]/b[2])/(a[0]/a[2])-1 if a[0] else None,
                gpu_delta_ms={k: on['gpu'][k]-off['gpu'][k] for k in off['gpu']},
                coverage_delta={k: on['coverage_metrics'][k]-off['coverage_metrics'][k]
                                for k in off.get('coverage_metrics', {}) if k in on.get('coverage_metrics', {})},
                coverage_off=off['coverage'], coverage_on=on['coverage'],
                quality_status='AWAITING_USER_LOOK',
                limitation='Descriptive cross-launch deltas; inspect static PNG pair. No same-run causal A/B or PT judgement.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--state', choices=['off', 'on'])
    ap.add_argument('--log', type=Path)
    ap.add_argument('--runner', type=Path)
    ap.add_argument('--compare', nargs=2, type=Path, metavar=('OFF_JSON', 'ON_JSON'))
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    try:
        if args.compare:
            result = compare(*(json.loads(p.read_text()) for p in args.compare))
        else:
            if not all((args.state, args.log, args.runner)):
                ap.error('--state, --log and --runner are required')
            result = extract(args.state, args.log, args.runner)
        out = args.out or (args.log.with_suffix('.tileset.json') if args.log else None)
        if out:
            out.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
        return int(bool(result.get('failures')))
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(json.dumps(dict(evidence_status='INVALID', error=str(exc))))
        return 1


if __name__ == '__main__':
    sys.exit(main())
