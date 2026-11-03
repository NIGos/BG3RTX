"""Prepare only; never enqueue or launch a game."""
import json
from pathlib import Path
from tools.live_batches.edge_flicker.make_job import write
from tools.live_batches.live_flow import repair_known_forbids
HERE = Path(__file__).resolve().parent
WINDOWS = [('off-a',0),('on-a',1),('off-b',0),('on-b',1)]

def make():
    template=json.loads((HERE.parent/'edge_flicker/jobs/ef1-dialogue3-attribution.json').read_text())
    for scene,save,serial in [('dialogue3','Test Dialogue3',98700),('s8','Water Queens House',98710)]:
        plan=[]; every=[]; traces=[]
        def step(label,seconds,state,**extra):
            plan.append(dict(label=label,kind='static',speed='static',seconds=seconds,repeat_ms=0,
                             scan=16,capture_at=0,serial=0,motion_start=seconds,motion_end=seconds,
                             pixels_per_second=0,direction=0,
                             set=','.join(f'{k}={v}' for k,v in state.items()),**extra))
        base=dict(rr=1,rr_post=1,rr_post_temporal=0,rr_hair_resolve=0,fg=2,
                  pan_edge=1,pan_post=1,pan_prepare=1,pan_capture=0,fg_snap=0,qa_exposure_hold=0)
        step('warmup',20,base);step('warmup-camera',20,dict(base,rr_post_temporal=1))
        for i,(label,on) in enumerate(WINDOWS):
            state=dict(base,rr_post_temporal=on,qa_exposure_hold=1)
            # At 30 app fps, 5 s is > 9 Halton cycles and > 99.99% EMA warmup.
            step('settle-'+label,5,state)
            step('rendered-'+label,5,dict(state,pan_capture=serial+i))
            plan[-1].update(serial=serial+i,capture_at=.1)
            step('presented-'+label,5,dict(state,fg_snap=8))
            # Float pre-post mean in dumps.json; no raw dumps or beauty PNG energy proxy.
            step('energy-'+label,5,state,trace_at=[1])
            traces.append(rf'\[motion-trace\] phase={len(plan)-1} label=energy-{label} repeat=0')
            step('timing-'+label,12,dict(state,pan_post=0))
            every += [dict(name='AC/EB K8 '+label,select=rf'\[pan\] burst {serial+i} complete',
                           match=rf'\[pan\] burst {serial+i} complete: 8 frames, 36 samples, transport ok',
                           event=True,min=1,windows=['rendered-'+label]),
                      dict(name='presented K8 '+label,select=r'\[sl\] present capture done',match=r'\[sl\] present capture done',
                           event=True,min=1,windows=['presented-'+label]),
                      dict(name='float energy '+label,select=r'\[trace\] frame \d+ written:',match=r'\[trace\] frame \d+ written:',
                           event=True,min=1,windows=['energy-'+label]),
                      dict(name='post GPU cost '+label,select=r'\[rrpost\] gpu:',
                           match=r'render-res chain [\d.]+ ms/frame, output-res grade [\d.]+ ms/frame',
                           min=1,windows=['timing-'+label]),
                      dict(name='FG actually presenting '+label,select=r'\[sl\] DLSS-G frame',
                           match=r'status 0x0, presented per app frame 2, displayed/rendered over the last \d+ frames (?:1\.(?:0[1-9]|[1-9]\d)|[2-9]\d*\.\d+).*api errors 0',
                           event=True,settle_s=1,min=1,windows=['presented-'+label])]
            if on:
                every.append(dict(name='temporal GPU cost '+label,select=r'\[rrpost\] gpu temporal:',
                                  match=r'gpu temporal: [\d.]+ ms/frame; active 1',min=1,windows=['timing-'+label]))
        step('restore',5,dict(base,pan_post=0,pan_edge=0,pan_prepare=0,qa_exposure_hold=0))
        startup=dict(x.split('=',1) for x in template['set'].split(','))
        if scene=='s8': startup.pop('rr_sss',None)  # layer default 1, not in bg3rtx.ini: combine.py cannot reset it in the S8 jobs
        # Real Ultra preset: no custom lighting overrides; native hair stays off as in ef1.
        for k in ['gi_ao','rt_shadows','gi_strength']: startup.pop(k,None)
        startup.update(base,dump_images=0,dump_png_width=384,dump_raw=0,dump_shaders=0,fg_snap_delay_ms=0)
        job=dict(template,name='pt-'+scene,save=save,live_batch='post-temporal',timeout=480,
                 set=','.join(f'{k}={v}' for k,v in startup.items()),
                 args=['-RealPresent','-Move','-MovePlan',f'captures/live_batches/post_temporal/plans/pt_{scene}.json'],
                 expect_every=every,estimated_phase_seconds=sum(p['seconds'] for p in plan),
                 expect_runner=[r'\[window-mode\].*visible_unfocused',r'^Run ended: done',
                                rf'\[motion-plan\] complete phases={len(plan)}\b']+traces,
                 purpose='READY_FOR_LIVE only after offline checks. Static same-run temporal post 0/1/0/1 on real Ultra. K8 rendered includes AC.a/C.rgb and EB.rgb/exposure. Presented K8 separate. No game launch authorized.')
        job['live_flow'] = dict(expected_fps=30,settings_latency_s=1,trace_stall_s=1)
        startup_state=dict(v.split('=',1) for v in job['set'].split(','))
        for key in ['rt_water_fg_mv', 'rt_water_glint_aa', 'rt_water_rr_mv']: startup_state.pop(key,None)
        for key,value in {'dump_png_width': 1280, 'fg_log_presents': 6, 'fg_snap': 0, 'fg_snap_any': 0, 'fg_snap_delay': 0, 'fg_snap_delay_ms': 0, 'fg_state_every': 60, 'rt_xfb_mb': 1024}.items(): startup_state.setdefault(key,str(value))
        job['set']=','.join(k+'='+v for k,v in startup_state.items())
        job['live_flow']['startup_only']=['rt_xfb_mb']
        job['forbid'] += [r'\[rrpost\] temporal REFUSED',r'\[rt\] mesh.*pool full',r'nonfinite']
        job['patterns'] += [r'\[trace\]', r'\[dump\]']
        if scene == 's8':
            job['framing_status'] = 'PROPOSED_STATIC_S8_REQUIRES_VERIFICATION'
        write(HERE/f'plans/pt_{scene}.json',plan)
        repair_known_forbids(job,plan)
        write(HERE/f'jobs/pt-{scene}.json',job)

if __name__=='__main__':make()
    from tools.live_batches.live_flow import tighten_files
    tighten_files(Path(__file__).resolve().parent)
