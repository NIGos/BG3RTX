"""Release receipts built with shared smoke phases, hair helpers and live_flow.

Run from the repository root with --profile-exe pointing to compiled profile.cpp.
Never queues or launches. High keeps OMM and masks off and is not in this batch.
"""
import argparse
import hashlib
from copy import deepcopy
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.combine import assignments, settings
from tools.live_batches.hair_qual.make_job import step

HERE = Path(__file__).resolve().parent
COST = ['static-a', 'pan-q', 'pan-e', 'rotate-r', 'rotate-l']


def gate(name, select, match, windows, **kw):
    return dict(name=name, select=select, match=match, windows=windows, min=1, **kw)


def profile(exe, preset):
    return settings(subprocess.check_output([exe, preset], cwd=ROOT, text=True).replace('\n', ','))


def finish(job, plan, state, observable, rule):
    name = job['name']
    # Diagnostics are explicit qualification-only overrides; rendering values stay
    # at the exported launcher profile.
    state.update(headless='0', dump_images=str(int(any(p.get('trace_at') for p in plan))), dump_png_width='1280', dump_raw='0',
                 rt_timing_frames='120', rt_scene_wait_log_frames='120', fg_state_every='30')
    job.update(set=assignments(state), live_batch='release-qual', window_mode='visible_unfocused_fg',
               hold=90, timeout=900, max_minutes=15, fail_fast=True, stop_on_dump=True,
               purpose=observable, estimated_phase_seconds=sum(p['seconds'] for p in plan))
    job['args'] = ['-RealPresent', '-Move', '-MovePlan', f'captures/live_batches/release_qual/plans/{name}.json']
    job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                           rf'\[motion-plan\] complete phases={len(plan)}\b']
    job['expect_runner'] += [rf'\[motion-trace\] phase={i} label={p["label"]} repeat=0'
                             for i, p in enumerate(plan) if p.get('trace_at')]
    job['patterns'] += [r'\[alpha hair\]', r'\[alpha scalp\]', r'\[hair primary\]', r'\[hair groom\]',
                        'occ stats', 'gi: background builds', r'\[watch\]']
    # Absence is valid. A primary/groom producer in this stock-card batch is not.
    job['expect_every'].append(dict(name='stock cards: no strand producer',
        # Activity lines only: the startup capability line "[hair groom] + VK_NV_..." is always logged.
        select=r'\[hair (?:primary|groom)\] (?:frame|coverage|strand(?! variants)|counted)', match=r'(?!)', min=0, event=True))
    flow.write(HERE / name / 'predicted.json', dict(job=name, status='READY_FOR_LIVE',
        observable=observable, pass_rule=rule, profile='data/release.ini + launcher preset via common/settings.h',
        rendering_overrides={},
        input_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                      ('common/settings.h', 'data/release.ini', 'bg3rtx.ini')},
        visual_acceptance=('Inspect the single static PNG for visible cards and face/neck shadows.'
                           if name.startswith('rel3') else 'Log-only release smoke; no captures.')))
    flow.write(HERE / 'plans' / f'{name}.json', plan)
    flow.write(HERE / 'jobs' / f'{name}.json', job)


