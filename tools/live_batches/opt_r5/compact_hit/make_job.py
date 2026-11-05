"""Prepare two GPU A/B jobs offline, without staging or launching."""
import argparse
from copy import deepcopy
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot, pipeline_readiness
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import observation_state, phase

HERE = Path(__file__).resolve().parent
KEY = 'rt_compact_hit_state'
STATES = [('off-a', '0'), ('on-a', '1'), ('off-b', '0'), ('on-b', '1')]
TIMERS = [('GI', 'gi: GPU cost'), ('GI trace', 'gi: trace pass'),
          ('reflections', 'rf: reflection pass GPU median'),
          ('local shadows', 'ls: local-light shadow pass GPU median'),
          ('RR', r'\[ngx\] gpu RR:'), ('scene build', 'scene build GPU')]


def generate(exe):
    lint_snapshot.load()
    flow.readiness_budget.configure([])
    state = observation_state(str(Path(exe).resolve()), 'ultra')
    state[KEY] = '0'
    estimates = {}
    for slug, save in [('camp', 'Campsite - 27h'), ('inn', 'The Blushing Mermaid - 96h 39m')]:
        name = 'opt-compact-hit-'+slug
        source = flow.read(ROOT/f'tools/live_batches/opt_profile/jobs/opt-profile-{slug}.json')
        job = deepcopy(source)
        quiet = pipeline_readiness.QUIET
        # experimental_specs does not bind 1029: exercise ON explicitly before OFF.
        warm = dict(state, **{KEY: '1'})
        plan = [phase('warmup', flow.SCENE_SETTLE_SECONDS.get(save, 20), set=assignments(warm),
                      wait_log=[r'\[auto\] scene ready', r'\[cfg\] settings read', quiet],
                      wait_log_fresh=[quiet], wait_log_timeout=15)]
        labels = [label+'--static-a' for label, _ in STATES]
        for (label, value), measured in zip(STATES, labels):
            setting = KEY+'='+value
            plan.append(phase('ready-'+label, 1.1, set=setting, wait_log=[quiet],
                              wait_log_fresh=[quiet], wait_log_timeout=8))
            plan.append(phase(measured, 20.1, set=setting, frames=600, settle_frames=0,
                              stats_reset=True, wait_stats=[rx for _, rx in TIMERS],
                              wait_log_timeout=10))
        plan.append(phase('restore', 1.1, set=KEY+'=0', wait_log=[quiet],
                          wait_log_fresh=[quiet], wait_log_timeout=8))
        plan_rel = f'tools/live_batches/opt_r5/compact_hit/plans/{name}.json'
        job_rel = f'tools/live_batches/opt_r5/compact_hit/jobs/{name}.json'
        job.update(name=name, save=save, live_batch='opt-r5-compact-hit', set=assignments(state),
                   args=['-RealPresent', '-Move', '-MovePlan', plan_rel],
                   hold=180, timeout=300, max_minutes=5, fail_fast=True,
                   window_mode='visible_unfocused_fg', stop_on_dump=False,
                   patterns=list(dict.fromkeys([rx for _, rx in TIMERS]+[r'\[watch\]', r'\[cfg\]',
                       r'\[auto\]', r'\[stats\]', 'gi: background builds'])),
                   expect_every=[dict(name=title+' cost', select=rx, match=rx, windows=labels,
                       min=3, period_frames=120, sample_frames=120) for title, rx in TIMERS],
                   expect_runner=[r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                                  rf'\[motion-plan\] complete phases={len(plan)}\b'],
                   live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=0),
                   purpose='READY_FOR_LIVE. Log-only static Ultra GPU A/B; explicit authorization required before staging.',
                   post=(f'python tools/live_batches/opt_r5/compact_hit/analyze.py --job {job_rel} '
                         '--log "{log}" --runner "{runner}" --out "{log}.compact-hit.json"'), post_gate=True)
        job['estimated_phase_seconds'] = round(sum(p['seconds'] for p in plan), 3)
        flow.repair_known_forbids(job, plan)
        flow.write(ROOT/job_rel, job)
        flow.write(ROOT/plan_rel, plan)
        budget = flow.duration_budget(job, plan)
        estimates[slug] = dict(scheduled_seconds=round(budget['seconds'], 3), load_allowance_seconds=120,
                              expected_readiness_seconds=budget['expected_readiness_seconds'],
                              estimated_seconds=round(120+budget['seconds']+budget['expected_readiness_seconds'], 3),
                              nominal_rendered_fps=30,
                              historical_p10_fps=flow.fps_windows.expected(job, state)['fps'],
                              historical_frame_estimate_seconds=round(120+budget['seconds']-80.4+
                                  2400/flow.fps_windows.expected(job, state)['fps']+.4, 3),
                              readiness='Unproven: conditional fresh QUIET waits; hard stop 300 s.')
    flow.write(HERE/'predicted.json', dict(status='READY_FOR_LIVE', key=KEY, spec_constant=1029,
        shader_request_delta=0, launches=2, preset='Ultra release', frames_per_window=600,
        timer_period_frames=120, jobs=estimates,
        prediction='GI trace lower by 0-0.5 ms at camp/inn Ultra; registers 255 -> 243, spill bytes 816 -> 44. '
                   'Other passes unchanged within OFF repeat spread. Image identical (bit-equal offline).',
        offline_proof='docs/progress/gi-livestate.md',
        decision='SAVING only when both ON GI trace medians beat both OFF medians by more than the OFF repeat spread; otherwise NO_SAVING. Missing evidence is INVALID.',
        readiness='No 1029 binding in experimental_specs. Warmup exercises ON, then fresh QUIET after every toggle; no build-generation witness required.',
        pending='Offline only. No launch, queue writes or live saving claim.'))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', required=True)
    generate(ap.parse_args().profile_exe)
