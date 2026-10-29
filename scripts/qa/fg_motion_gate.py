"""One-run FG matrix. FG 2 decides P1; FG 0 is reported as reference only."""
import argparse
import json
from pathlib import Path
import re
import statistics
from .rebase_gate import analyze as primary_rows, clock
from .fg_log_audit import audit


def normalize_clocks(log,runner):
    # Jobs can cross midnight. Keep app/runner wall clocks on a common day;
    # small cross-thread timestamp reordering is not a day rollover.
    def unwrap(text):
        previous=None; day=0; out=[]
        for line in text.splitlines():
            m=re.search(r'(\d\d):(\d\d):(\d\d)\.(\d{3})',line)
            if m:
                t=clock(line)+day
                if previous is not None and t<previous-12*3600: day+=24*3600; t+=24*3600
                previous=max(previous or t,t)
                stamp=f'{int(t)//3600:02}:{int(t)//60%60:02}:{int(t)%60:02}.{m[4]}'
                line=line[:m.start()]+stamp+line[m.end():]
            out.append(line)
        return '\n'.join(out)
    return unwrap(log),unwrap(runner)


def starts(runner):
    return {int(m[1]):clock(line) for line in runner.splitlines()
            if (m:=re.search(r'\[pan-plan\] window (\d+)',line)) and clock(line) is not None}


def phase_metrics(log,runner,plan):
    log,runner=normalize_clocks(log,runner)
    rows=primary_rows(log,runner,plan)['phases']; times=starts(runner); events=audit(log)
    for row,step in zip(rows,plan):
        row.update(fg=step['fg'],variant=step.get('variant','baseline'),view=step.get('view','normal'))
        row.update(watch_ratios=[],sdk=[],effect_evidence={k:[] for k in ('sun','local','GI','reflections','RR')},
                   fps_median=None,match_median=None,displayed_rendered_median=None,
                   host_toggles_interior=0,host_toggles_per_minute=0,options_calls_full_phase=0,host_toggles_full_phase=0,
                   sdk_focus_disable_messages=[],sdk_transitions_interior=[],lighting_pose=[],faults=[])
        if row['index'] not in times: continue
        start=times[row['index']]; begin=start+15; end=start+step['seconds']-10
        row['begin']=begin; row['end']=end
        for line in log.splitlines():
            t=clock(line)
            if t is None or not begin<=t<=end: continue
            if re.search(r'RR evaluate failed|\[crash\] exception 0x|VK_ERROR_DEVICE_LOST|slDLSSGSetOptions.*: eError',line): row['faults'].append(line)
            m=re.search(r'\[watch\] presents (\d+) \(displayed (\d+)\)',line)
            if m and int(m[1]): row['watch_ratios'].append(int(m[2])/int(m[1]))
            m=re.search(r'DLSS-G frame (\d+): status (0x[\da-f]+), presented per app frame (\d+), displayed/rendered over the last (\d+) frames ([\d.]+)',line)
            if m: row['sdk'].append({'frame':int(m[1]),'status':int(m[2],16),'presented':int(m[3]),'ratio':float(m[5])})
            m=re.search(r'lighting forward \[([^]]+)\], origin \[([^]]+)\]',line)
            if m: row['lighting_pose'].append([float(v.strip()) for group in m.groups() for v in group.split(',')])
            signatures={'sun':r'rt stats .*material classes:', 'local':r'ls stats .*px/frame; lights per pixel:',
                        'GI':r'rt stats .*hybrid GI [\d.]+M rays/frame', 'reflections':r'rf stats .*px/frame',
                        'RR':r'\[ngx\] gpu RR:.*evaluate [\d.]+ ms'}
            for effect,pattern in signatures.items():
                if re.search(pattern,line): row['effect_evidence'][effect].append(line)
        row['options_calls_full_phase']=sum(start<=r['seconds']<start+step['seconds'] for r in events['decisions'])
        row['host_toggles_full_phase']=sum(r['transition'] and start<=r['seconds']<start+step['seconds'] for r in events['decisions'])
        toggles=[r for r in events['decisions'] if r['transition'] and begin<=r['seconds']<=end]
        row['host_toggles_interior']=len(toggles)
        row['host_toggles_per_minute']=len(toggles)*60/max(1,end-begin)
        row['sdk_transitions_interior']=[r for r in events['sdk_transitions'] if begin<=r['seconds']<=end]
        row['sdk_focus_disable_messages']=[r for r in events['sdk_disabled_reasons'] if begin<=clock(r['text'])<=end]
        row['match_median']=statistics.median(row['primary_match']) if row['primary_match'] else None
        row['fps_median']=statistics.median(row['fps']) if row['fps'] else None
        row['displayed_rendered_median']=statistics.median(row['watch_ratios']) if row['watch_ratios'] else None
        # A 120-frame primary window spans 120/rendered_fps seconds. Association
        # with a mode toggle is observational, never a claim of causation.
        associated={False:[],True:[]}
        if row['fps_median']:
            for line in log.splitlines():
                t=clock(line); m=re.search(r'primary-ray check match ([\d.]+)%',line)
                if m and t is not None and begin<=t<=end:
                    recent=any(t-120/row['fps_median']<=r['seconds']<=t for r in events['decisions'] if r['transition'])
                    associated[recent].append(float(m[1]))
        row['toggle_match_association']={('with_toggle' if k else 'without_toggle'):
            {'windows':len(v),'mean_match':statistics.mean(v) if v else None} for k,v in associated.items()}
    return rows


