"""Read-only re-gate of SCC1's historical run; never invokes shader tools or the queue."""
import argparse
import json
from pathlib import Path
import re
import statistics
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from scripts import queue_gates


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--log', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    batch = Path(__file__).resolve().parent
    job = json.loads((batch / 'jobs/scc1-spec-cascade-s8-fg2.json').read_text())
    plan = json.loads((batch / 'history/scc_s8_013945.json').read_text())
    game = args.log.read_text(encoding='utf-8-sig', errors='replace')
    runner = args.log.with_suffix('.runner.txt').read_text(encoding='utf-8-sig', errors='replace')
    labels = {s['label'] for s in plan}
    gates, missing = [], []
    for gate in job['expect_every']:
        gate = dict(gate)
        missing.extend(w for w in gate.get('windows', []) if w not in labels)
        if 'windows' in gate:
            gate['windows'] = [w for w in gate['windows'] if w in labels]
            if not gate['windows']:
                continue
        gates.append(gate)
    failures = queue_gates.check(gates, game, runner, plan)
    failures += ['missing expect: ' + p for p in job['expect'] if not re.search(p, game, re.M)]
    failures += ['forbidden: ' + p for p in job['forbid'] if re.search(p, game, re.M)]
    # The new plan has additional phases; historical completion is checked against its preserved plan.
    for p in job['expect_runner']:
        p = re.sub(r'phases=\d+', f'phases={len(plan)}', p)
        if not re.search(p, runner, re.M):
            failures.append('missing runner expect: ' + p)
    lines = queue_gates.stamped_lines(game)
    wins = queue_gates.windows(runner, plan, lines)
    compiles = [dict(line=l, window=next((w['label'] for w in wins if w['start'] <= t < w['end']), 'startup'))
                for t,l in lines if re.search(r'\[shaders\] \S+:\w+ compiled in ', l)]
    pipelines = [float(x) for x in re.findall(r'spec cascade=0[^\n]*pipeline ([0-9.]+) ms', game)]
    costs = {}
    for w in wins:
        if not w['label'].startswith('cost-'):
            continue
        costs[w['label']] = {}
        for name, pattern in [('gi',r'gi: GPU cost ([0-9.]+) ms/frame'),('rf',r'rf: reflection pass GPU median ([0-9.]+) ms'),('ls',r'ls: local-light shadow pass GPU median ([0-9.]+) ms'),('rt',r'rt passes: GPU cost ([0-9.]+) ms/frame')]:
            # First stats line straddles the settings transition, as in queue_gates.
            samples = [float(m.group(1)) for t,l in lines if w['applied'] <= t < w['end'] and (m:=re.search(pattern,l))][1:]
            costs[w['label']][name] = statistics.median(samples) if samples else None
    energy = {}
    for w in wins:
        if not w['label'].startswith('shot-'):
            continue
        captures = [Path(m.group(1).strip()) for t,l in lines if w['start'] <= t < w['end'] and (m:=re.search(r'\[trace\].* -> (.+)',l))]
        vals=[]
        for cap in captures:
            dumps=json.loads((cap/'dumps.json').read_text())
            candidates=[v for v in dumps.values() if isinstance(v,dict) and str(v.get('pass_fs','')).startswith('5c3fdabc') and v.get('slot')==0]
            if len(candidates)!=1:
                raise ValueError(f'{cap}: expected one pre-post scene colour, got {len(candidates)}')
            mean=candidates[0]['mean']; vals.append(sum(a*b for a,b in zip(mean,[0.2126,0.7152,0.0722])))
        energy[w['label']]=statistics.mean(vals) if vals else None
    comparisons={}
    for c in ['c1','c0']:
        comparisons[c]={}
        families={'energy': {k.removeprefix('shot-'):v for k,v in energy.items()}}
        for metric in ['gi','rf','ls','rt']:
            families[metric]={k.removeprefix('cost-'):v[metric] for k,v in costs.items()}
        for metric,values in families.items():
            a,on,b=[values.get(f'{c}-{s}') for s in ['off-a','on','off-b']]
            comparisons[c][metric]=None if None in [a,on,b] else dict(delta=on-(a+b)/2,off_off_spread=abs(a-b),within_spread=abs(on-(a+b)/2)<=abs(a-b))
    for state, metrics in comparisons.items():
        for metric, comparison in metrics.items():
            if comparison is None:
                failures.append(f'{state} {metric}: missing repeat evidence')
            elif not comparison['within_spread']:
                failures.append(f'{state} {metric}: delta {comparison["delta"]:.6g} exceeds OFF-OFF spread {comparison["off_off_spread"]:.6g}')
    result=dict(status='FAIL' if failures else 'PASS_OBSERVED_WINDOWS_ONLY',full_migration='NOT_COVERED',failures=failures,unobserved_windows=sorted(set(missing)),compiles=compiles,cascade_off_pipeline_ms=pipelines,energy=energy,gpu_medians_ms=costs,repeat_comparisons=comparisons,qualification='Retired switch ignored: same-state repeats, not old/new equivalence. New converted-key windows absent. No full qualification.')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=1)+'\n')
    print(json.dumps(dict(status=result['status'],failures=failures,compiles=compiles,repeat_comparisons=comparisons),indent=1))
    return 1 if failures else 0

if __name__=='__main__':
    sys.exit(main())
