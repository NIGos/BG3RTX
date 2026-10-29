"""Read one presented static/slow/fast off/on run. Missing RT is a hard failure."""
import argparse
import json
from pathlib import Path
import re
import statistics


def clock(line):
    match = re.search(r'(\d\d):(\d\d):(\d\d)\.(\d{3})', line)
    if not match: return None
    h,m,s,ms = map(int,match.groups())
    return h*3600+m*60+s+ms/1000


def analyze(log, runner, plan):
    starts = {}
    for line in runner.splitlines():
        match = re.search(r'\[pan-plan\] window (\d+)',line)
        if match and clock(line) is not None:
            starts[int(match[1])] = clock(line)
    rows = []
    for index,step in enumerate(plan):
        on = 'rt_scene_rebase=1' in step['set']
        mode = 'static' if not step['repeat_ms'] else 'slow' if step['repeat_ms'] == 100 else 'fast'
        row = {'index':index,'mode':mode,'on':on,'primary_match':[], 'miss':[], 'rt_missing':[],
               'rt_active':[], 'fps':[], 'gpu_ms':[]}
        if index not in starts:
            row['error'] = 'missing phase marker'; rows.append(row); continue
        # Exclude shader warmup, live-setting polling, mixed statistics and
        # readback latency. Each full 120-frame window must lie in this interior.
        begin = starts[index]+max(15,step['motion_start']+10 if mode!='static' else 15)
        end = starts[index]+min(step['seconds']-10,step['motion_end']-5 if mode!='static' else step['seconds']-10)
        previous_watch = None
        for line in log.splitlines():
            t = clock(line)
            if t is None: continue
            watch = re.search(r'\[watch\] presents (\d+) \(displayed (\d+)\)',line)
            if watch:
                if previous_watch is not None and begin <= previous_watch < t <= end:
                    count,displayed = map(int,watch.groups())
                    row['fps'].append(count/(t-previous_watch))
                    if displayed == 0: row['error'] = 'no displayed frames'
                previous_watch = t
            if not begin <= t <= end: continue
            primary = re.search(r'primary-ray check match ([\d.]+)%.*miss ([\d.]+)%',line)
            if primary:
                row['primary_match'].append(float(primary[1])); row['miss'].append(float(primary[2]))
            rt = re.search(r'rt passes: GPU cost ([\d.]+) ms/frame.*TLAS this frame (\d+), previous frame (\d+), none (\d+)',line)
            if rt:
                row['gpu_ms'].append(float(rt[1])); row['rt_active'].append(int(rt[2])+int(rt[3])); row['rt_missing'].append(int(rt[4]))
        rows.append(row)
    reasons = []
    expected = {(mode,on) for mode in ('static','slow','fast') for on in (False,True)}
    if {(r['mode'],r['on']) for r in rows} != expected: reasons.append('missing static/slow/fast off/on phase')
    for r in rows:
        if r.get('error'): reasons.append(f'phase {r["index"]}: {r["error"]}')
        if len(r['primary_match'])<3: reasons.append(f'phase {r["index"]}: fewer than 3 complete primary windows')
        if not r['rt_active'] or min(r['rt_active'])<120 or any(r['rt_missing']):
            reasons.append(f'phase {r["index"]}: missing RT or no active-RT proof')
        if not r['fps'] or statistics.median(r['fps'])<18:
            reasons.append(f'phase {r["index"]}: cadence below 18 fps or absent')
    baseline = [v for r in rows if r['mode']=='static' and not r['on'] for v in r['primary_match']]
    floor = None
    if baseline:
        # The tolerance is the measured range of unchanged static repeats,
        # plus one printed 0.001 percentage-point code for rounding.
        floor = min(baseline)-(max(baseline)-min(baseline))-.001
        if floor<99.0: reasons.append('static control does not reach the established approximately 99.5% level')
        for r in rows:
            if r['on'] and r['primary_match'] and min(r['primary_match'])<floor:
                reasons.append(f'phase {r["index"]}: key-on primary match below static-derived floor {floor:.3f}%')
        for mode in ('slow','fast'):
            off = [v for r in rows if r['mode']==mode and not r['on'] for v in r['primary_match']]
            if not off or statistics.median(off)>=floor:
                reasons.append(f'{mode}: key-off positive control did not reproduce mismatch')
    else: reasons.append('no static key-off calibration')
    return {'gate':'PASS' if not reasons else 'FAIL', 'static_derived_floor_percent':floor,
            'reasons':reasons,'phases':rows}


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--log',type=Path,required=True)
    ap.add_argument('--runner',type=Path,required=True)
    ap.add_argument('--plan',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args()
    result=analyze(a.log.read_text(errors='replace'),a.runner.read_text(encoding='utf-8-sig',errors='replace'),
                   json.loads(a.plan.read_text()))
    result.update(log=str(a.log.resolve()),runner=str(a.runner.resolve()),plan=str(a.plan.resolve()))
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(result,indent=2))
    print(result['gate'], 'static-derived floor',result['static_derived_floor_percent'])
    for r in result['phases']:
        values=r['primary_match']
        print(r['mode'],'on' if r['on'] else 'off',f'match {min(values):.3f}-{max(values):.3f}%' if values else 'NO PRIMARY',
              'fps',round(statistics.median(r['fps']),2) if r['fps'] else None,'missing RT',r['rt_missing'])
    for reason in result['reasons']: print(reason)


if __name__=='__main__': main()
