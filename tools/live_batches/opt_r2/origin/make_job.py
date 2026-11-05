"""Prepare an offline-only inn origin OFF/ON/OFF/ON receipt."""
from copy import deepcopy
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot, readiness_budget
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import phase
from tools.live_batches.release_qual.make_final import retime_final
HERE=Path(__file__).resolve().parent
KEY='rt_origin_lazy_normal'
STATES=[('off-a',0),('on-a',1),('off-b',0),('on-b',1)]
TIMER='gi: origin pass GPU cost'

def main():
    snapshot=lint_snapshot.load()
    readiness_budget.configure([]) # New key transitions are explicitly unproven.
    source,old,_=flow.load_job(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')
    ultra=next(s for p,s in zip(old,flow.phase_states(source,old)) if p['label']=='ultra--static-a')
    state=dict(ultra,**{KEY:'0'},dev='1',detailed_logging='1',cpu_timing='0',rt_stats='0',
        rt_primary_check='0',ls_stats='0',rf_stats='0',rt_occ_stats='0',rt_stats_period_frames='60',
        rt_timing_frames='60',pan_prepare='1',pan_edge='1',pan_post='1',pan_capture='0',
        dump_images='0',dump_raw='0',dump_preview_f32='0',dump_shaders='0',qa_exposure_hold='0')
    target=dict(state,qa_exposure_hold='1')
    plan=[phase('warmup',29,set=assignments(state),wait_log=[r'\[shaders\] cache: prefetched ',r'\[auto\] scene ready'],wait_log_timeout=60)]
    for value in (1,0):
        plan.append(phase('warm-key-'+str(value),5,set=assignments(dict(target,**{KEY:str(value)}))))
    every=[]
    for i,(label,value) in enumerate(STATES):
        current=dict(target,**{KEY:str(value)},pan_capture='0')
        plan.append(phase('ready-'+label,3,set=assignments(current)))
        cost=label+'--static-a'
        plan.append(phase(cost,15,set=assignments(current),frames=180,settle_frames=0,stats_reset=True,wait_stats=[TIMER],wait_log_timeout=15))
        every.append(dict(name=label+' origin GPU',select=TIMER,match=TIMER+r' [\d.]+ ms/frame',windows=[cost],min=2,period_frames=60,sample_frames=60))
        every.append(dict(name=label+' current TLAS',select='rt passes: GPU cost',match=r'TLAS this frame [1-9]\d*, previous frame 0, none 0',windows=[cost],min=2,period_frames=60,sample_frames=60))
        plan.append(phase('energy-'+label,8,set=assignments(current),trace_at=[1]))
        serial=99620+i
        plan.append(dict(phase('qa-'+label,3,set=assignments(dict(current,pan_capture=str(serial)))),serial=serial,capture_at=.1))
        every.extend([dict(name=label+' float energy',select=r'\[trace\] frame \d+ written:',match=r'\[trace\] frame \d+ written:',windows=['energy-'+label],event=True,min=1),dict(name=label+' QA transport',select=rf'\[pan\] burst {serial} complete',match=rf'\[pan\] burst {serial} complete: 8 frames, \d+ samples, transport ok',windows=['qa-'+label],event=True,min=1)])
    plan.append(phase('restore',2,set=assignments(dict(target,**{KEY:'0'},qa_exposure_hold='0',pan_capture='0',pan_prepare='0',pan_edge='0',pan_post='0'))))
    job=deepcopy(source)
    name='opt-origin-inn'
    job.update(name=name,live_batch='opt-r2-origin',set=assignments(state),
       args=['-RealPresent','-Move','-MovePlan','tools/live_batches/opt_r2/origin/plans/'+name+'.json'],
       expect_every=every,hold=90,timeout=300,max_minutes=5,stop_on_dump=False,
       live_flow=dict(expected_fps=30,settings_latency_s=1,trace_stall_s=1),
       candidate=dict(key=KEY,states=STATES,serial_base=99620,preset='ultra'),
       purpose='Prepared offline only; never stage without explicit named-batch authorization.')
    job['patterns']=list(dict.fromkeys(job['patterns']+[TIMER,r'\[pan\]',r'\[trace\]',r'\[dump\]']))
    job['expect_runner']=[r'\[window-mode\].*visible_unfocused','^Run ended: done',rf'\[motion-plan\] complete phases={len(plan)}\b']
    job['post']='python tools/live_batches/opt_r2/origin/analyze.py --job tools/live_batches/opt_r2/origin/jobs/opt-origin-inn.json --log "{log}" --runner "{runner}" --out "{log}.origin.json"'
    job['post_gate']=True
    from tools.live_batches import pipeline_readiness
    for p,wanted in zip(plan,pipeline_readiness.requirements(job,plan)):
        for key in wanted:
            if key not in pipeline_readiness.TABLE:
                value=flow.settings(p['set'])[key]
                p.setdefault('wait_log',[]).append(r'\[cfg\] keys:.*\b'+key+'='+value+r'(?:\s|$)')
                if key==KEY:
                    p['wait_log'].append(r'gi: gi/rt_origin\.hlsl[^ ]* generation ')
    retime_final(job,plan)
    for p,current in zip(plan,flow.phase_states(job,plan)):
        if p['label'].endswith('--static-a'):
            # Three periods leave one complete readback-latency margin while
            # still requiring two complete origin timer samples per window.
            p['frames']=180
            p['seconds']=round(180/flow.fps_windows.expected(job,current)['fps']+.1,3)
            p['motion_end']=p['seconds']
        if p['label'].startswith('energy-'):
            p.update(seconds=max(p['seconds'],5),motion_end=max(p['seconds'],5))
        if p.get('wait_log'):
            p['wait_log_timeout']=60 if p['label']=='warmup' else 10
    job['estimated_phase_seconds']=round(sum(p['seconds'] for p in plan),3)
    budget=flow.duration_budget(job,plan)
    job['timeout']=max(300,round(budget['seconds']+budget['readiness_timeout_seconds']+1))
    flow.repair_known_forbids(job,plan)
    flow.write(HERE/'jobs'/f'{name}.json',job)
    flow.write(HERE/'plans'/f'{name}.json',plan)
    flow.write(HERE/'predicted.json',dict(status='READY_FOR_LIVE',job=name,key=KEY,shader_request_delta=0,
      baseline_origin_ms=1.18,expected_delta='ON avoids shading-normal texture fetch/decode on pixels with valid geometric normal; magnitude unmeasured.',
      cost_gate='Both ON median origin times must beat OFF median by more than OFF-repeat median spread.',
      energy_gate='Each ON float pre-post RGB/luminance mean equals OFF mean within OFF-repeat Student prediction band; family alpha 0.05 across eight comparisons.',
      qa_gate='Existing QA temporal detector: each ON versus each OFF, exclude animated geometry, reject camera drift >= 1 pixel.',
      states=STATES,preset='Ultra profiling settings with dev=1; native hair/scalp requested, physical hair origin off',
      launch_authorized=False,estimated_phase_seconds=job['estimated_phase_seconds'],hard_timeout_seconds=job['timeout'],
      disk='No raw float/PNG exports; dumps.json full-image float means and four K8 half-grid rendered edge bursts; target below 2 GB.',
      restoration=f'{KEY}=0, qa_exposure_hold=0, pan_capture=0, pan_prepare=0, pan_edge=0, pan_post=0'))
    # Add only new readiness query receipts, explicitly no invented measurements.
    existing={(r['save'],tuple(r['patterns']),tuple(r['fresh']),tuple(tuple(p) for p in r['changed']),r['first']) for r in snapshot['readiness']}
    for k,v in readiness_budget.CACHE.items():
        if k not in existing:
            snapshot['readiness'].append(dict(save=k[0],patterns=k[1],fresh=k[2],changed=k[3],first=k[4],estimate=v))
    flow.write(lint_snapshot.PATH,snapshot)

if __name__=='__main__': main()
