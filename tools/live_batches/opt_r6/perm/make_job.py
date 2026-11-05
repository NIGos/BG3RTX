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
KEY = 'gi_lane_permute'
KEY2 = 'gi_ao_r16f'
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
    state[KEY2] = '0'
    # Developer raw keys are gated in core when dev=0, so the A/B needs dev=1
    # (both windows share it; the release profile is otherwise unchanged).
    state['dev'] = '1'
    estimates = {}
    for slug, save in [('s8', 'Water Queens House'), ('inn', 'The Blushing Mermaid - 96h 39m')]:
        name = 'opt-perm-'+slug
        source = flow.read(ROOT/f'tools/live_batches/opt_profile/jobs/opt-profile-{slug}.json')
        job = deepcopy(source)
        quiet = pipeline_readiness.QUIET
        # experimental_specs does not bind 1032/1033: exercise ON explicitly before OFF.
        warm = dict(state, **{KEY: '1', KEY2: '1'})
        plan = [phase('warmup', flow.SCENE_SETTLE_SECONDS.get(save, 20), set=assignments(warm),
                      wait_log=[r'\[auto\] scene ready', r'\[cfg\] settings read', quiet],
                      wait_log_fresh=[quiet], wait_log_timeout=15)]
        labels = [label+'--static-a' for label, _ in STATES]
        for (label, value), measured in zip(STATES, labels):
            setting = KEY+'='+value+','+KEY2+'='+value
            plan.append(phase('ready-'+label, 1.1, set=setting, wait_log=[quiet],
                              wait_log_fresh=[quiet], wait_log_timeout=8))
            plan.append(phase(measured, 20.1, set=setting, frames=600, settle_frames=0,
                              stats_reset=True, wait_stats=[rx for _, rx in TIMERS],
                              wait_log_timeout=10))
        plan.append(phase('restore', 1.1, set=KEY+'=0,'+KEY2+'=0', wait_log=[quiet],
                          wait_log_fresh=[quiet], wait_log_timeout=8))
        plan_rel = f'tools/live_batches/opt_r6/perm/plans/{name}.json'
        job_rel = f'tools/live_batches/opt_r6/perm/jobs/{name}.json'
        job.update(name=name, save=save, live_batch='opt-r6-perm', set=assignments(state),
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
                   post=(f'python tools/live_batches/opt_r6/perm/analyze.py --job {job_rel} '
                         '--log "{log}" --runner "{runner}" --out "{log}.perm.json"'), post_gate=True)
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
    flow.write(HERE/'predicted.json', dict(status=('READY_FOR_LIVE' if all(k in pipeline_readiness.TABLE for k in (KEY, KEY2)) else 'READY_FOR_TOOLING_INTEGRATION'), key=KEY, spec_constants=[1032, 1033], keys=[KEY, KEY2],
        shader_request_delta=0, launches=2, preset='Ultra release', frames_per_window=600,
        timer_period_frames=120, jobs=estimates,
        prediction='GI trace lower by 0-0.40 ms; scalar AO fold/local reads lower by 0-0.06 ms. Other passes within OFF spread.',
        offline_proof='docs/progress/gi-perm.md',
        decision='SAVING only when both ON GI trace medians beat both OFF medians by more than the OFF repeat spread; otherwise NO_SAVING. Missing evidence is INVALID.',
        readiness='No 1032/1033 binding in experimental_specs. Warmup exercises ON, then fresh QUIET after every toggle; no build-generation witness required.',
        pending='Readiness registry integration is required when status is READY_FOR_TOOLING_INTEGRATION. Offline only; no launch, queue writes or live saving claim.'))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', required=True)
    generate(ap.parse_args().profile_exe)
