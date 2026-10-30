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
                   sdk_focus_disable_messages=[],sdk_transitions_interior=[],lighting_pose=[],faults=[],applied_counts=[],pass_gpu_timings=[])
        if row['index'] not in times: continue
        start=times[row['index']]; begin=start+15; end=start+step['seconds']-10
        row['begin']=begin; row['end']=end
        for line in log.splitlines():
            t=clock(line)
            if t is None or not begin<=t<=end: continue
            if re.search(r'RR evaluate failed|\[crash\] exception 0x|VK_ERROR_DEVICE_LOST|slDLSSGSetOptions.*: eError',line): row['faults'].append(line)
            m=re.search(r'rt applied \((\d+) frames\): sun (\d+) GI (\d+) local (\d+) reflections (\d+)',line)
            if m:
                frames,*counts=map(int,m.groups()); row['applied_counts'].append({'frames':frames,'counts':counts})
                for effect,count in zip(('sun','GI','local','reflections'),counts):
                    if frames>=120 and count>=frames: row['effect_evidence'][effect].append(line)
            if re.search(r'gi: trace pass .*GPU median|ls: local-light shadow pass GPU|rf: reflection pass GPU',line): row['pass_gpu_timings'].append(line)
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


def coverage_quality(log, runner, plan, status=None):
    """M1's existing-log gate; absent automatic pose/ROI proof can never pass M1."""
    log, runner = normalize_clocks(log, runner)
    normalized = [dict(step, fg=2, variant='native_coverage', view='normal') for step in plan]
    rows = phase_metrics(log, runner, normalized)
    times = starts(runner)
    invalid, quality_failures = [], []
    if not re.search(r'slDLSSGSetOptions\(mode on, frames to generate 1,.*: ok', log):
        invalid.append('FG2 one-generated-frame options were never successfully applied')
    if not status or status.get('result') != 'done' or not status.get('plan_completed'):
        invalid.append('missing successful completed-plan process status')
    if status and (status.get('observed_exit_code') is not None or not status.get('real_present') or
                   not status.get('foreground') or status.get('visible_unfocused')):
        invalid.append('real foreground presentation or normal owned cleanup unproven')
    if not re.search(r'\[motion-plan\] complete phases='+str(len(plan))+r'\b', runner):
        invalid.append('incomplete M1 plan')
    alpha_frames = None
    parsed = []
    sun_pattern = (r'sun current native-domain coverage \((\d+) frames\): certified (\d+) pixels, fallback (\d+), '
        r'scene unready (\d+); frame/camera/rays rejected (\d+)/(\d+)/(\d+), aux active (\d+), cone escape (\d+), '
        r'unknown candidate (\d+); actual selected/next ray samples (\d+), native white without ray (\d+)')
    for line in log.splitlines():
        t = clock(line)
        if t is None: continue
        header = re.search(r'\[alpha\] last (\d+) frames:', line)
        if header: alpha_frames = int(header[1])
        hair = re.search(r'\[alpha hair\] native coverage records (\d+); invalid/missing native state (\d+); '
            r'unsupported native interfaces total (\d+); retired bounds-invalid primitives (\d+)', line)
        scalp = re.search(r'\[alpha scalp\] native hard records (\d+); native camera dither records (\d+); invalid/missing native state (\d+)', line)
        sun = re.search(sun_pattern, line)
        insertion = re.search(r'\[sun current\] calls (\d+), recorded (\d+), same-submit inserted (\d+)', line)
        parsed.append((t, line, hair, scalp, sun, insertion, alpha_frames))
    for row, step in zip(rows, plan):
        label = f'phase {row["index"]}'
        settings = dict(pair.split('=', 1) for pair in step['set'].split(',') if '=' in pair)
        cost = bool(re.search(r'cost', step.get('label', '')))
        opaque = settings.get('rt_alpha_test') == '0'
        debug = settings.get('rt_debug', '0') != '0' or settings.get('ls_debug', '0') != '0'
        row.update(cost_window=cost, opaque_control=opaque, debug_window=debug,
                   native_hair=[], native_scalp=[], native_sun=[], sun_insertions=[], target_fs=[], capture_capacity=[], actual_alpha_modes=[],
                   coverage_primary_required=not (cost or opaque or debug))
        if row['index'] not in times:
            invalid.append(label+': missing phase clock'); continue
        begin, end = row['begin'], row['end']
        rates = row['fps']
        fps = min(rates) if rates else 0
        if row.get('error') or not rates or fps < 18:
            invalid.append(label+': incomplete real-time cadence (minimum 18 rendered fps)')
        if row['faults']: invalid.append(label+': API/RR/device fault')
        if not row['rt_active'] or min(row['rt_active']) < 120 or any(row['rt_missing']):
            invalid.append(label+': missing active RT')
        if (not row['sdk'] or not any(s['presented'] >= 1 for s in row['sdk']) or any(s['status'] or s['ratio'] <= 1 for s in row['sdk']) or
            not row['watch_ratios'] or min(row['watch_ratios']) <= 1 or row['sdk_focus_disable_messages'] or
            row['sdk_transitions_interior'] or row['host_toggles_interior']):
            invalid.append(label+': steady generated FG2 presentation unproven')
        if row['coverage_primary_required']:
            if len(row['primary_match']) < 3 or len(row['epoch']) < 3:
                invalid.append(label+': fewer than 3 complete primary/epoch windows')
            if any(e['frames'] < 120 or e['frames'] != e['scene_frames'] for e in row['epoch']):
                invalid.append(label+': current native epoch snapshots incomplete')
        for t, line, hair, scalp, sun, insertion, frames in parsed:
            if not begin <= t <= end: continue
            # Each counter window must start after settings/input settle, not in
            # the preceding phase. A minimum observed rendered rate is conservative.
            full_alpha = fps > 0 and frames and t >= times[row['index']]+15+frames/fps
            if hair and full_alpha: row['native_hair'].append(list(map(int, hair.groups())))
            if scalp and full_alpha: row['native_scalp'].append(list(map(int, scalp.groups())))
            if sun and fps > 0 and t >= times[row['index']]+max(15,step.get('motion_start',15))+int(sun[1])/fps:
                values = list(map(int, sun.groups()))
                row['native_sun'].append(dict(zip(('frames','certified','fallback','unready','frame_rejected',
                    'camera_rejected','rays_rejected','aux','cone','unknown','attempted_samples','white_without_ray'), values)))
            if insertion: row['sun_insertions'].append(list(map(int, insertion.groups())))
            capacity = re.search(r'\[geo\] RT capture, last (\d+) frames:.*alpha draws made opaque \(region full\) ([\d.]+), opaque draws dropped \(buffer full\) ([\d.]+)', line)
            if capacity and fps > 0 and t >= times[row['index']]+15+int(capacity[1])/fps:
                row['capture_capacity'].append({'frames':int(capacity[1]),'alpha_made_opaque':float(capacity[2]),'opaque_dropped':float(capacity[3])})
            if re.search(r'\[geo\] WARNING (alpha-tested region full|opaque region full)', line):
                invalid.append(label+': capture capacity overflow (raw draw warning)')

            actual_mode = re.search(r'rt stats \([0-9]+ frames, alpha test (on|off)\): primary-ray check', line)
            if actual_mode: row['actual_alpha_modes'].append(actual_mode[1])
            if 'this frame\'s shaders by triangles' in line:
                row['target_fs'].extend(re.findall(r'\b[0-9a-f]{16}\b', line))
            timing = re.search(r'rt passes: GPU cost .*TLAS this frame (\d+), previous frame (\d+), none (\d+)',line)
            if timing and (int(timing[1]) == 0 or int(timing[2]) != 0 or int(timing[3]) != 0):
                invalid.append(label+': stale/missing TLAS is not current-Sun proof')
        if not row['capture_capacity'] or any(v['alpha_made_opaque'] or v['opaque_dropped'] for v in row['capture_capacity']):
            invalid.append(label+': missing or overflowing capture capacity window')
        if cost: continue  # Report consumer cost without requiring disabled primary diagnostics.
        if not row['actual_alpha_modes'] or any(mode != ('off' if opaque else 'on') for mode in row['actual_alpha_modes']):
            invalid.append(label+': requested alpha coverage control was not observed')
        if not row['native_hair'] or any(v[0] == 0 or any(v[1:]) for v in row['native_hair']):
            invalid.append(label+': missing/invalid native Hair coverage')
        if not row['native_scalp'] or any(v[0]+v[1] == 0 or v[2] for v in row['native_scalp']):
            invalid.append(label+': missing/invalid native Scalp coverage')
        if not {'5a03e5b678b2d040','9e77575a9f377ec5'} <= set(row['target_fs']):
            invalid.append(label+': required retained target FS unproven')
        if len(row['sun_insertions']) < 2 or row['sun_insertions'][-1][2] <= row['sun_insertions'][0][2]:
            invalid.append(label+': no current-phase private Sun insertion increase')
        if not row['native_sun']:
            invalid.append(label+': no complete native Sun coverage window')
        for sun in row['native_sun']:
            sun['certified_ray_pixels'] = sun['certified']-sun['white_without_ray']
            if (sun['certified_ray_pixels'] <= 0 or sun['attempted_samples'] == 0 or sun['unready'] or
                sun['frame_rejected'] or sun['camera_rejected'] or sun['rays_rejected']):
                invalid.append(label+': native-white, all-unknown or unready Sun never passes')
    calibration = next((r for r in rows if r['kind']=='static' and r['coverage_primary_required']), None)
    values = calibration['primary_match'] if calibration else []
    floor = min(values)-(max(values)-min(values))-.001 if len(values) >= 3 else None
    if floor is None or floor < 99:
        invalid.append('missing stable native static calibration near 99.5%')
    rotations = [r for r in rows if r['kind']=='rotate' and not r['cost_window']]
    static_motion = max((e['motion_max'] for e in calibration['epoch']), default=0)+.000001 if calibration else .000001
    for row in rotations:
        if not row['epoch'] or statistics.median(e['motion_mean'] for e in row['epoch']) <= static_motion:
            invalid.append(f'phase {row["index"]}: actual rotation above static noise unproven')
        if floor is not None and row['primary_match'] and min(row['primary_match']) < floor:
            quality_failures.append(f'phase {row["index"]}: moving native primary below static-derived floor {floor:.3f}%')
    if rotations:
        for speed in {r['speed_px_s'] for r in rotations}:
            pair = [r for r in rotations if r['speed_px_s']==speed]
            yaw = [statistics.median(e['signed_yaw'] for e in r['epoch']) for r in pair if r['epoch']]
            if {r['direction'] for r in pair} != {-1,1} or len(yaw) != 2 or min(yaw)*max(yaw) >= 0:
                invalid.append(f'rotation speed {speed}: both actual directions unproven')
        for direction in (-1,1):
            ordered = sorted((r for r in rotations if r['direction']==direction), key=lambda r:r['speed_px_s'])
            measured = [statistics.median(e['motion_mean'] for e in r['epoch'])*r['fps_median'] for r in ordered if r['epoch'] and r['fps_median']]
            if len(measured) != len(ordered) or len(measured)<2 or any(b<=a for a,b in zip(measured,measured[1:])):
                invalid.append(f'rotation direction {direction}: increasing actual speeds unproven')
    invalid = list(dict.fromkeys(invalid))
    return {'gate':'FAIL' if quality_failures and not invalid else 'INVALID',
            'log_gate':'INVALID' if invalid else 'FAIL' if quality_failures else 'PASS',
            'invalid':invalid, 'quality_failures':quality_failures, 'static_floor_percent':floor,
            'pose_roi':{'gate':'UNAVAILABLE','reason':'Automatic Test1 dialogue pose and face/distant ROI assertion is absent; global counts cannot replace it'},
            'phases':rows,
            'limits':'Opaque coverage control may change energy. Cost rows exclude primary diagnostics; private Sun build cost is outside the existing consumer interval. No complete M1 PASS without automatic pose/ROI evidence.'}


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
    p.add_argument('--coverage',action='store_true',help='M1 native coverage log gate; missing automatic pose/ROI remains INVALID')
    p.add_argument('--status',type=Path)
    a=p.parse_args()
    log=a.log.read_text(errors='replace'); runner=a.runner.read_text(encoding='utf-8-sig',errors='replace'); plan=json.loads(a.plan.read_text())
    if a.coverage:
        status=json.loads(a.status.read_text(encoding='utf-8-sig')) if a.status and a.status.is_file() else None
        result=coverage_quality(log,runner,plan,status)
    else: result=quality(log,runner,plan)
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_text(json.dumps(result,indent=2)+'\n')
    if a.coverage:
        print('M1 COVERAGE GATE',result['gate'],'floor',result['static_floor_percent'])
        print('native log gate',result['log_gate'],'automatic pose/ROI',result['pose_roi']['gate'])
        for reason in result['invalid']+result['quality_failures']: print(reason)
    else:
        print('FG 2 DECIDING GATE',result['gate'],'reproduced',result['FG_on_reproduced'],'floor',result['static_floor_percent'])
        for name,value in result['candidates'].items(): print(name,value)
    raise SystemExit(0 if result['gate']=='PASS' else 1)


if __name__=='__main__': main()
