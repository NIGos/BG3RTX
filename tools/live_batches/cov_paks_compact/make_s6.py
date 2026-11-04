"""S6 publication A/B; extends cp4 without replacing its retained camp evidence."""
import copy
import json
from pathlib import Path
from tools.live_batches.cov_paks_compact.make_jobs import JOB
from tools.live_batches.live_flow import tighten_files, write
HERE=Path(__file__).resolve().parent

def main():
    job=copy.deepcopy(JOB)
    job.update(name='cp5-compact-s6-publication',save='Adamantine Forge',live_batch='cov-paks-compact',timeout=850,hold=90,
               purpose='S6 publication and same-run image/energy/cost qualification. Zero compatible publication rejects this candidate; UV matches alone never qualify.',
               args=['-RealPresent','-Move','-MovePlan','captures/live_batches/cov_paks_compact/plans/compact_s6.json'])
    state=dict(x.split('=',1) for x in job['set'].split(','))
    state.update(rt_hit_material_paks='0',dump_preview_f32='1',rt_native_tileset='1',rt_stats='0',rt_timing_frames='300')
    job['set']=','.join(k+'='+v for k,v in state.items())
    job['live_flow']=dict(expected_fps=30,settings_latency_s=1,trace_stall_s=3,startup_only=['rt_xfb_mb'])
    def phase(label,seconds,on=0,stats=0,shot=False):
        p=dict(label=label,kind='static',speed='static',seconds=seconds,repeat_ms=0,scan=16,capture_at=0,serial=0,motion_start=seconds,motion_end=seconds,pixels_per_second=0,direction=0,set=f'rt_hit_material_paks={on},rt_stats={stats},qa_exposure_hold={int(not label.startswith("warm"))}')
        if shot:p['trace_at']=[5]
        return p
    plan=[phase('warmup',30,1),phase('warm-off',10)]
    for label,on in [('off-a',0),('on',1),('off-b',0),('on-repeat',1),('off-c',0)]:
        if on:plan.append(phase('diag-'+label,35,on,1))
        plan += [phase('shot-'+label,12,on,shot=True),phase('cost-'+label,35,on)]
    plan.append(phase('restore',5));plan[-1]['set']+=',qa_exposure_hold=0'
    costs=[p['label'] for p in plan if p['label'].startswith('cost-')]
    diags=[p['label'] for p in plan if p['label'].startswith('diag-')]
    source=json.loads((HERE.parent/'gpu_opt1/jobs/go1-camp.json').read_text())
    job['expect_every']=[]
    for gate in source['expect_every']:
        if gate['name'].startswith(('scene wait:','capacity:')) or gate['select'] in ('gi: trace pass','gi: GPU cost','rt passes: GPU cost'):
            gate=copy.deepcopy(gate);gate['windows']=costs;job['expect_every'].append(gate)
    job['expect_every'] += [dict(name='full witness and publication in every ON',select=r'\[hit material paks join\]',match=r'attempts [1-9]\d* unique [1-9]\d* .*draws [1-9]\d* witness_ok [1-9]\d* witness_mismatch \d+ budget_deferred \d+ published_records [1-9]\d*',windows=diags,min=1,period_frames=120),
        dict(name='recipe cull resource and verified basis publication every ON',select=r'\[hit material paks\] frame',match=r'enabled 1: exact [1-9]\d*/[1-9]\d* attempts; records [1-9]\d* basis ready [1-9]\d*;',windows=diags,min=1,period_frames=120)]
    job['patterns']+=['gi: background builds','building in the background']
    job['expect']=[r'\[auto\] scene ready',r'\[hit material paks\] loaded 40983 candidates',r'\[trace\] frame \d+ written',r'\[rrpost\] exposure held']
    job['expect_runner']=[r'\[window-mode\].*visible_unfocused',r'^Run ended: done',rf'\[motion-plan\] complete phases={len(plan)}\b']+[rf"\[motion-trace\] phase={i} label={p['label']} repeat=0" for i,p in enumerate(plan) if p.get('trace_at')]
    job['post']='py -3 -m tools.m4_promotion_gate "{log}" --batch cov_paks_compact --pool run'
    job['post_gate']=True
    job['estimated_phase_seconds']=sum(p['seconds'] for p in plan)
    write(HERE/'plans/compact_s6.json',plan);write(HERE/'jobs/cp5-compact-s6-publication.json',job)
    tighten_files(HERE, job_names=["cp5-compact-s6-publication"])
    path=HERE/"plans/compact_s6.json"
    plan=json.loads(path.read_text())
    for i,p in enumerate(plan):
        if p.get("wait_log"):p["wait_log_timeout"]=180 if i==0 else 60
    write(path,plan)
    target=HERE/'jobs/cp5-compact-s6-publication.json'
    final=json.loads(target.read_text());final['timeout']=850;write(target,final)
if __name__=='__main__':main()