def smoke(exe):
    job, plan, _ = flow.load_job(ROOT / 'tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json')
    job, plan = deepcopy(job), deepcopy(plan)
    job['name'] = 'rel1-smoke-ultra-s8'
    # Keep the camera coverage and one master OFF/ON cycle; no requalification.
    plan = [p for p in plan if p['label'] in COST + ['warmup', 'ab-off', 'ab-on']]
    for p in plan:
        p.pop('trace_at', None)
        if p['label'] == 'warmup':
            # Use the shared measured scene settle floor instead of the old 45 s.
            p['seconds'] = flow.SCENE_SETTLE_SECONDS['Water Queens House']
            p['motion_start'] = p['motion_end'] = p['seconds']
            p['wait_log'] = [r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready']
    job['expect'] = [rx for rx in job['expect'] if not any(x in rx for x in ('trace', 'exposure held', 'omm'))]
    job['patterns'] = [rx for rx in job['patterns'] if not any(x in rx for x in ('trace', 'omm'))]
    job['forbid'] = [rx for rx in job['forbid'] if 'omm' not in rx]
    job['expect_every'] = [g for g in job['expect_every'] if any(x in g['name'] for x in
        ('water reflections', 'RT capture drops', 'cascade casters buffer', 'scene wait: 0',
         'compose stability', 'non-finite added by water', 'VRAM:')) and 'diagnostic' not in g['name']]
    for g in job['expect_every']:
        g['windows'] = COST
        g.pop('when_present', None)
    job['expect_every'].append(gate('zero alpha region overflow', r'\[geo\] RT capture, last \d+ frames:',
        r'alpha draws made opaque \(region full\) 0\.00,', COST))
    # Water rays prove RT participation in each window; the inherited job-level
    # current-TLAS/GPU-cost expectation remains, without six long cost windows.
    finish(job, plan, profile(exe, 'ultra'),
        'Release Ultra S8 smoke: static, Q/E pans and rotations, water/RT participation and one master toggle; no captures.',
        'Every measured window: water traced/composed and finite, driver-budget VRAM, zero RT capture/alpha overflow, '
        'cascade buffer full 0 and scene timeouts 0. Current TLAS and RT GPU cost must appear. '
        'One ab_off 0->1->0 cycle restores RT. No crash, device loss or strand producer.')


def dialogue(exe):
    source, _, _ = flow.load_job(ROOT / 'tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json')
    name = 'rel3-stock-hair-dialogue1'
    plan = [step('warmup', 35, rt_native_hair='1', rt_native_scalp='1', qa_exposure_hold='0'),
            step('static-a', 24, True, rt_native_hair='1', rt_native_scalp='1'),
            step('static-b', 24, rt_native_hair='1', rt_native_scalp='1'),
            step('settle', 5, rt_native_hair='1', rt_native_scalp='1', rt_occ_stats='0'),
            step('cost-hair', 26, rt_native_hair='1', rt_native_scalp='1', rt_occ_stats='0'),
            step('restore', 2, rt_native_hair='1', rt_native_scalp='1', rt_occ_stats='0', qa_exposure_hold='0')]
    plan[0].update(wait_log=[r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready',
        r'\[watch\].*\| pipelines 0 \(0 ms in driver\)'],
        wait_log_fresh=[r'\[watch\].*\| pipelines 0 \(0 ms in driver\)'], wait_log_timeout=180)
    judged = ['static-a', 'static-b', 'cost-hair']
    job = dict(name=name, save='Test Dialogue1', session=False, traces=0, repeat=1,
        live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=3),
        patterns=deepcopy(source['patterns']),
        expect=[r'\[auto\] scene ready', r'\[hot\] core generation \d+ loaded',
                r'\[ngx\] RR create .*: Success', r'\[trace\] frame \d+ written',
                r'\[geo\] transform feedback enabled: .*\(stride 88\)'],
        forbid=[r'\[crash\] exception 0x', 'VK_ERROR_DEVICE_LOST', r'\[hot\] core (rejected|faulted)',
                'FAILED, keeping previous', 'compile call failed', 'RR creation failed', 'RR evaluate failed'],
        expect_every=[deepcopy(g) for g in source['expect_every'] if any(s in g['name'] for s in
                      ('RT capture drops', 'cascade casters buffer', 'scene wait: 0', 'VRAM:'))])
    for g in job['expect_every']:
        g['windows'] = judged
    job['expect_every'] += [
        gate('native hair coverage active; valid bounds', r'\[alpha hair\] native coverage records \d+;',
             r'native coverage records [1-9]\d*; invalid/missing native state 0;.*retired bounds-invalid primitives 0;', judged),
        gate('native scalp participation', r'\[alpha scalp\] native hard records \d+;',
             r'native hard records (?:[1-9]\d*; native camera dither records \d+|\d+; native camera dither records [1-9]\d*); invalid/missing native state 0;', judged),
        gate('first-enable compilation complete, no skipped pass', r'gi: background builds \(\d+ frames\):',
             r': 0 frames waiting, 0 pending, 0 started', judged),
        gate('GPU cost with stock hair', r'rt passes: GPU cost', r'TLAS this frame [1-9]\d*, previous frame 0, none 0', ['cost-hair']),
        gate('GI cost with stock hair', r'gi: GPU cost', r'gi: GPU cost [0-9.]+ ms/frame', ['cost-hair'])]
    finish(job, plan, profile(exe, 'ultra'),
        'Dialogue1 release Ultra stock cards: startup is first enable; native hair/scalp participate; inspect face/neck shadows in the static PNG.',
        'Every judged window: hair/scalp records > 0, invalid/bounds 0, pending/waiting/started builds 0, no capacity loss or scene timeouts. '
        'Cost windows: RT/GI ms and VRAM within driver budget. '
        'All windows: no strand producer/device loss. One static PNG requires human inspection for visible cards and face/neck shadows; aggregate character counters do not locate the face/neck.')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--profile-exe', required=True)
    a = ap.parse_args()
    smoke(a.profile_exe)
    dialogue(a.profile_exe)
    flow.tighten_files(HERE, cache=HERE / 'offline/corpus.json')
    for path in (HERE / 'jobs').glob('*.json'):
        job, plan, plan_path = flow.load_job(path)
        # Unknown fresh startup readiness must retain the established cold-build
        # allowance; the shared estimator's 2 s no-sample floor is not evidence.
        plan[0]['wait_log_timeout'] = max(180, plan[0].get('wait_log_timeout', 0))
        flow.write(plan_path, plan)
        budget = flow.duration_budget(job, plan)
        prediction = flow.read(HERE / job['name'] / 'predicted.json')
        prediction.update(expected_minutes=round((120 + budget['seconds'] + budget['expected_readiness_seconds']) / 60, 2),
                          load_allowance_s=120, launch_cap_minutes=15,
                          scheduled_seconds=budget['seconds'], expected_readiness_seconds=budget['expected_readiness_seconds'])
        flow.write(HERE / job['name'] / 'predicted.json', prediction)


if __name__ == '__main__':
    main()
