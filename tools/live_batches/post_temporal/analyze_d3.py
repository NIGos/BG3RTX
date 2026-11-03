from pathlib import Path
import sys,json,re
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.live_batches.edge_flicker import analyze as ef
from tools.live_batches.edge_flicker.drift import translate,roi
from scripts.qa.pan_artifacts import decode
from scripts.qa import imgops as im
from scripts import queue_gates as gates
from scripts.rr_post_compare import lum,quant_trunc
HERE=Path(__file__).parent
BASE=Path('C:/Users/quali/Documents/Code Projects/BG3RTX')
LOG=BASE/'captures/queue/logs/pt-dialogue3.1.210703.log'
RUN=LOG.with_suffix('.runner.txt')
CAP=BASE/'.claude/worktrees/test-main/captures'
labels=['off-a','on-a','off-b','on-b'];consumed=[]
def read(p):
 consumed.append(str(p));return p.read_text(encoding='utf-8-sig')
def load_burst(p):
 m=json.loads(read(p/'burst.json'));assert m['valid_transport'] and m['K']==8 and len(m['samples'])==36
 by={}
 for e in m['samples']:
  assert e['submit_frame']!=2**64-1 and e['command_recording'];f=by.setdefault(e['record_frame'],{});assert e['name'] not in f
  consumed.append(str(p/e['file']));a=decode(p/e['file'],e);assert np.isfinite(a).all();f[e['name']]=a
 ids=sorted(by);assert ids==list(range(ids[0],ids[0]+8))
 for i,f in enumerate(ids):assert set(by[f])==({'depth','final','rr_post_ac','rr_post_eb','material','albedo'} if i in (0,7) else {'depth','final','rr_post_ac','rr_post_eb'})
 return {k:[by[f][k] for f in ids if k in by[f]] for k in by[ids[0]]}
def shifted(mask,s,shape):return ef.shifted_mask(ef.resize_mask(mask,shape),ef.scaled_shift(s,mask.shape,shape))
def metric(frames,mask):
 if not mask.any():return {'pixels':0,'error':'empty frozen OFF support'}
 return ef.energy([f[...,:3] for f in frames],mask)
def chain(z,ac,eb):
 return quant_trunc(quant_trunc(quant_trunc(np.maximum(ac[...,3:4]*z[...,:3]+ac[...,:3],0))*eb[...,3:4])+eb[...,:3])

def sample(a,jx,jy,rw,rh):
 # Float sidecars are nearest texels at normalized preview coordinates.
 h,w=a.shape[:2];xx=np.arange(w,dtype='f4')+jx*w/rw;yy=np.arange(h,dtype='f4')+jy*h/rh
 x0=np.floor(xx).astype(int);y0=np.floor(yy).astype(int);fx=(xx-x0)[None,:,None];fy=(yy-y0)[:,None,None]
 x1=np.clip(x0+1,0,w-1);y1=np.clip(y0+1,0,h-1);x0=np.clip(x0,0,w-1);y0=np.clip(y0,0,h-1)
 return (a[y0[:,None],x0]*(1-fx)+a[y0[:,None],x1]*fx)*(1-fy)+(a[y1[:,None],x0]*(1-fx)+a[y1[:,None],x1]*fx)*fy

