"""Fail-closed same-run origin cost, pre-post float energy and rendered QA gate."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
from scripts import queue_gates as gates
from scripts.qa import detectors as det
from tools.live_batches import live_flow as flow
from tools.live_batches.ab_receipts import gpu_samples, summary, print_verdict, burst, moving_mask
from tools.live_batches.release_qual.analyze_final import energy
from tools.m4_promotion_gate import t_quantile,ALPHA_FW
LABELS=['off-a','on-a','off-b','on-b']
OFF=['off-a','off-b']
ON=['on-a','on-b']
COST=r'gi: origin pass GPU cost ([\d.]+) ms/frame'

def energy_gate(rows):
    result={}
    for channel,name in enumerate(['r','g','b','luminance']):
        values={label:rows[i]['mean_rgb'][channel] if channel<3 else rows[i]['luminance'] for i,label in enumerate(LABELS)}
        if not all(math.isfinite(x) for x in values.values()):
            raise ValueError('nonfinite pre-post energy')
        off=[values[label] for label in OFF]
        sigma=statistics.stdev(off)
        # Two OFF replicates: one degree of freedom. Bonferroni for
        # four channels times two ON windows; no candidate-fitted tolerance.
        band=t_quantile(1-ALPHA_FW/16,1)*sigma*math.sqrt(1.5)
        mean=statistics.mean(off)
        result[name]=dict(off=off,off_mean=mean,sigma=sigma,band=band,
            on={label:dict(value=values[label],delta=values[label]-mean,pass_=abs(values[label]-mean)<=band) for label in ON})
    return result

def analyze(job,plan,game,runner):
    errors=gates.check(job['expect_every'],game,runner,plan)
    for field,text in [('expect',game),('expect_runner',runner)]:
        errors+=['missing '+rx for rx in job[field] if not re.search(rx,text,re.M)]
    errors+=['forbidden '+rx for rx in job['forbid'] if re.search(rx,game,re.M|re.I)]
    lines=gates.stamped_lines(game)
    wins={w['label']:w for w in gates.windows(runner,plan,lines)}
    def selected(label):
        w=wins[label]
        if not w['ready']: raise ValueError('window not acknowledged: '+label)
        return [(t,s) for t,s in lines if w['applied']<=t<w['end']]
    costs,floats,captures={},[],{}
    for label in LABELS:
        window=label+'--static-a'
        picked=selected(window)
        values=gpu_samples(picked,wins[window],COST)
        if len(values)<2 or not all(math.isfinite(x) and x>0 for x in values):
            errors.append(label+': insufficient finite positive origin GPU periods')
        costs[label]=values
        exports=[m[1].strip() for _,s in selected('energy-'+label) if (m:=re.search(r'\[trace\] frame \d+ written:.* -> ([^\r\n]+)',s))]
        if len(exports)!=1: raise ValueError(label+': require one float pre-post export')
        floats.append(energy(exports[0]))
        serial=job['candidate']['serial_base']+LABELS.index(label)
        paths=[m[1].strip() for _,s in selected('qa-'+label) if (m:=re.search(rf'\[pan\] burst {serial} complete:.*transport ok -> (.+)$',s))]
        if len(paths)!=1: raise ValueError(label+': require one valid rendered K8 burst')
        captures[label]=burst(Path(paths[0]))
    if len({tuple(e['dimensions']) for e in floats})!=1: errors.append('pre-post dimensions changed')
    energies=energy_gate(floats)
    errors += ['pre-post '+name+' '+label+' energy outside OFF-repeat noise' for name,row in energies.items() for label,v in row['on'].items() if not v['pass_']]
    quality={}
    for on_label in ON:
        on=captures[on_label]
        for off_label in OFF:
            off=captures[off_label]
            shape=on['codes'][0].shape[:2]
            if any(f.shape[:2]!=shape or not np.isfinite(f).all() for f in on['codes']+off['codes']):
                raise ValueError('nonfinite or mismatched QA frames')
            shift=det.view_shift(off['albedo'][0],on['albedo'][0])
            if max(abs(shift[0]),abs(shift[1]))>=1: errors.append('camera drift '+on_label+' / '+off_label)
            moving=moving_mask(off['albedo'],on['albedo'],shape)
            q=det.temporal(on['codes'],off['codes'],moving=moving)
            q.pop('map',None)
            quality[on_label+'/'+off_label]=dict(shift=shift,qa=q)
            if not q['pass']: errors.append('QA flicker worse or inconclusive '+on_label+' / '+off_label)
    savings={}
    if all(len(costs[label])>=2 for label in LABELS):
        med={label:statistics.median(costs[label]) for label in LABELS}
        baseline=statistics.mean(med[label] for label in OFF)
        noise=abs(med[OFF[0]]-med[OFF[1]])
        for label in ON:
            saving=baseline-med[label]
            savings[label]=dict(off_ms=baseline,on_ms=med[label],saving_ms=saving,off_repeat_spread_ms=noise)
            if saving<=noise: errors.append(label+': origin saving does not exceed OFF-repeat spread')
    return dict(status='FAIL' if errors else 'PASS',errors=errors,gpu_periods=costs,savings=savings,energy=energies,qa=quality,
        receipt=summary(costs,floats,LABELS,OFF,ON),
        limitation='Two OFF energy replicates yield one degree of freedom and a broad derived prediction band. Static rendered QA only; no FPS inference. Offline exactness remains mandatory.')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('job','log','runner','out'): ap.add_argument('--'+name,required=True)
    a=ap.parse_args()
    try:
        job,plan,_=flow.load_job(a.job)
        result=analyze(job,plan,Path(a.log).read_text(encoding='utf-8-sig',errors='replace'),Path(a.runner).read_text(encoding='utf-8-sig',errors='replace'))
    except (OSError,ValueError,KeyError,TypeError) as e:
        result=dict(status='INVALID',errors=[str(e)])
    result['sources'] = dict(job=a.job, log=a.log, runner=a.runner)
    flow.write(a.out,result)
    print_verdict(result)
    return int(result['status']!='PASS')
if __name__=='__main__': sys.exit(main())
