"""Reuse sh2's GI A/B job and the shared retimer. Offline preparation only."""
from pathlib import Path
from tools.live_batches import live_flow as flow

HERE = Path(__file__).resolve().parent
STATES = [('off-a', 2), ('on-a', 1), ('off-b', 2), ('on-b', 1)]


def make():
    job = flow.read(HERE.parent/'sharc_far/jobs/sh2-s8-fg2.json')
    # Start with the actual release preset, not sh2's historical feature overrides.
    startup = dict(preset='ultra', fg=2, rr=1, headless=0, fps_cap=0,
                   rt_stats=0, rt_primary_check=0, ls_stats=0, ls_check=0,
                   rf_stats=0, rt_debug=0, ls_debug=0, gi_debug=0, rf_debug=0,
                   rr_show=0, crash_dump=0, rt_timing_frames=120,
                   rt_scene_wait=1, rt_scene_wait_log_frames=120,
                   gi_rr_rays=2, qa_exposure_hold=0,
                   dump_images=1, dump_raw=0, dump_shaders=0, dump_png_width=1280,
                   pan_prepare=1, pan_edge=1, pan_water=0, pan_inputs=0,
                   pan_capture=0, pan_delay_ms=0, fg_snap=0)
    plan = []
    def step(label, seconds, **keys):
        p = dict(label=label, kind='static', speed='static', seconds=seconds,
                 repeat_ms=0, scan=16, capture_at=0, serial=0,
                 motion_start=seconds, motion_end=seconds,
                 pixels_per_second=0, direction=0,
                 set=','.join(f'{k}={v}' for k,v in keys.items()))
        plan.append(p)
        return p
    step('warmup', 17, gi_rr_rays=2, qa_exposure_hold=0)
    step('prefetch-one-ray', 3, gi_rr_rays=1)
    step('prefetch-two-rays', 3, gi_rr_rays=2)
    every = []
    costs, shots, measured = [], [], []
    for i, (label, rays) in enumerate(STATES):
        cost, shot, pan = 'cost-'+label, 'shot-rays-'+label, 'pan-'+label
        costs.append(cost); shots.append(shot); measured.extend([cost, shot, pan])
        state = dict(gi_rr_rays=rays, qa_exposure_hold=1, pan_capture=0)
        step(cost, 24, **state)
        step(shot, 17, **state).update(trace_at=[1, 6, 11])
        # K8 edge transport from edge_flicker: depth/final each frame,
        # material/albedo endpoints. Held Q, followed by the same-duration E return.
        serial = 99100+i
        step(pan, 5, **dict(state, pan_capture=serial)).update(
            kind='rotate', speed='slow', turn_mode='keys', direction=-1,
            pixels_per_second=1, motion_start=0, motion_end=3,
            capture_at=.2, serial=serial,
            registration_policy='per-frame-exclude-unregistered')
        step('return-'+label, 3, qa_exposure_hold=1, pan_capture=0).update(
            kind='rotate', speed='slow', turn_mode='keys', direction=1,
            pixels_per_second=1, motion_start=0, motion_end=3)
        every.append(dict(name='K8 '+label, select=rf'\[pan\] burst {serial} complete',
                          match=rf'\[pan\] burst {serial} complete: 8 frames, 20 samples, transport ok',
                          event=True, min=1, windows=[pan]))
        every.append(dict(name='GI rays '+label, select=r'gi: trace pass',
                          match=rf'gi: trace pass \(ray-traced, gi_rt=1, {rays} rays/sample, full res, RR denoises\) GPU median [0-9.]+ ms',
                          period_frames=300, min=1, windows=[cost]))
        every.append(dict(name='float traces '+label, select=r'\[trace\] frame \d+ written:',
                          match=r'\[trace\] frame \d+ written:', event=True, min=3, windows=[shot]))
    step('restore', 1, gi_rr_rays=2, qa_exposure_hold=0, pan_capture=0, pan_prepare=0, pan_edge=0)
    # Preserve the established capacity and scene-wait gates, extended to shots.
    for gate in job['expect_every']:
        gate['windows'] = costs + shots
        every.append(gate)
    every.append(dict(name='zero scene-wait timeouts throughout measurement',
                      select=r'\[rt\] scene wait: \d+ frames',
                      match=r'got this frame\x27s scene, 0 timeouts',
                      event=True, min=0, windows=measured))
    job.update(name='gi1-ultra-1ray', live_batch='gi-1ray-qual', hold=90,
               timeout=354, max_minutes=5.9, window_mode='visible_unfocused_fg',
               set=','.join(f'{k}={v}' for k,v in startup.items()),
               args=['-RealPresent', '-Move', '-MovePlan', 'captures/live_batches/gi_1ray_qual/plans/gi1.json'],
               live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=3),
               expect_every=every, stop_on_dump=False,
               purpose='One-ray GI release qualification. Ultra 2/1/2/1, static and held Q/E. Prepared only; no preset changes.',
               post='py -3 -m tools.live_batches.gi_1ray_qual.analyze "{log}"', post_gate=True)
    job['patterns'] += [r'\[pan\]', r'\[rt\] scene wait']
    job['expect'] = [r'\[auto\] scene ready', r'\[hot\] core generation \d+ loaded',
                     r'\[ngx\] RR create .*: Success', r'\[cfg\] settings read',
                     r'\[rrpost\] exposure held \(qa_exposure_hold=1\)']
    job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                            rf'\[motion-plan\] complete phases={len(plan)}\b']
    for i,p in enumerate(plan):
        for j,_ in enumerate(p.get('trace_at', [])):
            job['expect_runner'].append(rf'\[motion-trace\] phase={i} label={p["label"]} repeat={j}')
    job['forbid'] += [r'\[pan\].*(REFUSED|INVALID|failed|dropped)', r'\[rrpost\] exposure hold REFUSED']
    job['estimated_phase_seconds'] = sum(p['seconds'] for p in plan)
    flow.write(HERE/'plans/gi1.json', plan)
    flow.write(HERE/'jobs/gi1-ultra-1ray.json', job)
    flow.tighten_files(HERE)
    # Keep the retimer's readiness allowance; test-queue caps actual execution
    # at max_minutes (354 s), including load, and fails closed if it runs long.


if __name__ == '__main__':
    make()