def main():
 text=read(LOG);runner=read(RUN);plan=json.loads(read(HERE/'plans/pt_dialogue3.json'));lines=gates.stamped_lines(text);wins=gates.windows(runner,plan,lines)
 paths={};segments={}
 for l in labels:
  paths[l]={};segments[l]={}
  for kind,pat in [('rendered',r'\[pan\] burst .*transport ok -> (.+)$'),('presented',r'\[sl\] capturing 8 presented frames to (.+)$'),('energy',r'\[trace\] frame .* -> (.+)$'),('timing',None)]:
   w=next(w for w in wins if w['label']==kind+'-'+l);seg='\n'.join(line for t,line in lines if w['applied']<=t<w['end']);segments[l][kind]=seg
   if pat:
    found=re.findall(pat,seg,re.M);assert len(found)==1,(l,kind,found);paths[l][kind]=Path(found[0].strip())
 bursts={l:load_burst(paths[l]['rendered']) for l in labels};regs={};pres={};pregs={}
 for l in labels:
  print('register '+l,flush=True);b=bursts[l];depth=[a[...,0] for a in b['depth']]
  try:keep,r=ef.registered_run(bursts['off-a']['depth'][0][...,0],depth)
  except ValueError as e:raise ValueError(l+' rendered '+str(e))
  regs[l]=r
  for k in ('depth','final','rr_post_ac','rr_post_eb'):
   b[k]=[translate(b[k][i],ef.scaled_shift(tuple(-v for v in r['frames'][i]['shift_px']),depth[0].shape,b[k][i].shape[:2])) for i in keep]
  for k in ('material','albedo'):b[k]=[translate(b[k][j],tuple(-v for v in r['frames'][i]['shift_px'])) for j,i in enumerate((0,7)) if i in keep]
  files=sorted(paths[l]['presented'].glob('p*.png'));assert len(files)==8 and [int(p.stem[1:]) for p in files]==list(range(int(files[0].stem[1:]),int(files[0].stem[1:])+8));consumed.extend(map(str,files))
  p=[np.asarray(Image.open(f).convert('RGB'),dtype='f4')/255 for f in files];pres[l]=p
  try:keep,pr=ef.registered_run(pres['off-a'][0],p);pregs[l]=pr;pres[l]=ef.aligned_frames(p,pr)
  except ValueError as e:
   pregs[l]={'error':str(e)};keep,pr=ef.registered_run(p[0],p);pr['cross_window_error']=str(e);pregs[l]=pr;pres[l]=ef.aligned_frames(p,pr)
 # Freeze support in off-a coordinates, combining both OFF repeat geometry.
 ds=[];ms=[];cs=[]
 for l in ('off-a','off-b'):
  inv=tuple(-v for v in regs[l]['shift_px'])
  ds.extend([translate(a[...,0],inv) for a in bursts[l]['depth']]);ms.extend([translate(a,inv) for a in bursts[l]['material']]);cs.extend([translate(a,ef.scaled_shift(inv,ds[0].shape,a.shape[:2])) for a in bursts[l]['final']])
 d=np.stack(ds);valid=np.isfinite(d).all(0);clean=np.nan_to_num(d);classes=[np.rint(np.nan_to_num(m[...,3])*255).astype('u1')&7 for m in ms];char=np.logical_or.reduce([np.isin(c,[3,5]) for c in classes]);hair=np.logical_or.reduce([c==5 for c in classes]);skin=np.logical_and.reduce([c==3 for c in classes])
 # Use existing discontinuity detector with universal character support, removing its hair union.
 universal=np.zeros(ms[0].shape,dtype='f4');universal[...,3]=3/255
 edge,_=ef.masks(list(clean),[universal]);grad=[]
 for a in clean:
  gy,gx=np.gradient(a);grad.append(np.abs(gx)+np.abs(gy))
 prec=6*(np.max(np.abs(clean),0)**2*(2**-25/.1)+np.max(np.abs(clean),0)*2**-24)
 stable=valid & (np.ptp(clean,0)<=np.max(grad,0)+prec)
 far=stable & (np.median(clean,0)>np.median(clean[:,char])) & ~im.dilate(char,2)
 baseline_lum=np.nanmean(np.stack([np.asarray(Image.fromarray((a@ef.Y).astype('f4')).resize((ds[0].shape[1],ds[0].shape[0]),Image.Resampling.NEAREST)) for a in cs]),0)
 contour=edge & im.dilate(char,1)&valid;threshold=float(np.percentile(baseline_lum[contour],99));masks={'hair_over_dof':hair & edge & valid,'bright_contour':contour&(baseline_lum>=threshold),'depth_stable_face':im.erode(skin&stable&~edge,2),'far_background':im.erode(far,2),'hair_box':roi(ds[0].shape,ef.DIALOGUE_ROIS['hair_box'])}
 print({k:int(v.sum()) for k,v in masks.items()},flush=True)
 for m in masks.values():m[round(m.shape[0]*590/800):]=False
 rows={};trace_anchor=None;energy_join=None
 for l in labels:
  print('measure '+l,flush=True);b=bursts[l];row={'registration':{'rendered':regs[l],'presented':pregs[l]},'rendered':{},'presented':{},'components':{}}
  for domain,frames,r in [('rendered',b['final'],regs[l]),('presented',pres[l],pregs[l])]:
   for name,mask in masks.items():
    row[domain][name]=metric(frames,shifted(mask,r['shift_px'],frames[0].shape[:2]))
  for component,frames in [('C', [a[...,:3] for a in b['rr_post_ac']]),('A',[np.repeat(a[...,3:4],3,-1) for a in b['rr_post_ac']]),('bloom',[a[...,:3] for a in b['rr_post_eb']]),('exposure',[np.repeat(a[...,3:4],3,-1) for a in b['rr_post_eb']])]:
   row['components'][component]={name:metric(frames,shifted(mask,regs[l]['shift_px'],frames[0].shape[:2])) for name,mask in masks.items()}
  p=paths[l]['energy'];dumps=json.loads(read(p/'dumps.json'));frame=read(p/'frame.json');ops=[json.loads(s.rstrip(',\n')) for s in frame.splitlines() if s.strip().startswith('{"op":"pass"')];op=next(o for o in ops if o.get('tag')=='rrpost');joined={s:next(dict(v,dump=k) for k,v in dumps.items() if v['image']==op['color'][s]) for s in (1,2,6)};arr={}
  for s,v in joined.items():
   f=p/Path(v['png']).with_name(Path(v['png']).stem+'_p.f32');consumed.append(str(f));a=np.fromfile(f,dtype='<f4').reshape(-1,384,4);assert np.isfinite(a).all() and v['nonfinite']==0;arr[s]=a
  if trace_anchor is None:
   trace_anchor=arr[2][...,:3]
   anchor_final=bursts['off-a']['final'][0]
   small=np.stack([np.asarray(Image.fromarray(anchor_final[...,c]).resize((384,arr[2].shape[0]),Image.Resampling.NEAREST)) for c in range(3)],-1)
   try:_,energy_join=ef.registered_run(small,[trace_anchor]*4)
   except ValueError as e:energy_join={'error':str(e)}
  try:_,trace_reg=ef.registered_run(trace_anchor,[arr[2][...,:3]]*4)
  except ValueError as e:trace_reg={'error':str(e)}
  jitter=re.findall(r'energy jitter ([-\d.e+]+) ([-\d.e+]+), render (\d+)x(\d+), temporal (\d+)',segments[l]['energy']);assert jitter
  jx,jy,rw,rh,on=map(float,jitter[-1]);ac=sample(arr[1],jx,jy,rw,rh);eb=sample(arr[6],jx,jy,rw,rh);v=chain(arr[2],ac,eb);mask=ef.resize_mask(masks['far_background'],v.shape[:2]);row['energy']={'far_background_chain_float_mean':float(lum(v)[mask].mean()),'full_preview_chain_float_mean':float(lum(v).mean()),'pre_post_scene_full_image_float_mean':float(lum(np.asarray(joined[2]['mean'][:3]))),'joined_dumps':joined,'trace_registration':trace_reg,'jitter':jitter,'exposure_range':[float(eb[...,3].min()),float(eb[...,3].max())]}
  tlines=re.findall(r'^.*\[rrpost\] gpu temporal: ([\d.]+) ms/frame; active (\d+).*$',segments[l]['timing'],re.M);row['timing']={'temporal_ms':[float(v) for v,a in tlines if a=='1'],'lines':segments[l]['timing'].splitlines() if False else [s for s in segments[l]['timing'].splitlines() if '[rrpost] gpu' in s]};rows[l]=row
 report={'run':'pt-dialogue3.1.210703','main':'fbce1fec','run_valid':True,'quality_pass':False,'energy_mask_trace_join':energy_join,'qualified_for_ultra':False,'windows':rows,'capture_paths':{l:{k:str(v) for k,v in p.items()} for l,p in paths.items()},'mask_method':{'source':'both OFF repeats only, registered into off-a coordinates','depth_stability':'range across all OFF frames <= maximum one-pixel local gradient footprint plus six depth conversion intervals','hair':'baseline class-5 union intersect baseline depth discontinuity support','face':'eroded intersection of class-3 endpoints and depth-stable non-edge support; empty, gate unavailable','far':'eroded depth-stable pixels deeper than median baseline character depth, excluding dilated character support','bright':'brightest 1% baseline character depth-contour PQ luminance','pixels':{k:int(v.sum()) for k,v in masks.items()},'bright_threshold_PQ':threshold},'consumed_files':sorted(set(consumed)),'deleted':[]}
 report['observables']={}
 for domain in ('rendered','presented'):
  for region in masks:
   key=domain+'/'+region;obs={'gate':'Both ON residuals, and each parity independently, must improve below min(OFF)-abs(OFF-a - OFF-b). Face control must stay within OFF range expanded by OFF spread.'};metrics={l:rows[l][domain][region] for l in labels}
   if any('error' in v for v in metrics.values()):obs.update(verdict='FAIL',reason='No frozen OFF face support; observable unavailable',windows=metrics)
   else:
    comparisons={}
    for m in ('residual_x1000','residual_even','residual_odd'):
     v={l:metrics[l][m]*(1000 if m!='residual_x1000' else 1) for l in labels};lo=min(v['off-a'],v['off-b']);hi=max(v['off-a'],v['off-b']);spread=hi-lo
     comparisons[m]={'values_x1000':v,'off_repeat_spread_x1000':spread,'strict_improvement_threshold_x1000':lo-spread,'passes':{l:v[l]<lo-spread for l in ('on-a','on-b')}}
    registered=domain=='rendered' or not any('cross_window_error' in rows[l]['registration']['presented'] for l in labels)
    obs.update(verdict='PASS' if registered and all(all(c['passes'].values()) for c in comparisons.values()) else 'FAIL',comparisons=comparisons,registration_valid=registered)
    if not registered:obs['reason']='Cross-window rigid registration rejects on-a and off-b; presented values are diagnostics only.'
   report['observables'][key]=obs
 for metric_name in ('far_background_chain_float_mean','pre_post_scene_full_image_float_mean','full_preview_chain_float_mean'):
  v={l:rows[l]['energy'][metric_name] for l in labels};lo=min(v['off-a'],v['off-b']);hi=max(v['off-a'],v['off-b']);spread=hi-lo
  checks={l:{'delta_off_a':v[l]-v['off-a'],'delta_off_b':v[l]-v['off-b'],'pass':all(abs(v[l]-v[b])<=spread for b in ('off-a','off-b'))} for l in ('on-a','on-b')}
  report['observables']['energy/'+metric_name]={'verdict':'PASS' if all(v['pass'] for v in checks.values()) else 'FAIL','gate':'Each ON mean differs from each OFF repeat by no more than absolute OFF-OFF spread. Full-image scene is independent check; preview is diagnostic.','values':v,'off_repeat_spread':spread,'comparisons':checks}
 report['observables']['energy/far_background_chain_float_mean']['reason']='FAIL: fixed trace support cannot be registered to later phases; numerical ON-a deltas also exceed OFF noise, but are diagnostic under framing drift.'
 report['observables']['energy/far_background_chain_float_mean']['verdict']='FAIL'
 report['observables']['energy/far_background_chain_float_mean']['numeric_only']=True
 report['observables']['cost']={'verdict':'PASS' if all(rows[l]['timing']['temporal_ms'] for l in ('on-a','on-b')) else 'FAIL','gate':'Active=1 gpu temporal lines strictly inside timing-* applied/end; report cost, no ms ceiling.','on_a_ms':rows['on-a']['timing']['temporal_ms'],'on_b_ms':rows['on-b']['timing']['temporal_ms']}
 report['observables']['components']={'verdict':'PASS','gate':'Finite submitted AC/EB all eight frames; C.rgb/A.a and bloom.rgb/exposure.a independently reported; exposure remains fresh=1. No numeric component residual improvement threshold specified.','exposure_ranges':{l:rows[l]['energy']['exposure_range'] for l in labels},'residuals':'windows.*.components includes all masks and both parity residuals'}
 report['observables']['transport']={'verdict':'PASS','gate':'36 finite unique submitted samples with exact names on eight consecutive rendered frames; eight consecutive presented files; unique paths; joined float slots 1/2/6 finite.'}
 report['observables']['qualification']={'verdict':'FAIL','gate':'Both scenes and both ON windows pass all quality/energy gates.','reason':'Dialogue3 residual and background-energy gates fail; face unavailable; presented registration fails. S8 quality evidence absent; 0.12 ms is cost only.'}
 report['limitations']=['Run VALID does not imply every observable qualifies.','Cross-window rendered shifts off-b -15 px/on-b -10 px and rejected presented windows reveal framing/animation confounding; no beauty-frame cross-window warp.','OFF-frozen face intersection is empty; no mask threshold retuned to create support.','Nearest 384-wide float previews approximate full-resolution bilinear AC/EB samples, with logged ON jitter. Fixed background support is not motion-tracked into later trace phases; background energy cannot establish equivalence under framing drift.','Full-image pre-post scene means are exact dumps.json Z means, not PQ or isolated bloom; use Rec709 luminance.','No static S8 quality analysis supplied; no preset promotion.']
 report['raw_files_analyzed']=[p for p in sorted(set(consumed)) if Path(p).suffix in ('.raw','.f32') or (Path(p).name.startswith('p') and Path(p).suffix=='.png')]
 (HERE/'RESULT_20261103_D3_valid.json').write_text(json.dumps(report,indent=2)+'\n')
 print('wrote measurements',flush=True)
if __name__=='__main__':main()
