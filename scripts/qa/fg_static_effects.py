"""Same-view FG 0/2 effect presence and debug-image comparison; retained traces only."""
import argparse
import json
from pathlib import Path
import re
import statistics
import numpy as np
from PIL import Image,ImageDraw
from .fg_motion_gate import phase_metrics,starts,normalize_clocks
from .rebase_gate import clock
from .decode import Trace,PASS_DIFFUSE
from . import imgops as im, thresholds as th


def same_pose(poses):
    if not poses: return False
    a=np.asarray(poses,np.float64)
    if a.ndim!=2 or a.shape[1]!=6 or not np.isfinite(a).all(): return False
    f=a.astype(np.float32)
    ulp=np.maximum(np.abs(np.nextafter(f,np.float32(np.inf)).astype(np.float64)-f),
                   np.abs(np.nextafter(f,np.float32(-np.inf)).astype(np.float64)-f))
    # Two native FP32 bins plus rounding of the nine-significant-digit log.
    bound=ulp+ulp[0]+5e-9*(np.abs(a)+np.abs(a[0]))
    return bool(np.all(np.abs(a-a[0])<=bound))


def compare_debug(off,on,albedo_off,albedo_on):
    a,b=np.asarray(off,np.float64),np.asarray(on,np.float64)
    if a.shape!=b.shape or a.ndim!=4 or a.shape[0]<3: raise ValueError('three equal-size debug repeats per FG configuration required')
    aa,bb=np.asarray(albedo_off),np.asarray(albedo_on)
    if aa.shape!=bb.shape or aa.ndim!=4: raise ValueError('matching G-buffer albedo repeats required')
    av,bv=aa.mean(0),bb.mean(0)
    noise=np.maximum(np.max(np.abs(aa-av),axis=(0,3)),np.max(np.abs(bb-bv),axis=(0,3)))
    jitter=th.jitter_change(av.mean(-1),th.PREVIEW_STEP)
    stable=np.max(np.abs(av-bv),axis=-1)<=noise+jitter
    stable=np.asarray(Image.fromarray((stable*255).astype(np.uint8)).resize((a.shape[2],a.shape[1]),Image.Resampling.NEAREST))>0
    stable=im.erode(stable,1)
    if not stable.any(): raise ValueError('no stable same-view G-buffer support')
    smooth=lambda x: np.stack([np.stack([im.box(frame[...,c],1) for c in range(3)],axis=-1) for frame in x])
    a,b=smooth(a),smooth(b); ma,mb=a.mean(0),b.mean(0)
    sigma=np.sqrt(a.var(0,ddof=1)/len(a)+b.var(0,ddof=1)/len(b))
    bound=th.k_sigma(stable.shape,1)*sigma+th.PREVIEW_STEP
    # Native jitter can move a static edge by one pixel, in both configurations.
    edge=np.stack([th.jitter_change(ma[...,c],0) for c in range(3)],axis=-1)
    flagged=stable & np.any(np.abs(ma-mb)>bound+edge,axis=-1)
    labels,n=im.label(im.opening(flagged,1))
    regions=[{'area':int(area),'bbox':list(bb)} for _,area,bb in im.regions(labels,n,th.min_area(1))]
    return {'pass':not regions,'stable_pixels':int(stable.sum()),'flagged_pixels':int(flagged.sum()),
            'regions':regions,'mean_code_off':float(ma[stable].mean()),'mean_code_on':float(mb[stable].mean()),
            'threshold':'Bonferroni sigma of repeated previews + one 8-bit code + one-pixel jitter footprint'},ma,mb,flagged