def quality(log,runner,plan):
    rows=phase_metrics(log,runner,plan); invalid=[]
    if {s['fg'] for s in plan}!={0,2}: invalid.append('both FG 0 reference and FG 2 deciding configurations required')
    if not re.search(r'\[motion-plan\] complete phases='+str(len(plan))+r'\b',runner): invalid.append('incomplete motion matrix')
    matrix={(s['kind'],s.get('speed'),s.get('direction',0),s.get('variant','baseline'),s['fg']) for s in plan}
    for kind,speed,direction,variant,fg in matrix:
        if (kind,speed,direction,variant,2-fg) not in matrix: invalid.append('unpaired FG configuration')
    for r in rows:
        label=f'phase {r["index"]}'
        if r.get('error') or len(r['primary_match'])<3: invalid.append(label+': primary evidence incomplete')
        if r['faults']: invalid.append(label+': API/RR/device fault')
        if not r['rt_active'] or min(r['rt_active'])<120 or any(r['rt_missing']): invalid.append(label+': missing RT')
        if len(r['epoch'])<3 or any(e['frames']<120 or e['scene_frames']!=e['frames'] for e in r['epoch']): invalid.append(label+': missing GPU camera evidence')
        if r['fg']==2:
            for effect,evidence in r['effect_evidence'].items():
                if not evidence: invalid.append(label+': '+effect+' not proved applied while FG on')
            if not r['fps_median'] or r['fps_median']<18: invalid.append(label+': FG-on cadence below 18 fps')
            if not r['watch_ratios'] or statistics.median(r['watch_ratios'])<=1 or not any(s['presented']>=2 and s['status']==0 for s in r['sdk']):
                invalid.append(label+': FG requested but real generated presentation not proved')
            if r['sdk_focus_disable_messages']: invalid.append(label+': FG lost focus during deciding window')
        elif not r['watch_ratios'] or statistics.median(r['watch_ratios'])>1:
            invalid.append(label+': FG-off reference presentation unproved or still generating')
    calibration=next((r for r in rows if r['fg']==2 and r['kind']=='static' and r['variant']=='baseline'),None)
    values=calibration['primary_match'] if calibration else []
    floor=min(values)-(max(values)-min(values))-.001 if values else None
    if floor is None or floor<99: invalid.append('invalid FG-on static calibration near 99.2%')
    static_motion=max((e['motion_max'] for e in calibration['epoch']),default=0)+.000001 if calibration else .000001
    rotations=[r for r in rows if r['kind']=='rotate']
    for r in rotations:
        if not r['epoch'] or statistics.median(e['motion_mean'] for e in r['epoch'])<=static_motion:
            invalid.append(f'phase {r["index"]}: input did not prove actual GPU rotation')
    for fg in (0,2):
        for variant in {r['variant'] for r in rotations}:
            speeds=sorted({r['speed_px_s'] for r in rotations if r['fg']==fg and r['variant']==variant})
            if len(speeds)<2: invalid.append('multiple rotation speeds required')
            for speed in speeds:
                pair=[r for r in rotations if r['fg']==fg and r['variant']==variant and r['speed_px_s']==speed]
                yaw=[statistics.median(e['signed_yaw'] for e in r['epoch']) for r in pair if r['epoch']]
                if {r['direction'] for r in pair}!={-1,1} or len(yaw)!=2 or yaw[0]*yaw[1]>=0: invalid.append('opposite GPU rotation directions unproved')
            for direction in (-1,1):
                ordered=sorted([r for r in rotations if r['fg']==fg and r['variant']==variant and r['direction']==direction],key=lambda r:r['speed_px_s'])
                actual=[statistics.median(e['motion_mean'] for e in r['epoch'])*r['fps_median'] for r in ordered if r['epoch'] and r['fps_median']]
                if len(actual)!=len(ordered) or any(b<=a for a,b in zip(actual,actual[1:])): invalid.append('increasing actual rotation speeds unproved')
    for r in rows:
        if r['kind']=='pan' and (not r['epoch'] or not any(e.get('origin_motion_max',0)>e.get('origin_precision_bound',0)+.000001 for e in r['epoch'])):
            invalid.append(f'phase {r["index"]}: actual camera translation unproved')
    baseline=[r for r in rows if r['fg']==2 and r['kind']!='static' and r['variant']=='baseline']
    reproduced=floor is not None and any(r['match_median'] is not None and r['match_median']<floor for r in baseline)
    candidates={}
    for variant in ('rebase','current_destroy_only'):
        selected=[r for r in rows if r['fg']==2 and r['variant']==variant]
        reasons=list(invalid)
        if not reproduced: reasons.append('FG-on baseline did not reproduce mismatch')
        baseline_modes={r['mode'] for r in baseline}
        if {r['mode'] for r in selected if r['kind']!='static'}!=baseline_modes: reasons.append('candidate lacks paired moving coverage')
        if not selected or any(not r['primary_match'] or (floor is not None and min(r['primary_match'])<floor) for r in selected):
            reasons.append('candidate does not reach FG-on static level')
        if variant=='current_destroy_only':
            before=sum(r['host_toggles_interior'] for r in baseline)
            after=sum(r['host_toggles_interior'] for r in selected if r['kind']!='static')
            if before<=after: reasons.append('no measured reduction in FG-on interior toggles')
        candidates[variant]={'gate':'PASS' if not reasons else 'FAIL','reasons':reasons}
    return {'gate':'PASS' if any(c['gate']=='PASS' for c in candidates.values()) else 'FAIL',
        'deciding_fg':2,'reference_fg':0,'static_floor_percent':floor,'FG_on_reproduced':reproduced,
        'invalid':invalid,'candidates':candidates,'phases':rows,
        'limits':'FG 0 quality does not decide. Toggle association does not prove Reflex/TLAS causation; native instance-row epoch remains unproved.'}


def main():
    p=argparse.ArgumentParser()
    for name in ('log','runner','plan','out'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args(); result=quality(a.log.read_text(errors='replace'),a.runner.read_text(encoding='utf-8-sig',errors='replace'),json.loads(a.plan.read_text()))
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('FG 2 DECIDING GATE',result['gate'],'reproduced',result['FG_on_reproduced'],'floor',result['static_floor_percent'])
    for name,value in result['candidates'].items(): print(name,value)
    raise SystemExit(0 if result['gate']=='PASS' else 1)


if __name__=='__main__': main()
