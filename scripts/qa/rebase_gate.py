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
    rotation = any(step.get('kind')=='rotate' for step in plan)
    starts = {}
    for line in runner.splitlines():
        match = re.search(r'\[pan-plan\] window (\d+)',line)
        if match and clock(line) is not None:
            starts[int(match[1])] = clock(line)
    rows = []
    for index,step in enumerate(plan):
        on = 'rt_scene_rebase=1' in step['set']
        kind = step.get('kind') or ('static' if not step['repeat_ms'] else 'pan')
        mode = ('rotate_'+step['speed']+'_'+str(step['direction'])) if kind=='rotate' else 'static' if not step['repeat_ms'] else 'slow' if step['repeat_ms']==100 else 'fast'
        row = {'index':index,'mode':mode,'on':on,'primary_match':[], 'miss':[], 'rt_missing':[],
               'rt_active':[], 'fps':[], 'gpu_ms':[],'epoch':[],'kind':kind,
               'speed_px_s':step.get('pixels_per_second',0),'direction':step.get('direction',0)}
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
            epoch = re.search(r'rt epoch.*GPU camera frames (\d+), scene snapshots (\d+); camera motion mean ([\d.eE+-]+) deg/frame, max ([\d.eE+-]+) deg/frame, signed yaw ([\d.eE+-]+) deg/frame; scene rotation max ([\d.eE+-]+) deg, translation max ([\d.eE+-]+) m; pool hit match ([\d.]+)% of (\d+), soup hit match ([\d.]+)% of (\d+)',line)
            if epoch:
                v=epoch.groups()
                row['epoch'].append({'frames':int(v[0]),'scene_frames':int(v[1]),'motion_mean':float(v[2]),
                    'motion_max':float(v[3]),'signed_yaw':float(v[4]),'scene_rotation_max':float(v[5]),
                    'scene_translation_max':float(v[6]),'pool_match':float(v[7]),'pool_hits':int(v[8]),
                    'soup_match':float(v[9]),'soup_hits':int(v[10])})
                translation=re.search(r'camera translation motion max ([\d.eE+-]+) m/frame, precision bound ([\d.eE+-]+) m/frame',line)
                if translation:
                    row['epoch'][-1].update(origin_motion_max=float(translation[1]),origin_precision_bound=float(translation[2]))
        rows.append(row)
    reasons = []
    moving_modes = sorted({r['mode'] for r in rows if r['kind']=='rotate'}) if rotation else ['slow','fast']
    expected = {(mode,on) for mode in ['static',*moving_modes] for on in (False,True)}
    if {(r['mode'],r['on']) for r in rows} != expected: reasons.append('missing static/moving off/on phase')
    if rotation:
        if not re.search(r'\[motion-plan\] complete phases='+str(len(plan))+r'\b',runner): reasons.append('rotation plan did not complete')
        if {r['direction'] for r in rows if r['kind']=='rotate'} != {-1,1}: reasons.append('both rotation directions are required')
        if len({r['speed_px_s'] for r in rows if r['kind']=='rotate'})<2: reasons.append('multiple rotation speeds are required')
    for r in rows:
        if r.get('error'): reasons.append(f'phase {r["index"]}: {r["error"]}')
        if len(r['primary_match'])<3: reasons.append(f'phase {r["index"]}: fewer than 3 complete primary windows')
        if not r['rt_active'] or min(r['rt_active'])<120 or any(r['rt_missing']):
            reasons.append(f'phase {r["index"]}: missing RT or no active-RT proof')
        if not r['fps'] or statistics.median(r['fps'])<18:
            reasons.append(f'phase {r["index"]}: cadence below 18 fps or absent')
        if rotation and (len(r['epoch'])<3 or any(e['frames']<120 or e['frames']!=e['scene_frames'] for e in r['epoch'])):
            reasons.append(f'phase {r["index"]}: missing GPU camera/scene epoch proof')
    calibration = next((r for r in rows if r['mode']=='static' and not r['on']),None)
    baseline = calibration['primary_match'] if calibration else []
    floor = None
    if baseline:
        # The tolerance is the measured range of unchanged static repeats,
        # plus one printed 0.001 percentage-point code for rounding.
        floor = min(baseline)-(max(baseline)-min(baseline))-.001
        if floor<99.0:
            reasons.append('invalid static calibration; expected approximately 99.2% match')
            floor=None
        for r in rows:
            if floor is not None and r['on'] and r['primary_match'] and min(r['primary_match'])<floor:
                reasons.append(f'phase {r["index"]}: key-on primary match below static-derived floor {floor:.3f}%')
        for mode in moving_modes:
            off = [v for r in rows if r['mode']==mode and not r['on'] for v in r['primary_match']]
            if floor is not None and (not off or statistics.median(off)>=floor):
                reasons.append(f'{mode}: key-off positive control did not reproduce mismatch')
    else: reasons.append('no static key-off calibration')
    if rotation:
        static_epochs=calibration['epoch'] if calibration else []
        # Six decimal places are printed. The motion floor is the maximum
        # measured static motion plus one printed code, never tuned to pass.
        motion_floor=max((e['motion_max'] for e in static_epochs),default=0)+.000001
        for r in rows:
            if r['kind']=='rotate' and (not r['epoch'] or statistics.median(e['motion_mean'] for e in r['epoch'])<=motion_floor):
                reasons.append(f'phase {r["index"]}: requested mouse drag did not prove rotation above static noise')
        for on in (False,True):
            for speed in sorted({r['speed_px_s'] for r in rows if r['kind']=='rotate'}):
                pair=[r for r in rows if r['kind']=='rotate' and r['speed_px_s']==speed and r['on']==on]
                yaw=[statistics.median(e['signed_yaw'] for e in r['epoch']) for r in pair if r['epoch']]
                if len(yaw)!=2 or yaw[0]*yaw[1]>=0: reasons.append(f'rotation speed {speed}, on={on}: opposite measured directions unproven')
            for direction in (-1,1):
                speed_rows=sorted([r for r in rows if r['kind']=='rotate' and r['direction']==direction and r['on']==on],key=lambda r:r['speed_px_s'])
                measured=[statistics.median(e['motion_mean'] for e in r['epoch'])*statistics.median(r['fps']) for r in speed_rows if r['epoch'] and r['fps']]
                if len(measured)!=len(speed_rows) or any(b<=a for a,b in zip(measured,measured[1:])):
                    reasons.append(f'rotation direction {direction}, on={on}: increasing actual speeds unproven')
    return {'gate':'PASS' if not reasons else 'FAIL', 'static_derived_floor_percent':floor,
            'main_gate':'rotation' if rotation else 'pan','reasons':reasons,'phases':rows}


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
    raise SystemExit(0 if result['gate']=='PASS' else 1)


if __name__=='__main__': main()