def analyze(log,runner,plan,out_dir):
    log,runner=normalize_clocks(log,runner)
    rows=phase_metrics(log,runner,plan); times=starts(runner); reasons=[]
    if not re.search(r'\[motion-plan\] complete phases='+str(len(plan))+r'\b',runner): reasons.append('static plan incomplete')
    normal=[r for r in rows if r['view']=='normal']
    if {r['fg'] for r in normal}!={0,2}: reasons.append('both static FG configurations required')
    for r in rows:
        if not r['rt_active'] or min(r['rt_active'])<120 or any(r['rt_missing']): reasons.append(f'phase {r["index"]}: missing RT')
        if not r['lighting_pose']: reasons.append(f'phase {r["index"]}: missing GPU pose')
        if r['faults']: reasons.append(f'phase {r["index"]}: API/RR/device fault')
        if r['fg']==2 and (not r['watch_ratios'] or statistics.median(r['watch_ratios'])<=1 or
            not any(s['presented']>=2 and s['status']==0 for s in r['sdk'])):
            reasons.append(f'phase {r["index"]}: FG 2 requested but generation unproved')
        if r['fg']==2 and r['sdk_focus_disable_messages']: reasons.append(f'phase {r["index"]}: FG focus loss')
        if r['fg']==0 and (not r['watch_ratios'] or statistics.median(r['watch_ratios'])>1): reasons.append('FG-off reference presentation unproved')
    for r in normal:
        if len(r['primary_match'])<3 or len(r['epoch'])<3: reasons.append('incomplete static primary/GPU windows')
        for effect,lines in r['effect_evidence'].items():
            if not lines: reasons.append(f'FG {r["fg"]}: {effect} not proved applied by per-pass counters')
        if r['fg']==2 and (not r['fps_median'] or r['fps_median']<18): reasons.append('FG-on static cadence below 18 fps')
        if not r['lighting_pose']: reasons.append('missing GPU lighting pose')
    if not same_pose([p for r in rows for p in r['lighting_pose']]): reasons.append('static camera pose differs beyond native precision')
    traces=[]
    for line in log.splitlines():
        m=re.search(r'\[trace\] frame \d+ written:.* -> (.+)$',line)
        if m: traces.append((clock(line),Path(m[1].strip())))
    views={}; image_results={}
    for i,step in enumerate(plan):
        if step.get('view','normal')=='normal': continue
        begin=times.get(i); group=[]
        if begin is not None: group=[path for t,path in traces if t is not None and begin<=t<begin+step['seconds']]
        views[(step['view'],step['fg'])]=group
    out_dir=Path(out_dir); out_dir.mkdir(parents=True,exist_ok=True)
    for view in ('sun','local','GI','reflections','RR'):
        groups={fg:views.get((view,fg),[]) for fg in (0,2)}
        try:
            samples={}; albedos={}
            for fg,paths in groups.items():
                if len(paths)!=3: raise ValueError(f'FG {fg}: expected three completed {view} traces, got {len(paths)}')
                samples[fg]=[]; albedos[fg]=[]
                for path in paths:
                    if not (path/'done.txt').exists(): raise ValueError('incomplete trace '+str(path))
                    t=Trace(path); key=t.final_key() if view=='RR' else t.find(PASS_DIFFUSE,'R11G11B10F')
                    if t.nonfinite(): raise ValueError('non-finite captured values '+str(path))
                    g=t.gbuffer_fs(); alb=t.find(g,'RGBA8',slot=1) if g else None
                    if not key or not alb: raise ValueError('missing debug/G-buffer dump '+str(path))
                    samples[fg].append(np.asarray(Image.open(path/t.dumps[key]['png']).convert('RGB'),np.float32)/255)
                    albedos[fg].append(t.load(alb)[...,:3])
            result,a,b,flagged=compare_debug(samples[0],samples[2],albedos[0],albedos[2])
            result['captures']={str(fg):[str(p) for p in paths] for fg,paths in groups.items()}
            canvas=Image.new('RGB',(a.shape[1]*2,a.shape[0]+24))
            canvas.paste(Image.fromarray((np.clip(a,0,1)*255).astype(np.uint8)),(0,24))
            canvas.paste(Image.fromarray((np.clip(b,0,1)*255).astype(np.uint8)),(a.shape[1],24))
            draw=ImageDraw.Draw(canvas); draw.text((4,4),view+' FG 0 reference | FG 2 deciding',fill='white')
            for region in result['regions']:
                y0,y1,x0,x1=region['bbox']
                draw.rectangle((x0+a.shape[1],y0+24,x1+a.shape[1],y1+24),outline='red',width=2)
            path=out_dir/(view+'-fg-static.png'); canvas.save(path); result['sheet']=str(path)
            image_results[view]=result
            if not result['pass']: reasons.append(view+': FG-dependent debug difference beyond measured repeats/encoding/jitter')
        except (ValueError,FileNotFoundError,KeyError) as error:
            reasons.append(view+': '+str(error)); image_results[view]={'pass':False,'error':str(error)}
    return {'gate':'PASS' if not reasons else 'FAIL','reasons':reasons,'phases':rows,'debug_views':image_results,
        'limits':'Debug differences flag a parity bug for investigation, not its cause. Preview precision and repeated-noise assumptions limit sensitivity.'}


def main():
    p=argparse.ArgumentParser()
    for name in ('log','runner','plan','out'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args(); r=analyze(a.log.read_text(errors='replace'),a.runner.read_text(encoding='utf-8-sig',errors='replace'),json.loads(a.plan.read_text()),a.out.parent/'static-effects-sheets')
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_text(json.dumps(r,indent=2)+'\n')
    print('FG STATIC EFFECT PARITY',r['gate'])
    for reason in r['reasons']: print(reason)
    raise SystemExit(0 if r['gate']=='PASS' else 1)


if __name__=='__main__': main()
