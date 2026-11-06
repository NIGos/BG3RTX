"""Compare paired replay numeric differences with the OFF/OFF empirical p99 bound."""
from __future__ import annotations
import argparse
import csv
import json
import math
from pathlib import Path
from statistics import median

METRICS = ('different_pixels', 'max_abs', 'mean_abs_diff', 'abs_mean_diff')

def percentile99(values):
    """Empirical nearest-rank p99; 40 pairs use the largest control observation."""
    values = sorted(values)
    return values[math.ceil(.99 * len(values)) - 1]

def read_numeric(path, pairs=40):
    result = {}
    with Path(path).open(newline='') as stream:
        for row in csv.DictReader(stream):
            key = (row['output'], int(row['channel']))
            item = {name: float(row[name]) for name in METRICS if name != 'abs_mean_diff'}
            item['mean_diff'] = float(row['mean_diff'])
            item['abs_mean_diff'] = abs(item['mean_diff'])
            item['pair'] = int(row['pair'])
            if int(row['nonfinite']) or not all(math.isfinite(v) for v in item.values()):
                raise ValueError('Nonfinite numerical difference')
            result.setdefault(key, []).append(item)
    if not result or any(sorted(r['pair'] for r in rows) != list(range(1, pairs+1)) for rows in result.values()):
        raise ValueError(f'Expected exactly {pairs} pairs per output/channel')
    return result

def compare(control, candidate):
    if control.keys() != candidate.keys():
        raise ValueError('Output/channel sets differ')
    channels = []
    for key, rows in control.items():
        limits = {name: percentile99([r[name] for r in rows]) for name in METRICS}
        observed = {name: max(r[name] for r in candidate[key]) for name in METRICS}
        exceeded = {name: sum(r[name] > limits[name] for r in candidate[key]) for name in METRICS}
        channels.append(dict(output=key[0], channel=key[1], control_p99=limits,
            candidate_max=observed, exceeded_pairs=exceeded,
            control_mean_signed=sum(r['mean_diff'] for r in rows)/len(rows),
            candidate_mean_signed=sum(r['mean_diff'] for r in candidate[key])/len(candidate[key]),
            passed=not any(exceeded.values())))
    return dict(passed=all(row['passed'] for row in channels), channels=channels)

def timing(path):
    with Path(path).open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    ds = sorted(float(r['delta_ms']) for r in rows)
    # Exact distribution-free two-sided 95% median interval, matching replay_ab.h.
    n=len(ds)
    k=max((j+1 for j in range(n//2+1) if sum(math.comb(n,i) for i in range(j+1))/2**n <= .025), default=0)
    return dict(pairs=n, median_ms=median(ds), ci95_ms=[ds[k-1],ds[n-k]] if k else None,
        A_median_ms=median(float(r['A_ms']) for r in rows), B_median_ms=median(float(r['B_ms']) for r in rows),
        counters_equal=all(r['counters_equal']=='1' for r in rows))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--control', type=Path, required=True)
    ap.add_argument('--candidate', type=Path, required=True)
    ap.add_argument('--pairs', type=int, default=40)
    ap.add_argument('--out', type=Path, required=True)
    args=ap.parse_args()
    report=compare(read_numeric(args.control/'ab_numeric.csv',args.pairs),read_numeric(args.candidate/'ab_numeric.csv',args.pairs))
    report.update(control=str(args.control),candidate=str(args.candidate),pairs=args.pairs,
        bound='empirical nearest-rank control p99 (maximum for 40 pairs); every candidate pair must fit each bound',
        comparison='phase 0 vs phase 1 per ABBA/BAAB quartet; signed differences B-A; means over all pixels',
        control_timing=timing(args.control/'ab.csv'),candidate_timing=timing(args.candidate/'ab.csv'))
    args.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
