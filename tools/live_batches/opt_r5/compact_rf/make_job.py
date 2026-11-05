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
KEY = 'rf_compact_probe_state'
STATES = [('off-a', '0'), ('on-a', '1'), ('off-b', '0'), ('on-b', '1')]
TIMERS = [('GI', 'gi: GPU cost'), ('GI trace', 'gi: trace pass'),
          ('reflections', 'rf: reflection pass GPU median'),
          ('local shadows', 'ls: local-light shadow pass GPU median'),
          ('RR', r'\[ngx\] gpu RR:'), ('scene build', 'scene build GPU')]


def generate(exe=None):
    lint_snapshot.load()
    flow.readiness_budget.configure([])
    state = (observation_state(str(Path(exe).resolve()), 'ultra') if exe else
             flow.settings(flow.read(HERE.parent/'compact_hit/jobs/opt-compact-hit-inn.json')['set']))
    state[KEY] = '0'
    # Raw key, no knob. Preserve the compact-hit developer observation profile.
    # Hit shading and hit GI must remain ON in every window or core disables 1031.
    state.update(dev='1', rf_hit_shade='1', rf_hit_gi='1', qa_exposure_hold='1')
    # The trace writes the float pre-post export only with the pan capture path prepared (as opt_r1 scene_holds).
    state.update(pan_prepare='1', pan_edge='1', pan_post='1', pan_capture='0')
    estimates = {}
    for slug, save in [('inn', 'The Blushing Mermaid - 96h 39m'), ('s8', 'Water Queens House')]:
        name = 'opt-compact-rf-'+slug
        source = flow.read(ROOT/f'tools/live_batches/opt_profile/jobs/opt-profile-{slug}.json')
        job = deepcopy(source)
        quiet = pipeline_readiness.QUIET
        # experimental_specs does not bind 1031: exercise ON explicitly before OFF.
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
        # Energy exports after every cost window: a trace capture raised later pass costs when interleaved.
        for label, value in STATES:
            setting = KEY+'='+value
            plan.append(phase('ready-energy-'+label, 1.1, set=setting, wait_log=[quiet],
                              wait_log_fresh=[quiet], wait_log_timeout=8))
            plan.append(phase('energy-'+label, 8, set=setting, trace_at=[1]))
        plan.append(phase('restore', 1.1, set=KEY+'=0,qa_exposure_hold=0', wait_log=[quiet],
                          wait_log_fresh=[quiet], wait_log_timeout=8))
        plan_rel = f'tools/live_batches/opt_r5/compact_rf/plans/{name}.json'
        job_rel = f'tools/live_batches/opt_r5/compact_rf/jobs/{name}.json'
        job.update(name=name, save=save, live_batch='opt-r5-compact-rf', set=assignments(state),
                   args=['-RealPresent', '-Move', '-MovePlan', plan_rel],
                   hold=180, timeout=300, max_minutes=5, fail_fast=True,
                   window_mode='visible_unfocused_fg', stop_on_dump=False,
                   patterns=list(dict.fromkeys([rx for _, rx in TIMERS]+[r'\[trace\]', r'\[dump\]', r'\[watch\]', r'\[cfg\]',
                       r'\[auto\]', r'\[stats\]', 'gi: background builds'])),
                   expect_every=[dict(name=title+' cost', select=rx, match=rx, windows=labels,
                       min=3, period_frames=120, sample_frames=120) for title, rx in TIMERS]+[
                       dict(name=label+' float pre-post export', select=r'\[trace\] frame \d+ written:',
                            match=r'\[trace\] frame \d+ written:', event=True, min=1, windows=['energy-'+label])
                       for label, _ in STATES],
                   expect_runner=[r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                                  rf'\[motion-plan\] complete phases={len(plan)}\b'],
                   live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=1),
                   purpose='READY_FOR_LIVE. Static Ultra GPU A/B and four pre-post float means; explicit authorization required before staging.',
                   post=(f'python tools/live_batches/opt_r5/compact_rf/analyze.py --job {job_rel} '
                         '--log "{log}" --runner "{runner}" --out "{log}.compact-rf.json"'), post_gate=True)
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
    flow.write(HERE/'predicted.json', dict(status='READY_FOR_LIVE', key=KEY, spec_constant=1031,
        shader_request_delta=0, launches=2, preset='Ultra release', frames_per_window=600,
        timer_period_frames=120, jobs=estimates,
        prediction='Reflections lower by 0-0.2 ms at inn/S8; registers 255 -> 245, driver shared bytes 800 -> 32. '
                   'Other passes unchanged within OFF repeat spread. Image identical (construction/SPIR-V proof only).' ,
        offline_proof='docs/progress/rf-ls-regs.md',
        decision='SAVING only when both ON reflection medians beat both OFF medians by more than the OFF repeat spread; otherwise NO_SAVING. Energy outside OFF-derived noise is ENERGY_CHANGE; missing evidence is INVALID.',
        energy='One trace per separate energy phase; reusable rrpost slot-2 float reader. Two OFF repeats derive Student noise band (df=1); both ON versus preceding OFF, RGB and luminance.',
        pairs='Every ON minus preceding OFF and per-window medians retained; aggregate saving requires every ON beat every OFF beyond OFF spread.',
        readiness='No 1031 binding in experimental_specs. Warmup exercises ON, then fresh QUIET after every toggle; no build-generation witness required.',
        pending='Offline only. No launch, queue writes or live saving claim.'))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', help='Optional rebuilt profile exporter; default copies the compact-hit frozen Ultra profile')
    generate(ap.parse_args().profile_exe)
