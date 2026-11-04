"""Prepare four log-only release profiling launches. Never enqueue or launch."""
import argparse
from copy import deepcopy
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot, pipeline_readiness
from tools.live_batches.combine import assignments
from tools.live_batches.release_qual import make_jobs as release, make_final

HERE = Path(__file__).resolve().parent
SCENES = [('s8', 'Water Queens House'), ('camp', 'Campsite - 27h'),
          ('inn', 'The Blushing Mermaid - 96h 39m'), ('dialogue1', 'Test Dialogue1')]
PERIOD = 120
# Three timer periods permit 2-3 complete samples after GPU readback latency.
FRAMES = 3 * PERIOD
BASE_TIMERS = [('RT', 'rt passes: GPU cost'), ('GI', 'gi: GPU cost'),
               ('GI trace', 'gi: trace pass'), ('scene build', 'scene build GPU'),
               ('local shadows', 'ls: local-light shadow pass GPU median'),
               ('reflections', 'rf: reflection pass GPU median'),
               ('RR', r'\[ngx\] gpu RR:'), ('post', r'\[rrpost\] gpu:'),
               ('FG copy', r'\[sl\] gpu FG input copy:'), ('CPU/driver', r'\[watch\]')]
WATER_TIMERS = [('water '+p, rf'\[water\] {p} GPU') for p in ('build', 'reflections', 'compose')]


def observation_state(exe, preset):
    state = release.profile(exe, preset)
    state.update(detailed_logging='1', cpu_timing='1', headless='0', rt_stats_period_frames=str(PERIOD),
                 rt_timing_frames=str(PERIOD), rt_scene_wait_log_frames=str(PERIOD),
                 dump_images='0', dump_raw='0', dump_preview_f32='0', dump_shaders='0',
                 rt_stats='0', rt_primary_check='0', rt_occ_stats='0', ls_stats='0',
                 rf_stats='0', ls_check='0', rt_debug='0', gi_debug='0', ls_debug='0', rf_debug='0')
    return state


def phase(label, seconds, **kw):
    return dict(label=label, kind='static', speed='static', seconds=seconds, repeat_ms=0,
                scan=16, capture_at=0, serial=0, motion_start=0, motion_end=seconds,
                pixels_per_second=0, direction=0, **kw)


