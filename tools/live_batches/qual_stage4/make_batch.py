"""Prepare only: existing job schema, shared readiness/retiming, no queue or Git writes."""
import copy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches import pipeline_readiness as ready
HERE=Path(__file__).resolve().parent

def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n')
def phase(label,settings,seconds=8):
 return dict(label=label,kind='static',speed='static',seconds=seconds,repeat_ms=0,scan=16,capture_at=0,serial=0,motion_start=seconds,motion_end=seconds,pixels_per_second=0,direction=0,set=settings)
def finish(job,plan,stem):
 job.update(name='qs4-'+stem,live_batch='qual-stage4',window_mode='visible_unfocused_fg',session=False,stop_on_dump=False,hold=120,live_flow=dict(expected_fps=30,settings_latency_s=1,trace_stall_s=3))
 job['args']=['-RealPresent','-Move','-MovePlan',f'captures/live_batches/qual_stage4/plans/{stem}.json']
 for p,wanted in zip(plan,ready.requirements(job,plan)):
  for key in ('rt_hit_material_generated','gi_temporal_mv_sample','gi_prev_radiance_guard'):
   if key in wanted:
    consumer=(r'gi: gi/gi_temporal\.hlsl[^ ]* generation ' if key=='gi_temporal_mv_sample' else ready.GI_TRACE)
    p.setdefault('wait_log',[]).append('(?#'+key+')'+consumer)
 job['expect_runner']=[r'\[window-mode\].*visible_unfocused','^Run ended: done',rf'\[motion-plan\] complete phases={len(plan)}\b']
 job['expect_runner'] += [rf'\[motion-trace\] phase={i} label={p["label"]} repeat=0' for i,p in enumerate(plan) if p.get('trace_at')]
 write(HERE/'jobs'/f'{stem}.json',job);write(HERE/'plans'/f'{stem}.json',plan)
def main():
 prediction=flow.read(HERE/'predicted.json')
 prediction.update(status='PREPARED_NOT_AUTHORIZED',queued=False)
 prediction['blockers']=['Original 5 K16 contract cannot be satisfied by PNG screening. 03-guards-camp is the smallest rejection screen (float static energy plus Q/E images and GPU cost); it can reject or identify a fix, never promote. Original calibrated 5 K16 transient qualification remains required for promotion.']
 prediction['guard_decision']={'observable':'scripts.qa.pan_artifacts gi_luma edge/interior pixel-frame rates and mean, same-run OFF repeat spread; gi trace and temporal GPU ms','original_promotion_gate':'prev: edge below min(OFF) by more than OFF spread, interior/mean within OFF spread; mv: edge/interior no greater than max(OFF), mean within OFF spread; fresh current-sample fallback preserves energy. All five K16 complete and valid; FG active on all frames, registration and calibrated detector pass.','reject':'Any energy/detail regression, no edge benefit beyond OFF spread, or no useful effect rejects that bit for release. Missing evidence is INVALID.','tool':'python -m scripts.qa.pan_artifacts --log <same-run-log> --out <analysis-dir>','disk_mb':600}
 write(HERE/'predicted.json',prediction)
 base=None
 for i,scene in enumerate(('camp','inn'),1):
  source=next((HERE.parent/'m4_generated/jobs').glob(f'hgn{i}-*.json'))
  job,plan,_=flow.load_job(source);job=copy.deepcopy(job);plan=copy.deepcopy(plan)
  job['purpose']='Generated row qualification; identical source gates to m4_generated, no GI PNG screening.'
  finish(job,plan,f'{i:02d}-{scene}')
  if scene=='camp':base=copy.deepcopy(job)
 job=copy.deepcopy(base)
 job['expect_every']=[]
 job['expect']=[r'\[auto\] scene ready',r'\[trace\] frame \d+ written',r'\[rrpost\] exposure held']
 job['patterns'] += [r'\[pan\]',r'gi:.*GPU',r'gi: gi/gi_temporal']
 job['purpose']='GI fix-or-reject screen only: static float energy and Q/E images with per-bit cost; cannot promote without original K16 transient qualification.'
 job['set'] += ',gi_temporal_mv_sample=0,gi_prev_radiance_guard=0,rt_hit_material_generated=0,pan_capture=0,pan_prepare=0'
 plan=[]
 for label,mv,pg in [('warm-off',0,0),('warm-mv',1,0),('warm-prev',0,1),('warm-off-return',0,0)]:
  plan.append(phase(label,f'rt_stats=0,gi_temporal_mv_sample={mv},gi_prev_radiance_guard={pg},pan_capture=0,pan_prepare=0,qa_exposure_hold={0 if label=="warm-off" else 1}',30 if label=='warm-off' else 5))
 for label,mv,pg in [('off-a',0,0),('mv',1,0),('prev',0,1),('off-b',0,0)]:
  settings=f'rt_stats=0,gi_temporal_mv_sample={mv},gi_prev_radiance_guard={pg},qa_exposure_hold=1,pan_capture=0,pan_prepare=0'
  p=phase('shot-'+label,settings,8);p['trace_at']=[5];plan.append(p)
  for direction,turn in [(-1,'q'),(1,'e')]:
   p=phase('turn-'+turn+'-'+label,settings,8);p.update(kind='rotate',turn_mode='keys',direction=direction,motion_start=1,motion_end=6,trace_at=[3]);plan.append(p)
  p=phase('cost-'+label,settings,20);plan.append(p)
  job['expect_every'].append(dict(name='GI trace cost '+label,select=r'gi: trace pass \(ray-traced',match=r'GPU median [0-9.]+ ms',windows=['cost-'+label],min=1))
 plan.append(phase('restore','gi_temporal_mv_sample=0,gi_prev_radiance_guard=0,pan_prepare=0,pan_capture=0,qa_exposure_hold=0,rt_stats=0',5))
 for source_gate in base['expect_every']:
  if any(term in source_gate['name'] for term in ('scene wait:', 'capacity: the RT', 'capacity: the cascade')):
   gate=copy.deepcopy(source_gate);gate['windows']=['cost-'+x for x in ('off-a','mv','prev','off-b')];job['expect_every'].append(gate)
 job.pop('post',None)
 finish(job,plan,'03-guards-camp')
 flow.tighten_files(HERE,cache=HERE/'.corpus.json')
 for path in (HERE/'jobs').glob('*.json'):
  job,plan,pp=flow.load_job(path)
  for p in plan:
   if p.get('wait_log'):p['wait_log_timeout']=min(40,p['wait_log_timeout'])
  budget=flow.duration_budget(job,plan);job['timeout']=int(__import__('math').ceil(budget['seconds']+budget['readiness_timeout_seconds']+120))
  assert job['timeout']<900,(path,job['timeout'])
  write(path,job);write(pp,plan)
if __name__=='__main__':main()
