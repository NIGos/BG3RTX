"""Repair historical drafts through the shared retiming/readiness path; offline only."""
import copy
import json
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches import pipeline_readiness as ready
HERE=Path(__file__).resolve().parent

def write(p,x): p.write_text(json.dumps(x,indent=2)+'\n')
def run():
    for path in (HERE/'jobs').glob('*.json'):
        job,plan,pp=flow.load_job(path)
        job['window_mode']='visible_unfocused_fg'
        job['args']=[a for a in job['args'] if a!='-Foreground']
        job['stop_on_dump']=False
        job['live_flow']=dict(expected_fps=30,settings_latency_s=1,trace_stall_s=3)
        if not any(p['label']=='warm-generated-off' for p in plan):
            warm=copy.deepcopy(plan[0]); warm.update(label='warm-generated-off',set='rt_hit_material_generated=0,rt_stats=0,qa_exposure_hold=0',seconds=5,motion_start=5,motion_end=5)
            plan.insert(1,warm)
        # Generated rows alter host publication and startup VS capture, not the hot GI shader family.
        # Use the existing consumer completion as the conditional build witness; a fresh quiet watch
        # still reconciles outstanding builds after the settings acknowledgement.
        for p,wanted in zip(plan,ready.requirements(job,plan)):
            # Capacity/generated/streams producers are gated by rt_stats.
            if p['label'].startswith(('producer-sanity-', 'diag-')):
                p['set']=p['set'].replace('rt_stats=0','rt_stats=1')
            if 'rt_hit_material_generated' in wanted:
                p['wait_log']=['(?#rt_hit_material_generated)'+ready.GI_TRACE]
        for gate in job['expect_every']:
            if gate.get('event'):
                gate.pop('event',None);gate['period_frames']=120
        job['expect_runner']=[r'\[window-mode\].*visible_unfocused', '^Run ended: done',rf'\[motion-plan\] complete phases={len(plan)}\b']
        job['expect_runner'] += [rf'\[motion-trace\] phase={i} label={p["label"]} repeat=0' for i,p in enumerate(plan) if p.get('trace_at')]
        write(path,job);write(pp,plan)
    flow.tighten_files(HERE,cache=HERE/'.corpus.json')
    # Bound the complete launch, including all cold waits and a 120-second load.
    for path in (HERE/'jobs').glob('*.json'):
        job,plan,pp=flow.load_job(path)
        for phase in plan:
            if phase.get('wait_log'): phase['wait_log_timeout']=min(40,phase['wait_log_timeout'])
        budget=flow.duration_budget(job,plan)
        job['timeout']=int(__import__('math').ceil(budget['seconds']+budget['readiness_timeout_seconds']+120))
        assert job['timeout']<900, (path,job['timeout'])
        write(path,job);write(pp,plan)
if __name__=='__main__':run()
