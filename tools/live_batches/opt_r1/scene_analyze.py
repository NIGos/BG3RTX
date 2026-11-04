"""Fail-closed scene A/B gate using existing reset, energy and QA readers."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates as gates
from scripts.qa import detectors as det
from tools.live_batches import live_flow as flow
from tools.live_batches.edge_flicker.analyze import burst
from tools.live_batches.release_qual.analyze_final import energy, COSTS
from tools.m4_promotion_gate import t_quantile, ALPHA_FW

EXTRA = dict(convert=r'scene build GPU .*median convert\+extract ([\d.]+) ms')
LABELS = ['off-a', 'off-b', 'on', 'off-c']


def energy_gate(rows):
    """OFF-repeat Student band, no candidate-tuned energy percentage."""
    result = {}
    for channel in range(4):
        values = [r['mean_rgb'][channel] if channel < 3 else r['luminance'] for r in rows]
        if not all(math.isfinite(x) for x in values):
            raise ValueError('nonfinite pre-post energy')
        off = [values[i] for i in (0, 1, 3)]
        # Three OFF replicates estimate noise with 2 degrees of freedom.
        # Four two-sided channel checks share the existing family alpha.
        sigma = statistics.stdev(off)
        band = t_quantile(1-ALPHA_FW/8, 2)*sigma*math.sqrt(1.5)
        delta = values[2]-(values[1]+values[3])/2
        result[['r', 'g', 'b', 'luminance'][channel]] = dict(
            off=off, on=values[2], sigma=sigma, band=band, delta=delta, pass_=abs(delta)<=band)
    return result


def analyze(job, plan, game, runner):
    errors = gates.check(job['expect_every'], game, runner, plan)
    for field, text in [('expect', game), ('expect_runner', runner)]:
        errors += ['missing '+rx for rx in job[field] if not re.search(rx, text, re.M)]
    errors += ['forbidden '+rx for rx in job['forbid'] if re.search(rx, game, re.M|re.I)]
    lines = gates.stamped_lines(game)
    wins = {w['label']:w for w in gates.windows(runner, plan, lines)}

    def selected(label):
        w = wins[label]
        if not w['ready']:
            raise ValueError('window not acknowledged: '+label)
        return [(t,s) for t,s in lines if w['applied']<=t<w['end']]

    costs, floats, captures = {}, [], {}
    wanted = ['convert', 'scene build', 'GI trace', 'local shadows', 'reflections']
    for label in LABELS:
        window = label+'--static-a'
        picked = gates.complete_period_lines(selected(window), wins[window])
        row = {}
        for name in wanted:
            rx = dict(COSTS, **EXTRA)[name]
            values = [float(m[1]) for _,s in picked if (m:=re.search(rx,s))]
            if len(values)<2 or not all(math.isfinite(x) for x in values):
                errors.append(label+': insufficient complete '+name+' GPU periods')
            row[name] = values
        costs[label] = row
        exports = [m[1].strip() for _,s in selected('energy-'+label)
                   if (m:=re.search(r'\[trace\] frame \d+ written:.* -> ([^\r\n]+)',s))]
        if len(exports)!=1:
            raise ValueError(label+': require one float pre-post export')
        floats.append(energy(exports[0]))
        serial=job['candidate']['serial_base']+LABELS.index(label)
        paths=[m[1].strip() for _,s in selected('qa-'+label)
               if (m:=re.search(rf'\[pan\] burst {serial} complete:.*transport ok -> (.+)$',s))]
        if len(paths)!=1:
            raise ValueError(label+': require one valid rendered K8 burst')
        captures[label]=burst(Path(paths[0]))
    if len({tuple(e['dimensions']) for e in floats})!=1:
        errors.append('pre-post energy dimensions changed')
    energies=energy_gate(floats)
    errors += ['pre-post '+k+' energy outside OFF-repeat noise' for k,v in energies.items() if not v['pass_']]
    quality={}
    on=captures['on']
    for label in ('off-a','off-b','off-c'):
        off=captures[label]
        shape=on['codes'][0].shape[:2]
        frames=on['codes']+off['codes']+on['albedo']+off['albedo']
        if any(f.shape[:2]!=shape or not np.isfinite(f).all() for f in frames):
            raise ValueError('nonfinite or mismatched QA frames')
        shift=det.view_shift(off['albedo'][0],on['albedo'][0])
        if max(abs(shift[0]),abs(shift[1]))>=1:
            errors.append('camera drift versus '+label)
        moving=det.animated(off['albedo']+on['albedo'],[],shape)
        q=det.temporal(on['codes'],off['codes'],moving=moving)
        q.pop('map',None)
        quality[label]=dict(shift=shift,qa=q)
        if not q['pass']:
            errors.append('QA flicker worse or inconclusive versus '+label)
    savings={}
    if not any('GPU periods' in e for e in errors):
        med=lambda label,name:statistics.median(costs[label][name])
        for name in wanted:
            off=(med('off-b',name)+med('off-c',name))/2
            savings[name]=dict(off_ms=off,on_ms=med('on',name),saving_ms=off-med('on',name))
        touched=savings['convert']['saving_ms']
        noise=max(med(label,'convert') for label in ('off-a','off-b','off-c'))-min(
                  med(label,'convert') for label in ('off-a','off-b','off-c'))
        if touched<max(.05,noise):
            errors.append('saving below 0.05 ms or OFF-repeat spread')
        total_names=['scene build','GI trace','local shadows','reflections']
        baseline=[sum(med(label,n) for n in total_names) for label in ('off-a','off-b','off-c')]
        combined=sum(med('on',n) for n in total_names)
        if combined>(baseline[1]+baseline[2])/2+max(baseline)-min(baseline):
            errors.append('scene build + subsequent ray passes regressed beyond OFF-repeat spread')
    return dict(status='FAIL' if errors else 'PASS',errors=errors,gpu_periods=costs,
                savings=savings,energy=energies,qa=quality,
                limitation='Static rendered QA only; no presented FG frame-time or FPS inference. Exact geometry proof remains a separate mandatory offline gate.')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('job','log','runner','out'):
        ap.add_argument('--'+name,required=True)
    a=ap.parse_args()
    try:
        job,plan,_=flow.load_job(a.job)
        result=analyze(job,plan,Path(a.log).read_text(encoding='utf-8-sig',errors='replace'),
                       Path(a.runner).read_text(encoding='utf-8-sig',errors='replace'))
    except (OSError,ValueError,KeyError,TypeError) as e:
        result=dict(status='INVALID',errors=[str(e)])
    flow.write(a.out,result)
    print(json.dumps(dict(status=result['status'],errors=result['errors'])))
    return int(result['status']!='PASS')


if __name__=='__main__':
    sys.exit(main())
