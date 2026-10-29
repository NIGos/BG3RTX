"""Qualify the unattended job/exit path before any rotation quality run."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
from .rebase_gate import analyze


def qualify(log,runner,plan,status):
    phases=analyze(log,runner,plan)['phases']
    reasons=[]
    if status.get('result')!='done' or not status.get('plan_completed'):
        reasons.append('game/job did not complete the preflight')
    if status.get('observed_exit_code') is not None:
        reasons.append('game exited before scheduled cleanup')
    unfocused=status.get('visible_unfocused',False)
    if not status.get('real_present') or not (status.get('foreground') or unfocused):
        reasons.append('foreground or verified visible-unfocused real presentation was not active')
    if unfocused and (status.get('foreground') or status.get('unfocused_checks',0)<1 or
        status.get('unfocused_visible_min_pixels',0)<=0 or status.get('unfocused_violation')):
        reasons.append('unfocused presentation lacks visibility/focus proof or had a violation')
    if not re.search(r'\[motion-plan\] complete phases='+str(len(plan))+r'\b',runner):
        reasons.append('missing complete-plan marker')
    if re.search(r'\[crash\] exception 0x[0-9a-f]+|device lost|VK_ERROR_DEVICE_LOST',log,re.I):
        reasons.append('exception/device fault needs investigation before rotation')
    if '[crash] exception diagnostics active independently of FG; minidump armed' not in log:
        reasons.append('FG-independent exception/minidump capture not armed')
    # Normalization/dot-product roundoff in the CPU's double-precision motion
    # calculation, plus the six-decimal printed angle code.
    static_angle_bound=math.degrees(math.acos(1-32*math.ulp(1.0)))+.000001
    profiles=[]
    for row,step in zip(phases,plan):
        label=f'phase {row["index"]}'
        if not row['rt_active'] or min(row['rt_active'])<120 or any(row['rt_missing']): reasons.append(label+': missing active RT')
        if not row['fps'] or statistics.median(row['fps'])<18: reasons.append(label+': below 18 fps or missing cadence')
        if 'rt_primary_check=1' in step['set']:
            if len(row['primary_match'])<3 or min(row['primary_match'],default=0)<99:
                reasons.append(label+': incomplete or unstable static primary control')
            if len(row['epoch'])<3 or any(e['frames']<120 or e['frames']!=e['scene_frames'] for e in row['epoch']):
                reasons.append(label+': incomplete GPU epoch samples')
            for e in row['epoch']:
                if e['motion_max']>static_angle_bound: reasons.append(label+': static orientation moved above precision'); break
                if e.get('origin_motion_max',0)>e.get('origin_precision_bound',0)+.000001:
                    reasons.append(label+': static origin moved beyond native FP32 precision'); break
        profiles.append({'phase':row['index'],'settings':step['set'],
            'fps_median':statistics.median(row['fps']) if row['fps'] else None,
            'rt_interval_ms_median':statistics.median(row['gpu_ms']) if row['gpu_ms'] else None,
            'primary_match':row['primary_match']})
    cost_comparison={}
    if len(profiles)==4 and all(p['rt_interval_ms_median'] is not None for p in profiles):
        production=(profiles[0]['rt_interval_ms_median']+profiles[3]['rt_interval_ms_median'])/2
        cost_comparison={'production_repeat_ms':[profiles[0]['rt_interval_ms_median'],profiles[3]['rt_interval_ms_median']],
            'primary_plus_epoch_overhead_ms':profiles[1]['rt_interval_ms_median']-production,
            'static_rebase_delta_ms':profiles[2]['rt_interval_ms_median']-profiles[1]['rt_interval_ms_median'],
            'caveat':'Same static run; primary-plus-epoch delta includes both diagnostics and source-pose snapshot work.'}
    return {'gate':'PASS' if not reasons else 'FAIL','reasons':reasons,'profiles':profiles,'cost_comparison':cost_comparison,
            'note':'RT interval includes pre-lighting GI/AO/sun and primary diagnostics when enabled; it is not an isolated sun cost.'}


def main():
    p=argparse.ArgumentParser()
    for name in ('log','runner','plan','status','out'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    report=qualify(a.log.read_text(errors='replace'),a.runner.read_text(encoding='utf-8-sig',errors='replace'),
                   json.loads(a.plan.read_text()),json.loads(a.status.read_text(encoding='utf-8-sig')))
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_text(json.dumps(report,indent=2)+'\n')
    print('ROTATION PREFLIGHT',report['gate'])
    for row in report['profiles']: print(row)
    for reason in report['reasons']: print(reason)
    raise SystemExit(0 if report['gate']=='PASS' else 1)


if __name__=='__main__': main()
