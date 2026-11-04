"""Repair canonical JSON with shared timing/readiness. Never queues."""
import json
import tempfile
from pathlib import Path
from tools.live_batches import live_flow
BASE = Path(__file__).resolve().parent

def main():
    path = BASE / 'jobs/wos1-s8-fg2.json'
    job = json.loads(path.read_text())
    job.update(timeout=840, max_minutes=15, post='py -3 tools/m4_promotion_gate.py "{log}" --pool run', post_gate=True)
    job['live_flow'] = dict(expected_fps=18, settings_latency_s=1, trace_stall_s=4)
    job['set'] = ','.join(s for s in job['set'].split(',') if not s.startswith(('rt_water_rr_mv=', 'rt_water_fg_mv=', 'rt_water_glint_aa=')))
    plan_path = BASE / 'plans/wos1_s8.json'
    plan = json.loads(plan_path.read_text())
    for phase in plan:
        phase['set'] = phase.get('set', '').replace('rt_water_hit_shade=1', 'rt_water_hit_shade=2')
    plan_path.write_text(json.dumps(plan, indent=2) + '\n')
    job['forbid'] = [s for s in job['forbid'] if 'roof volumes' not in s and 'over kSunMaxVolumes' not in s]
    windows = ['cost-off-a', 'cost-on', 'cost-off-b']
    job['expect_every'] = [g for g in job['expect_every'] if g['name'] not in ('water geometry coverage', 'no roof overflow', 'world query never truncates')]
    job['expect_every'].extend([
        dict(name='water geometry coverage', select=r'\[water\] coverage \(', match=r'captured [1-9]\d* of [1-9]\d* water draws \(100\.0%\).*triangles [1-9]\d* of [1-9]\d* supported \(100\.0%\).*non-finite 0, collapsed 0', period_frames=120, min=1, windows=windows),
        dict(name='no roof overflow', select=r'sun: WARNING \d+ roof volumes within|over kSunMaxVolumes on [1-9]', match='(?!)', event=True, min=0, windows=windows),
        dict(name='world query never truncates', select=r'\[water\] (reflections|compose) \(', match=r'world truncated 0\b', min=1, windows=windows)])
    path.write_text(json.dumps(job, indent=2) + '\n')
    live_flow.tighten_files(BASE, cache=Path(tempfile.gettempdir()) / 'bg3rtx-backlog-water-corpus.json')
    job, plan, plan_path = live_flow.load_job(path)
    for phase in plan:
        if phase.get('wait_log_timeout') and phase['label'] != 'warmup':
            phase['wait_log_timeout'] = min(30, phase['wait_log_timeout'])
    plan_path.write_text(json.dumps(plan, indent=2) + '\n')
    job['timeout'] = 840
    path.write_text(json.dumps(job, indent=2) + '\n')

if __name__ == '__main__':
    main()
