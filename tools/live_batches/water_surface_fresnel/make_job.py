"""Prepare M5.6 sun-footprint A/B only; never enqueue or launch BG3.

The checked-in step-2 wsf1 job supplies the qualified Ultra/queue contracts.
This generator adds wsg1 without changing that independent shading-model test.
"""
import copy
import json
from pathlib import Path

from tools.live_batches.live_flow import repair_known_forbids, tighten_files, write

HERE = Path(__file__).resolve().parent
FPS = 30
PERIOD = 120 / FPS
APPLY = 1
STALL = 1
# Strictly exceed apply + two complete intervals + serialized trace stalls.
STATIC = APPLY + 2 * PERIOD + 3 * STALL + .1
COST = APPLY + 2 * PERIOD + .1
BURST = 5  # 2 s delay + 9 rendered records / 30 fps + writer allowance.


def make():
    job = json.loads((HERE / 'jobs/wsf1-s8-interface-column.json').read_text())
    startup = dict(v.split('=', 1) for v in job['set'].split(','))
    startup.update(rt_water_surface_fresnel='1', rt_water_surface_glint='1',
                   pan_prepare='1', pan_water='1', pan_water_frames='8', pan_delay_ms='2000')
    plan = []

    def step(label, seconds, on=0, direction=0, **extra):
        state = dict(rt_water_surface_fresnel=1, rt_water_surface_glint=on,
                     qa_exposure_hold=1, pan_capture=0)
        state.update(extra.pop('state', {}))
        plan.append(dict(label=label, kind='rotate' if direction else 'static',
                         speed='slow' if direction else 'static', seconds=seconds,
                         repeat_ms=0, scan=16, capture_at=0, serial=0,
                         motion_start=0 if direction else seconds, motion_end=seconds,
                         pixels_per_second=1 if direction else 0, direction=direction,
                         set=','.join(f'{k}={v}' for k, v in state.items()), **extra))
        if direction:
            plan[-1]['turn_mode'] = 'keys'

    step('warmup', 25, state=dict(qa_exposure_hold=0))
    step('prefetch-glint-on', 5, on=1)
    static_labels = []
    for i, on in enumerate((0, 1, 0, 1)):
        label = f'glint-static-{i}'
        step(label, STATIC, on, trace_at=[5, 8, 11])
        static_labels.append(label)
    turn_labels = []
    for direction, key in ((1, 'E'), (-1, 'Q')):
        for i, on in enumerate((0, 1, 0, 1)):
            label = f'glint-turn-{key}{i}'
            serial = 98920 + (0 if key == 'E' else 4) + i
            # Start holding the key before the delayed raw burst. Compare
            # within-burst second differences, never pixels from different pans.
            step(label, BURST, on, direction, state=dict(pan_capture=serial))
            turn_labels.append((label, serial))
    cost_labels = []
    for i, on in enumerate((0, 1, 0, 1)):
        label = f'glint-cost-{i}'
        step(label, COST, on, 1 if i < 2 else -1)
        cost_labels.append(label)
    step('restore', 5, state=dict(rt_water_surface_fresnel=0, qa_exposure_hold=0,
                                pan_prepare=0, pan_water=0, pan_delay_ms=0))
    periodic = []
    for gate in job['expect_every']:
        if gate.get('event') or 'surface' in gate['name'] or 'coverage-weighted' in gate['name']:
            continue
        g = copy.deepcopy(gate)
        # Burst windows are observations, never cost/statistics windows.
        g['windows'] = static_labels + cost_labels
        periodic.append(g)
    for gate in job['expect_every'][-2:]:
        g = copy.deepcopy(gate)
        g['windows'] = static_labels + cost_labels  # Fresnel fixed ON throughout.
        periodic.append(g)
    periodic.append(dict(name='three finite post-water float shots per static state',
                         select=r'\[trace\] frame \d+ written:', match=r'\[trace\] frame \d+ written:',
                         event=True, min=3, settle_s=1, windows=static_labels))
    for label, serial in turn_labels:
        periodic.append(dict(name=f'raw K8 water burst {serial}', select=rf'\[pan\] burst {serial} complete',
                             match=rf'\[pan\] burst {serial} complete: 8 frames, 16 samples, transport ok',
                             event=True, min=1, windows=[label]))
    job.update(name='wsg1-s8-sun-glint', live_batch='water-surface-glint',
               candidate_status='BLOCKED_OFFLINE_REFERENCE', candidate_implemented=False,
               purpose='BLOCKED_OFFLINE_REFERENCE: future M5.6 direct sun footprint 0/1/0/1 static and held E/Q in one S8 Ultra run; Fresnel coverage stays on. No production glint setting or specialization exists. Prepared protocol only; not a runnable candidate; no launch or queue authorization.',
               set=','.join(f'{k}={v}' for k, v in startup.items()),
               args=['-RealPresent', '-Move', '-MovePlan', 'captures/live_batches/water_surface_fresnel/plans/wsg1_s8.json'],
               estimated_phase_seconds=round(sum(p['seconds'] for p in plan), 3),
               expect_every=periodic,
               expect_runner=[r'\[window-mode\].*visible_unfocused', r'^Run ended: done',
                              rf'\[motion-plan\] complete phases={len(plan)}\b'],
               post='py -3 tools/water_glint_gate.py "{log}"', post_gate=True)
    for index, p in enumerate(plan):
        if p['kind'] == 'rotate':
            job['expect_runner'].append(rf'\[rotation-input\] phase={index} mode=keys label={p["label"]} direction={p["direction"]}\b')
    repair_known_forbids(job, plan)
    write(HERE / 'plans/wsg1_s8.json', plan)
    write(HERE / 'jobs/wsg1-s8-sun-glint.json', job)
    tighten_files(HERE / 'jobs/wsg1-s8-sun-glint.json')
    # Record the actual corpus-derived artifacts, not the 30 FPS seed bounds.
    job = json.loads((HERE / 'jobs/wsg1-s8-sun-glint.json').read_text())
    plan = json.loads((HERE / 'plans/wsg1_s8.json').read_text())
    durations = {p['label']: p['seconds'] for p in plan}
    predicted = json.loads((HERE / 'predicted.json').read_text())
    predicted['sun_glint_step_1'] = dict(
        status='BLOCKED_OFFLINE_REFERENCE', job=job['name'], key='rt_water_surface_glint',
        implementation='Not implemented: dense native peak-lobe footprint reference rejects 32x32 quadrature in 5/12 fixtures (7/12 pass); 16x16 passes only 1/12. No production setting or specialization exists. This is a future protocol only; do not submit it.',
        fixed='rt_water_surface_fresnel=1; real Ultra; no reflection trace or roughness change',
        prediction='Broader direct sun footprint, fewer isolated flashing glints, unchanged integrated lobe energy. No measured improvement claimed.',
        energy='All 12 static shots: full-resolution post-water RR float luminance / valid held exposure. Every ON shot must match zero energy change within same-run OFF SD and existing family-wise Student t bound; no PNG/bloom means.',
        motion='Two OFF and two ON K8 raw water bursts for each held E and Q. Residual is mean absolute second difference inside each burst, restricted to common exact water record coverage. Every ON residual must improve beyond same-direction OFF/OFF spread. No cross-pan pixel subtraction or physical correspondence claimed; smooth-turn residual is a proxy, reject changed framing.',
        cost='Separate capture-free 120-frame cost windows. Compose timing excludes forward FS filter cost; report that limit. Original +0.02-0.10 ms estimate is unmeasured and may be exceeded by extra normal taps.',
        duration=dict(fps=FPS, producer_frames=120, settings_latency_s=APPLY, trace_stall_s=STALL,
                      static_s=durations['glint-static-0'], static_derivation='strictly exceed 1 + 2*(120 / observed FPS) + 3*1; shared live_flow uses recent valid state-matched FPS corpus',
                      burst_s=BURST, burst_derivation='2 s delay + 9/30 s records + writer allowance, no cost judgement',
                      cost_s=durations['glint-cost-0'], cost_derivation='strictly exceed 1 + 2*(120 / observed FPS); no capture stalls in cost phases',
                      total_s=job['estimated_phase_seconds'], traces=12, raw_bursts=8, approximate_disk_mb=1800,
                      readiness='Shared generator adds producer sanity after warmup and explicit prefetch/scene waits; timeout includes those bounded readiness waits.'),
        limits='Reference/encoding bound comes from dependent offline native-BRDF footprint tests; runtime residual alone cannot qualify the model. Missing/invalid inputs or zero ownership means INVALID, not an improvement.',
        restore='Last phase sets glint=0, Fresnel=0, exposure hold=0 and releases capture preparation; serial queue restores original INI.')
    write(HERE / 'predicted.json', predicted)
    print(f'Prepared {job["name"]}: {job["estimated_phase_seconds"]} scheduled seconds; nothing queued')


if __name__ == '__main__':
    make()
