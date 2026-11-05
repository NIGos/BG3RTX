"""Prepare one static Ultra GI trace attribution launch; never stage or run."""
from copy import deepcopy
from pathlib import Path
import math
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot, pipeline_readiness
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import phase
from tools.live_batches.release_qual.make_final import retime_final

HERE = Path(__file__).resolve().parent
FEATURES = [('direct', 'gi_hit_direct', '0'), ('sun', 'gi_hit_sun', '0'),
            ('emission', 'rt_hit_emission', '0'), ('far', 'gi_far', '0'),
            ('second-ray', 'gi_rr_rays', '1'), ('material', 'rt_hit_material', '0')]
TRACE = 'gi: trace pass'
# Six cold alternatives get 37 s each; six returns to resident Ultra get 3 s.
WARM_TIMEOUT_BUDGET = 240


def generate():
    lint_snapshot.load()
    flow.readiness_budget.configure([])
    source, original, _ = flow.load_job(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')
    base = next(s for p, s in zip(original, flow.phase_states(source, original))
                if p['label'] == 'ultra--static-a')
    base.update(detailed_logging='1', dev='1', cpu_timing='0', qa_exposure_hold='1',
                rt_timing_frames='60', rt_stats_period_frames='60',
                pan_capture='0', pan_prepare='0', pan_edge='0', pan_post='0')
    plan = [phase('warmup', 29, set=assignments(base),
                  wait_log=[r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready'],
                  wait_log_timeout=60)]
    # Warm all selected variants before any judged window; no instrumentation
    # beyond the ordinary GPU timer, no captures, no camera motion.
    for name, key, off in FEATURES:
        for label, value in [('off', off), ('on', base[key])]:
            plan.append(phase('warm-'+name+'-'+label, 1, set=f'{key}={value}',
                wait_log=[pipeline_readiness.GI_TRACE, pipeline_readiness.QUIET],
                wait_log_fresh=[pipeline_readiness.QUIET], wait_log_timeout=37 if label=='off' else 3))
    every, brackets = [], []
    for name, key, off in FEATURES:
        windows = []
        for label, value in [('off-a', off), ('on', base[key]), ('off-b', off)]:
            window = name+'--'+label
            windows.append(window)
            plan.append(phase(window, 4.1, set=f'{key}={value}', frames=120,
                              settle_frames=0, stats_reset=True, wait_stats=[TRACE], wait_log_timeout=10))
            every.append(dict(name=window+' trace cost', select=TRACE,
                match=r'gi: trace pass .*GPU median [0-9.]+ ms', windows=[window],
                min=1, period_frames=60, sample_frames=60))
        plan.append(phase('restore-'+name, 1, set=f'{key}={base[key]}'))
        brackets.append(dict(feature=name, key=key, off=off, on=base[key], windows=windows))
    plan.append(phase('restore', 1, set='qa_exposure_hold=0'))
    name = 'opt-gi-attr-inn'
    plan_path = HERE/'plans'/f'{name}.json'
    job = deepcopy(source)
    for field in ('post', 'post_gate', 'candidate'):
        job.pop(field, None)
    job.update(name=name, live_batch='opt-r2-gi-attr', set=assignments(base),
        args=['-RealPresent', '-Move', '-MovePlan', plan_path.relative_to(ROOT).as_posix()],
        expect=[TRACE], expect_every=every, forbid=[],
        patterns=[TRACE, r'\[cfg\]', r'\[auto\]'],
        expect_runner=['^Run ended: done', rf'\[motion-plan\] complete phases={len(plan)}\b'],
        live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=0),
        purpose='Diagnostic attribution only: each feature OFF / Ultra ON / OFF, other Ultra dependencies held on.',
        timeout=300, max_minutes=5, hold=90, stop_on_dump=False)
    retime_final(job, plan)
    # Cold shader requests are awaited only here, before all cost windows.
    # Later transitions revisit resident variants and retain the short watchdog.
    for p in plan[1:]:
        if p.get('wait_log'):
            p['wait_log_timeout'] = (37 if p['label'].endswith('-off') else 3) if p['label'].startswith('warm-') else 3
    budget = flow.duration_budget(job, plan)
    job['timeout'] = math.ceil(budget['seconds'] + budget['readiness_timeout_seconds'] + 1)
    job['max_minutes'] = math.ceil(job['timeout']/60)
    flow.write(plan_path, plan)
    flow.write(HERE/'jobs'/f'{name}.json', job)
    flow.write(HERE/'predicted.json', dict(status='READY_FOR_LIVE', diagnostic=True,
        launches=1, static_camera=True, max_seconds=job['timeout'], shader_request_delta=0,
        warm_readiness_budget_seconds=WARM_TIMEOUT_BUDGET,
        warm_policy='Fresh driver quiescence and selected trace completion before timing; 37 s per cold OFF request, 3 s per resident Ultra return.',
        reference='captures/queue/logs/opt-profile-inn.1.005111.log: Ultra GI trace about 13.6 ms',
        brackets=brackets, attribution='trace(on) minus mean(trace(off-a), trace(off-b)); report both signed bracket deltas and OFF drift.',
        baseline='Pair-specific OFF: only the named feature differs from Ultra. Dependencies stay enabled; costs are conditional and not additive.',
        gates='Only each measured window logging its GI trace GPU cost; no quality, energy or speedup gate.',
        pending='Prepared offline only. No staging or launch authorized. No candidate qualification claimed.',
        captures=0, disk_estimate_mb=5))


if __name__ == '__main__':
    generate()