def generate(exe):
    lint_snapshot.load()
    predictions = {}
    source = flow.read(make_final.HERE/'jobs/rel1-smoke-ultra-s8.json')
    profiles = {p: observation_state(exe, p) for p in ('ultra', 'high')}
    for slug, save in SCENES:
        name = 'opt-profile-'+slug
        plan, every = [], []
        for n, (preset, state) in enumerate(profiles.items()):
            warm = phase(preset+'--warmup', flow.SCENE_SETTLE_SECONDS.get(save, 29) if not n else 2,
                         set=assignments(state))
            warm.update(wait_log=[r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready'] if not n else [],
                        wait_log_timeout=60 if not n else 10)
            plan.append(warm)
            labels = [preset+'--static-a', preset+'--pan-q']
            timers = list(BASE_TIMERS)
            if state['rt_water_geometry'] != '0':
                # No surface in a given view is a reported absence, never zero.
                timers += WATER_TIMERS
            if state['rt_emissive'] != '0':
                timers += [('emissive', r'\[em\] per build:')]
            for title, select in timers:
                optional = title.startswith('water ') or title == 'emissive'
                every.append(release.gate(title+' cost', select, select, labels,
                             period_frames=PERIOD, sample_frames=PERIOD,
                             **({'when_present': True} if optional else {})))
                every[-1]['min'] = 2
                if title == 'CPU/driver':
                    every[-1]['match'] = r'submit CPU (?!0\.000\b)[\d.]+ ms'
            for label in labels:
                p = phase(label, 20, set='detailed_logging=1')
                if label.endswith('pan-q'):
                    # Held-key duty cycle controls angular speed; key repeat
                    # alone cannot slow BG3's camera (camera-plan.ps1).
                    p.update(kind='pan', speed='slow', repeat_ms=100, direction=-1,
                             key_period_ms=400, key_down_ms=100)
                plan.append(p)
        job = dict(name=name, save=save, session=False, traces=0, repeat=1, hold=90,
                   timeout=300, max_minutes=5, window_mode='visible_unfocused_fg',
                   live_batch='opt-profile', fail_fast=True, stop_on_dump=True,
                   set=assignments(profiles['ultra']),
                   args=['-RealPresent', '-Move', '-MovePlan', f'tools/live_batches/opt_profile/plans/{name}.json'],
                   patterns=list(dict.fromkeys([rx for _, rx in BASE_TIMERS+WATER_TIMERS] +
                       [r'\[em\]', r'\[cpu\]', r'\[auto\]', r'\[stats\]', r'\[cfg\]', 'gi: background builds'])),
                   expect=[r'\[auto\] scene ready', r'\[hot\] core generation \d+ loaded',
                           r'\[ngx\] RR create .*: Success'],
                   forbid=deepcopy(source['forbid']), expect_every=every,
                   expect_runner=[r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                                  rf'\[motion-plan\] complete phases={len(plan)}\b'],
                   live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=0))
        job['live_flow']['segments'] = [dict(offset=3*n, count=3, set=assignments(state),
            live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=0))
            for n, state in enumerate(profiles.values())]
        # Explicit cfg witnesses for CPU/unknown keys. Pipeline families come
        # solely from target-state requirements, including disabled pass rules.
        for p, wanted in zip(plan, pipeline_readiness.requirements(job, plan)):
            for key in wanted:
                if key not in pipeline_readiness.TABLE:
                    value = flow.settings(p['set'])[key]
                    p.setdefault('wait_log', []).append(r'\[cfg\] keys:.*\b'+re.escape(key)+'='+re.escape(value)+r'(?:\s|$)')
        make_final.retime_final(job, plan)
        for p, state in zip(plan, flow.phase_states(job, plan)):
            if p.get('wait_stats'):
                p.update(frames=FRAMES, settle_frames=0, stats_reset=True)
                p['seconds'] = round(FRAMES/flow.fps_windows.expected(job, state)['fps']+.1, 3)
                p['motion_end'] = p['seconds']
                p['wait_log_timeout'] = 15
        job['estimated_phase_seconds'] = round(sum(p['seconds'] for p in plan), 3)
        job['post'] = (f'python tools/live_batches/opt_profile/analyze.py --job tools/live_batches/opt_profile/jobs/{name}.json '
                       '--log "{log}" --runner "{runner}" --out "{log}.profile.md"')
        job['post_gate'] = True
        flow.write(HERE/'jobs'/f'{name}.json', job)
        flow.write(HERE/'plans'/f'{name}.json', plan)
        budget = flow.duration_budget(job, plan)
        predictions[name] = dict(save=save, status='READY_FOR_LIVE', hard_timeout_seconds=300,
            expected_minutes=round((120+budget['seconds']+budget['expected_readiness_seconds'])/60, 2),
            scheduled_seconds=budget['seconds'], readiness_seconds=budget['expected_readiness_seconds'],
            readiness_unproven=[w['window'] for w in budget['windows'] if w['readiness_timeout_seconds'] and not w['readiness_samples']],
            cost_windows=[p['label'] for p in plan if p.get('frames')],
            predicted_delta='Measurement only: no rendering change, no claimed speedup or quality pass.')
    flow.write(HERE/'predicted.json', dict(status='READY_FOR_LIVE', launches=4, shader_request_delta=0,
        queue_glob='tools/live_batches/opt_profile/jobs/opt-profile-*.json', load_allowance_seconds=120,
        frames_per_window=FRAMES, period_frames=PERIOD, acknowledged_periods=1,
        expected_samples='2-3 complete periods per active timer/window; missing samples invalidate active costs.',
        captures=0, diagnostics=0, profiles=profiles, jobs=predictions,
        pending='Offline preparation only. Coordinator commits; explicit batch authorization before staging.'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', required=True)
    generate(ap.parse_args().profile_exe)


if __name__ == '__main__':
    main()
