"""Generate offline gpu-opt1 drafts; never queues or launches the game."""
import json
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
KEYS = ('rt_compact_hit_state', 'rt_alpha_metadata_cache', 'rt_water_empty_tiles')
def phase(label, seconds, state, shot=False):
    p = dict(label=label, kind='static', speed='static', seconds=seconds, repeat_ms=0, scan=16,
             capture_at=0, serial=0, motion_start=seconds, motion_end=seconds, pixels_per_second=0, direction=0,
             set=','.join(f'{key}={int(key in state)}' for key in KEYS)+',qa_exposure_hold=1,rt_stats=0,rt_primary_check=0')
    if shot: p['trace_at']=[5]
    return p

def main():
    for sub in ('jobs','plans'): (HERE/sub).mkdir(exist_ok=True)
    states=[('off-a',()),('compact',(KEYS[0],)),('off-b',()),('alpha',(KEYS[1],)),('off-c',()),('water',(KEYS[2],)),('off-d',())]
    plan=[phase('warmup',24,KEYS)]+[phase('warm-'+key,10,(key,)) for key in KEYS]+[phase('warm-off',10,())]
    for label,state in states:
        plan += [phase('shot-'+label,11,state,True),phase('cost-'+label,25,state)]
    plan[0]['set'] = plan[0]['set'].replace('qa_exposure_hold=1','qa_exposure_hold=0')
    plan += [phase('restore',5,())]; plan[-1]['set']+=',qa_exposure_hold=0'
    (HERE/'plans/gpu_opt1.json').write_text(json.dumps(plan,indent=1)+'\n',encoding='utf-8')
    costs=[p['label'] for p in plan if p['label'].startswith('cost-')]
    measured=[p['label'] for p in plan if p['label'].startswith(('shot-','cost-'))]
    for n,save,source in [('go1-camp','Campsite - 27h','m5p3-camp-fg2.json'),('go2-s8','Water Queens House','m5p1-s8-fg2.json')]:
        j=json.loads((HERE.parent/'m5_promotion/jobs'/source).read_text())
        j.update(name=n,save=save,live_batch='gpu-opt1',window_mode='visible_unfocused_fg',timeout=sum(p['seconds'] for p in plan)+300,
                 live_flow=dict(expected_fps=30,settings_latency_s=1,trace_stall_s=1),
                 estimated_phase_seconds=sum(p['seconds'] for p in plan),set=j['set']+',dump_shaders=0,dump_preview_f32=1,'+','.join(k+'=0' for k in KEYS),
                 args=['-RealPresent','-Move','-MovePlan','captures/live_batches/gpu_opt1/plans/gpu_opt1.json'])
        j['expect']=[r'\[auto\] scene ready',r'gi: trace pass .*GPU median',r'gi: GPU cost .*average of 300 frames',r'\[trace\] frame \d+ written',r'\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+',r'\[rrpost\] exposure hold released after [1-9]\d* writes']
        j['patterns']+=['gi: trace pass','gi: GPU cost','gi: background builds','building in the background',r'\[em\]',r'\[geo\]',r'\[rrpost\] exposure','rf: reflection pass','ls: local-light shadow pass']
        gates=[]
        for g in j['expect_every']:
            if any(t in g['name'] for t in ('scene wait:','capacity:','compose stability:','non-finite added')):
                g['windows']=costs;gates.append(g)
        gates += [dict(name='no pipeline builds in every measured window',select=r'gi: background builds \(\d+ frames\):',match=r': 0 frames waiting, 0 pending, 0 started',min=1,windows=measured),
                  dict(name='no skipped background pass in measured windows',select='building in the background',match='(?!)',min=0,event=True,windows=measured),
                  dict(name='complete trace timer every cost state',select='gi: trace pass',match=r'GPU median [0-9.]+ ms, p10 [0-9.]+, p90 [0-9.]+ \(300 frames\)',min=1,windows=costs),
                  dict(name='complete GI timer every cost state',select='gi: GPU cost',match=r'average of 300 frames',min=1,windows=costs)]
        for prefix in ('rf: reflection pass','ls: local-light shadow pass'):
            gates.append(dict(name=prefix+' complete timer per cost state',select=prefix,
                              match=r'GPU median [0-9.]+ ms, .*\(300 frames;',period_frames=300,min=1,windows=costs))
        gates += [dict(name='enclosing RT cost complete timer',select='rt passes: GPU cost',
                       match=r'average of 300 frames',min=1,windows=costs),
                  dict(name='water compose GPU timer present',select=r'\[water\] compose GPU',
                       match=r'\([1-9]\d* timed frames:',min=1,windows=costs),
                  dict(name='water reflection GPU timer present',select=r'\[water\] reflections GPU',
                       match=r'\([1-9]\d* timed;',min=1,windows=costs)]
        j['expect_every']=gates
        j['expect_runner']=[r'\[window-mode\].*visible_unfocused']+[rf"\[motion-trace\] phase={i} label={p['label']} repeat=0" for i,p in enumerate(plan) if p.get('trace_at')]+[rf'\[motion-plan\] complete phases={len(plan)}\b','^Run ended: done']
        startup_state=dict(v.split('=',1) for v in j['set'].split(','))
        for key in ['rt_water_fg_mv', 'rt_water_glint_aa', 'rt_water_rr_mv']: startup_state.pop(key,None)
        for key,value in {'dump_png_width': 1280, 'fg_log_presents': 6, 'fg_snap': 0, 'fg_snap_any': 0, 'fg_snap_delay': 0, 'fg_snap_delay_ms': 0, 'fg_state_every': 60, 'rt_xfb_mb': 1024}.items(): startup_state.setdefault(key,str(value))
        j['set']=','.join(k+'='+v for k,v in startup_state.items())
        j['live_flow']['startup_only']=['rt_xfb_mb']
        j['purpose']='Ultra exact candidates, same-run independent ON states bracketed by OFF. Static float/PNG identity must pass noise-derived OFF-repeat bands; no release promotion from energy alone. Complete 300-frame cost timers required; missing sample lines INVALID. No game permission granted.'
        (HERE/'jobs'/f'{n}.json').write_text(json.dumps(j,indent=1)+'\n',encoding='utf-8')
    predicted={'status':'READY_FOR_LIVE_DRAFT','no_game_launched':True,'keys':{
        KEYS[0]:{'camp_trace_ms_baseline':6.9,'planned_fraction_saved':[.05,.10],'exactness':'same arithmetic, hit normal and light metadata; offline gate first'},
        KEYS[1]:{'alpha_ceiling_ms':5.7,'planned_fraction_saved':[.02,.05],'exactness':'same metadata bits; ray LOD and thresholds unchanged'},
        KEYS[2]:{'WI_compose_ms_baseline':.313,'planned_empty_saved_ms':[.08,.16],'populated_s8_saved_ms':[0,.1],'exactness':'no native target writes on groups with no validity records'}},
        'guaranteed_saved_ms':0,'identity':'Compare pre-post float scene energy and static image OFF repeats against ON within one run; OFF-OFF noise derives tolerance. Exact deterministic replay is mandatory separately.',
        'cost_contract':'25s at >=24 app fps yields >=600 frames, allowing a full 300-frame timer after toggle. Require one wholly-contained 300-frame line after queue_gates discards the first straddling timer line, otherwise INVALID. no background build in shots/cost. No heavy offline work during cost.',
        'missing_observable':'Actual GPU cost and image identity; WI empty scene requires follow-up job if camp/S8 have records.'}
    (HERE/'predicted.json').write_text(json.dumps(predicted,indent=1)+'\n',encoding='utf-8')
    print(f'2 jobs, {len(plan)} phases, {sum(p["seconds"] for p in plan)}s per scene')
if __name__=='__main__': main()
