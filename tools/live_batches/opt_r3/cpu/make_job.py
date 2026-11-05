"""Prepare one inn CPU A/B session outside the queue. Never launch or stage."""
from copy import deepcopy
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import phase

HERE = Path(__file__).resolve().parent
NAME = 'opt-cpu1-inn'


def generate():
    source = flow.read(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')
    job = deepcopy(source)
    state = flow.settings(source['set'])
    state.update(cpu_opt1='0', rt_stats_period_frames='120', rt_timing_frames='120')
    quiet = r'\[watch\].*\| pipelines 0 \(0 ms in driver\)'
    warm_state = dict(state, cpu_opt1='1')
    plan = [phase('warmup', 20, set=assignments(warm_state),
                  wait_log=[r'\[auto\] scene ready', r'\[cfg\] settings read', quiet],
                  wait_log_fresh=[quiet], wait_log_timeout=30)]
    labels = []
    for label, value in [('off-a', '0'), ('on-a', '1'), ('off-b', '0'), ('on-b', '1')]:
        setting = 'cpu_opt1='+value
        plan.append(phase('ready-'+label, 1.1, set=setting,
            wait_log=[quiet],
            wait_log_fresh=[quiet], wait_log_timeout=30))
        name = label+'--static-a'
        labels.append(name)
        plan.append(phase(name, 20.1, set=setting, frames=600, settle_frames=0,
                          stats_reset=True, wait_stats=[r'\[watch\]', r'\[cpu\] capture hooks'],
                          wait_log_timeout=10))
    job.update(name=NAME, live_batch='opt-r3-cpu', set=assignments(state),
               args=['-RealPresent', '-Move', '-MovePlan', f'tools/live_batches/opt_r3/cpu/plans/{NAME}.json'],
               hold=180, timeout=300, max_minutes=5, patterns=[r'\[cpu\]', r'\[watch\]', r'\[life\]',
                   r'\[cfg\]', r'\[stats\]', r'\[auto\]', 'scene build GPU', 'gi: background builds'],
               expect_every=[dict(name=title, select=select, match=match, windows=labels,
                   min=3, period_frames=120, sample_frames=120) for title, select, match in [
                   ('CPU submit measured', r'\[watch\]', r'submit CPU (?!0\.000\b)[\d.]+ ms'),
                   ('capture CPU', r'\[cpu\] capture hooks', r'over 120 frames, ms/frame'),
                   ('scene decisions CPU', r'\[cpu\] bvh_build phases', r'ms/build over 120')]],
               expect_runner=[r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                              rf'\[motion-plan\] complete phases={len(plan)}\b'],
               live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=0),
               purpose='Offline preparation only. Coordinator commits; explicit authorization before staging.')
    job['post'] = ('python tools/live_batches/opt_r3/cpu/analyze.py --log "{log}" --runner "{runner}" '
                   f'--plan tools/live_batches/opt_r3/cpu/plans/{NAME}.json --out "{{log}}.cpu.md"')
    flow.write(HERE/'jobs'/f'{NAME}.json', job)
    flow.write(HERE/'plans'/f'{NAME}.json', plan)
    flow.write(HERE/'predicted.json', dict(status='READY_FOR_LIVE', key='cpu_opt1', default=0,
        shader_request_delta=0, launches=1, frames_per_window=600,
        prediction='Lower bvh decide/per-draw/TLAS recording CPU; no driver/GPU/energy change expected. '
                   'Synthetic 5000-key benchmark is not a measured game saving.',
        decision='Compare ON medians to both OFF windows and OFF repeat spread; keep no positive '
                 'saving claim if delta is inside OFF spread. Inspect every ON window and lifecycle forbids.',
        timers='Watch totals / actual rendered presents; never displayed FG presents. '
               'Scene/matrix 300-build producers drop first receipt; life is minute-wide context only.',
        readiness='Scene-ready, fresh settings-read witness (cpu_opt1 is not a knob, so it is absent from the cfg keys line) and fresh zero-pipeline watch only. No shader/pipeline generation waits.',
        pending='Coordinator commit and authorized live run. No launch or queue staging by this lane.'))


if __name__ == '__main__':
    generate()
