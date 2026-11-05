"""Prepare the inn roof A/B offline. Never stage jobs or launch the game."""
from copy import deepcopy
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import phase
from tools.live_batches.release_qual.make_final import retime_final

HERE = Path(__file__).resolve().parent
STATES = [('off-a', 0), ('on-a', 1), ('off-b', 0), ('on-b', 1)]
PASSES = ['origin', 'temporal', 'AO fold', 'sun trace', 'sun resolve', 'composite']
TIMERS = ['gi: GPU cost', 'gi: trace pass', 'rt passes: GPU cost'] + [
    'gi: '+p+' pass GPU cost' for p in PASSES]


def generate():
    lint_snapshot.load()
    flow.readiness_budget.configure([])
    source, old_plan, _ = flow.load_job(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')
    ultra = next(s for p, s in zip(old_plan, flow.phase_states(source, old_plan))
                 if p['label'] == 'ultra--static-a')
    startup = dict(ultra, gi_roof_reject='0', detailed_logging='1', cpu_timing='0', dev='1',
        rt_stats='0', rt_primary_check='0', ls_stats='0', rf_stats='0', sharc='0',
        rt_stats_period_frames='60', rt_timing_frames='60', rt_scene_wait_log_frames='60',
        qa_exposure_hold='0', pan_prepare='1', pan_edge='1', pan_post='1', pan_capture='0',
        dump_images='0', dump_raw='0', dump_preview_f32='0', dump_shaders='0')
    target = dict(startup, qa_exposure_hold='1')
    plan = [phase('warmup', 29, set=assignments(startup),
                  wait_log=[r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready'], wait_log_timeout=60)]
    # Warm both specializations; camera-plan reconciles outstanding builds
    # after the settings acknowledgement before it starts any measured frame.
    for value in (1, 0):
        plan.append(phase('warm-key-'+str(value), 5,
                          set=assignments(dict(target, gi_roof_reject=str(value)))))
    every = []
    serial = 99640
    for i, (label, value) in enumerate(STATES):
        state = dict(target, gi_roof_reject=str(value), pan_capture='0')
        plan.append(phase('ready-'+label, 3, set=assignments(state)))
        window = label+'--static-a'
        plan.append(phase(window, 15, set=assignments(state), frames=180, settle_frames=0,
                          stats_reset=True, wait_stats=TIMERS, wait_log_timeout=15))
        for timer in TIMERS:
            every.append(dict(name=label+' '+timer, select=timer, match=timer,
                windows=[window], min=2, period_frames=60, sample_frames=60))
        plan.append(phase('energy-'+label, 8, set=assignments(state), trace_at=[1]))
        plan.append(dict(phase('qa-'+label, 3, set=assignments(dict(state, pan_capture=str(serial+i)))),
                         serial=serial+i, capture_at=.1))
        every += [dict(name=label+' float pre-post export', select=r'\[trace\] frame \d+ written:',
                       match=r'\[trace\] frame \d+ written:', event=True, min=1, windows=['energy-'+label]),
                  dict(name=label+' QA transport', select=rf'\[pan\] burst {serial+i} complete',
                       match=rf'\[pan\] burst {serial+i} complete: 8 frames, \d+ samples, transport ok',
                       event=True, min=1, windows=['qa-'+label])]
    plan.append(phase('restore', 2, set=assignments(dict(startup, pan_prepare='0', pan_edge='0', pan_post='0'))))
    every.append(dict(name='current-frame scene', select='rt passes: GPU cost',
        match=r'TLAS this frame [1-9]\d*, previous frame 0, none 0',
        windows=[label+'--static-a' for label, _ in STATES], min=2, period_frames=60, sample_frames=60))
    name = 'opt-gi-roof-inn'
    plan_path = HERE/'plans'/f'{name}.json'
    job_path = HERE/'jobs'/f'{name}.json'
    job = deepcopy(source)
    job.update(name=name, live_batch='opt-r1-gi-roof', set=assignments(startup),
        args=['-RealPresent', '-Move', '-MovePlan', plan_path.relative_to(ROOT).as_posix()],
        expect_every=every, hold=90, timeout=300, max_minutes=5, stop_on_dump=False,
        live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=1),
        candidate=dict(key='gi_roof_reject', preset='ultra', states=STATES, serial_base=serial),
        purpose='Prepared offline only. Exact decision proof; one-launch OFF/ON/OFF/ON, no live qualification yet.')
    job['patterns'] = list(dict.fromkeys(job['patterns'] + TIMERS + [r'\[trace\]', r'\[dump\]', r'\[pan\]']))
    job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                           rf'\[motion-plan\] complete phases={len(plan)}\b']
    job['post'] = ('python tools/live_batches/opt_r1/gi_roof/analyze.py --job '+job_path.relative_to(ROOT).as_posix()+
                   ' --log "{log}" --runner "{runner}" --out "{log}.roof.json"')
    job['post_gate'] = True
    retime_final(job, plan)
    for p in plan:
        if p['label'].startswith('energy-'):
            p['seconds'] = p['motion_end'] = max(p['seconds'], 5)
        if p.get('wait_log'):
            p['wait_log_timeout'] = 60 if p['label'] == 'warmup' else 10
    job['estimated_phase_seconds'] = round(sum(p['seconds'] for p in plan), 3)
    flow.repair_known_forbids(job, plan)
    flow.write(job_path, job)
    flow.write(plan_path, plan)
    flow.write(HERE/'predicted.json', dict(status='OFFLINE_CHECKS_PENDING', key='gi_roof_reject',
        spec_id=1027, default=0, scene=source['save'], preset='ultra', states=STATES,
        expected_trace_saving_ms=[.30, 1.50], measured_saving_ms=None,
        exactness='After emission and successful screen/SHaRC paths; blocked hidden t > RB forces far_sun false, radiance false, fr.w0 zero. Original fallback retained. rt_stats bypasses optimization.',
        cost='Both ON windows must reduce GI trace and enclosing GI beyond OFF-repeat spread. Report all six active per-pass timers independently; contact is absent with RT sun on. Do not add nested timers.',
        energy='Each ON RGB/luminance delta against adjacent OFF baseline must be within absolute OFF-repeat difference, using float pre-post dumps.json means only.',
        qa='Each ON rendered K8 burst versus both OFF bursts: existing temporal detector, animated albedo mask, subpixel camera registration; missing/inconclusive evidence fails.',
        smoke_ultra='All measured windows use real Ultra, stock hair, two GI rays, far GI, hit emission, water, exact-k and RR temporal. rt_stats=0 so the optimization executes.',
        limits='No launch/staging authorized. Static camera flicker qualification only; judge GPU ms, never occluded FG captures or frame time. No build/compile during the run.',
        disk='No raw trace/float images; retain dumps.json means and rendered K8 bursts below 2 GB; no individual file above 20 MB.'))


if __name__ == '__main__':
    generate()
