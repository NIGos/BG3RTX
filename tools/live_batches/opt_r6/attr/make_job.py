"""Prepare two diagnostic GPU attribution jobs offline, without staging or launching."""
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
BASE = {'gi_hit_sun': '1', 'gi_hit_direct': '1', 'hit_ao_rays': '2', 'gi_far': '1'}
STATES = [('base-a', {}), ('sun-off', {'gi_hit_sun': '0'}), ('base-b', {}),
          ('direct-off', {'gi_hit_direct': '0', 'gi_hit_sun': '0'}), ('base-c', {}),
          ('ao-off', {'hit_ao_rays': '0'}), ('base-d', {}),
          ('near-only', {'gi_far': '0'}), ('base-e', {}), ('base-f', {})]
# Keep base-f as a final drift receipt even though restart-only OMM is skipped.
FRAMES = 480


def states_for(slug):
    # Drop only the S8 AO ablation; retain every baseline drift receipt.
    return [(label, values) for label, values in STATES
            if slug != 's8' or label != 'ao-off']


TIMERS = [('GI', 'gi: GPU cost'), ('GI trace', 'gi: trace pass'),
          ('origin', 'gi: origin pass GPU cost'), ('sun trace', 'gi: sun trace pass GPU cost'),
          ('reflections', 'rf: reflection pass GPU median'),
          ('local shadows', 'ls: local-light shadow pass GPU median'),
          ('RR', r'\[ngx\] gpu RR:'), ('scene build', 'scene build GPU')]


def generate(exe=None):
    lint_snapshot.load()
    flow.readiness_budget.configure([])
    state = (observation_state(str(Path(exe).resolve()), 'ultra') if exe else
             flow.settings(flow.read(ROOT/'tools/live_batches/opt_r5/compact_hit/jobs/opt-compact-hit-inn.json')['set']))
    state.update(BASE)
    # Keep raw developer settings live instead of applying release-policy overrides.
    state['dev'] = '1'
    estimates = {}
    for slug, save in [('inn', 'The Blushing Mermaid - 96h 39m'), ('s8', 'Water Queens House')]:
        name = 'opt-attr-'+slug
        source = flow.read(ROOT/f'tools/live_batches/opt_profile/jobs/opt-profile-{slug}.json')
        job = deepcopy(source)
        quiet = pipeline_readiness.QUIET
        # Warm Ultra, then reconcile cached or newly selected pipelines at each transition.
        warm = dict(state)
        plan = [phase('warmup', flow.SCENE_SETTLE_SECONDS.get(save, 20), set=assignments(warm),
                      wait_log=[r'\[auto\] scene ready', r'\[cfg\] settings read', quiet],
                      wait_log_fresh=[quiet], wait_log_timeout=15)]
        states = states_for(slug)
        for label, overrides in STATES:
            if overrides:
                plan.append(phase('warmup-'+label, 1.1, set=assignments(dict(BASE, **overrides)),
                                  wait_log=[quiet], wait_log_fresh=[quiet], wait_log_timeout=60))
        plan.append(phase('warmup-baseline', 1.1, set=assignments(BASE),
                          wait_log=[quiet], wait_log_fresh=[quiet], wait_log_timeout=60))
        labels = [label+'--static-a' for label, _ in states]
        for (label, overrides), measured in zip(states, labels):
            setting = assignments(dict(BASE, **overrides))
            plan.append(phase('ready-'+label, 1.1, set=setting, wait_log=[quiet],
                              wait_log_fresh=[quiet], wait_log_timeout=8))
            plan.append(phase(measured, 16.1, set=setting, frames=FRAMES, settle_frames=0,
                              stats_reset=True, wait_stats=[rx for _, rx in TIMERS],
                              wait_log_timeout=10))
        plan.append(phase('restore', 1.1, set=assignments(BASE), wait_log=[quiet],
                          wait_log_fresh=[quiet], wait_log_timeout=8))
        plan_rel = f'tools/live_batches/opt_r6/attr/plans/{name}.json'
        job_rel = f'tools/live_batches/opt_r6/attr/jobs/{name}.json'
        job.update(name=name, save=save, live_batch='opt-r6-attr', set=assignments(state),
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
                   purpose='BLOCKED_DURATION. ATTRIBUTION diagnostic: duration/readiness budget unresolved; image changes expected and allowed; nothing ships; explicit authorization required before staging.',
                   post=(f'python tools/live_batches/opt_r6/attr/analyze.py --job {job_rel} '
                         '--log "{log}" --runner "{runner}" --out "{log}.attr.md"'), post_gate=True)
        job['estimated_phase_seconds'] = round(sum(p['seconds'] for p in plan), 3)
        flow.repair_known_forbids(job, plan)
        flow.write(ROOT/job_rel, job)
        flow.write(ROOT/plan_rel, plan)
        budget = flow.duration_budget(job, plan)
        estimates[slug] = dict(scheduled_seconds=round(budget['seconds'], 3), load_allowance_seconds=120,
                              expected_readiness_seconds=budget['expected_readiness_seconds'],
                              estimated_seconds=round(120+budget['seconds']+budget['expected_readiness_seconds'], 3),
                              historical_frame_estimate_seconds=round(120+budget['seconds']-16.1*len(states)+
                                  FRAMES*len(states)/flow.fps_windows.expected(job, state)['fps'], 3),
                              nominal_rendered_fps=30,
                              historical_p10_fps=flow.fps_windows.expected(job, state)['fps'],
                              readiness='Unproven: conditional fresh QUIET waits; hard stop 300 s.')
    # Add only new readiness queries; never refresh frozen evidence.
    lint_snapshot.extend_readiness(sorted((HERE/'jobs').glob('*.json')))
    flow.write(HERE/'predicted.json', dict(status='BLOCKED_DURATION', kind='ATTRIBUTION',
        shader_request_delta=0, launches=2, preset='Ultra observation_state, dev=1',
        frames_per_window=FRAMES, timer_period_frames=120, jobs=estimates,
        reference_gi_trace_ms=dict(inn=12.8, s8=8.0),
        live_keys=list(BASE), skipped={'s8_ao_off': 'Omitted to fit the five-minute nominal launch budget; all baselines retained.',
            'rt_omm': 'Restart required: common/settings.h kRestart; '
            'layer/omm_backend.cpp device creation; layer/omm_content.cpp runtime off invalidates content proof until restart.'},
        prediction='Ablations should reduce GI trace; magnitude unknown. Report variant minus adjacent baseline mean, '
                   'and following minus preceding baseline drift, for all eight timers. Effects interact; deltas are not additive.',
        direct_off='gi_hit_direct=0 bypasses hit_gi_dispatch (core/gi.cpp); hit sun, AO, emission and far bounce disappear. '
                   'gi_hit_sun=0 is explicit, redundant for dispatch. This is not isolated local-light cost.',
        sun_off='Also disables far bounce (far_on requires sun). near-only isolates far distance with sun retained.',
        scope='Diagnostic only. Image and energy changes expected and allowed. Nothing ships; no quality or saving gate.',
        pending='Offline only. Inn nominal duration exceeds 300 s including load; both jobs fail timeout allowance lint. '
            'Resolve launch partition/readiness budget before staging. No launch or queue writes.'))



if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', help='Optional profile exporter; default uses compact-hit frozen Ultra observation_state')
    generate(ap.parse_args().profile_exe)

