"""Generate one offline-only Dialogue3 attribution job. Never enqueue or launch."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = dict(fg=2, gi_ao=1, rt_shadows=1, gi_strength=1, rr=1,
            rr_post=1, rr_cbt=1, rr_sss=1)
WINDOWS = [('base-a', {}), ('fg-off', {'fg': 0}), ('ao-off', {'gi_ao': 0}),
           ('sun-off', {'rt_shadows': 0}), ('gi-off', {'gi_strength': 0}),
           ('rr-off', {'rr': 0}), ('cbt-off', {'rr_cbt': 0}),
           ('post-late', {'rr_post': 0}), ('base-b', {})]


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def make():
    plan = []

    def step(label, seconds, keys, **extra):
        plan.append(dict(label=label, kind='static', speed='static', seconds=seconds,
                         repeat_ms=0, scan=16, capture_at=0, serial=0,
                         motion_start=seconds, motion_end=seconds, pixels_per_second=0,
                         direction=0, registration_policy='per-frame-exclude-unregistered', set=','.join(f'{k}={v}' for k, v in keys.items()), **extra))

    step('warmup', 20, dict(BASE, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    step('warmup-camera', 20, dict(BASE, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    # Exercise every attribution state before the first captured baseline.
    for label, change in WINDOWS[1:-1]:
        step('warm-' + label, 5, dict(BASE, **change, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    step('warm-base-return', 5, dict(BASE, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    every = []
    for i, (label, change) in enumerate(WINDOWS):
        state = dict(BASE, **change, fg_snap=0, pan_capture=0, qa_exposure_hold=1)
        step('settle-' + label, 5, state)
        step('rendered-' + label, 5, dict(state, pan_capture=98300+i))
        plan[-1].update(capture_at=0.1, serial=98300+i)
        # Separate the presented capture from the rendered readback's QueueWaitIdle.
        step('presented-' + label, 5, dict(state, fg_snap=16))
        every.extend([
            dict(name='K8 '+label, select=rf'\[pan\] burst {98300+i} complete',
                 match=rf'\[pan\] burst {98300+i} complete: 8 frames, 20 samples, transport ok',
                 event=True, min=1, windows=['rendered-'+label]),
            dict(name='P16 '+label, select=r'\[sl\] present capture done',
                 match=r'\[sl\] present capture done', event=True, min=1,
                 windows=['presented-'+label])])
        if label != 'fg-off':
            every.append(dict(name='FG actually presenting '+label,
                              select=r'\[sl\] DLSS-G frame',
                              match=r'status 0x0, presented per app frame 2, displayed/rendered over the last \d+ frames (?:1\.(?:0[1-9]|[1-9]\d)|[2-9]\d*\.\d+).*api errors 0',
                              event=True, settle_s=1, min=1, windows=['presented-'+label]))
    step('restore', 5, dict(BASE, fg_snap=0, pan_capture=0, pan_edge=0,
                          pan_prepare=0, qa_exposure_hold=0))
    startup = dict(preset='ultra', headless=0, fps_cap=0, rt=1, reflex=1,
                   crash_dump=0, rt_stats=0, rt_primary_check=0, rt_primary_identity=0,
                   ls_stats=0, ls_check=0, rf_stats=0, rt_debug=0, ls_debug=0,
                   gi_debug=0, rf_debug=0, rr_show=0, rr_guides_at=1, rr_guides_v2=1,
                   rt_native_hair=0, rt_native_scalp=0, rt_hair_physical_origin=0,
                   pan_prepare=1, pan_edge=1, pan_water=0, pan_inputs=0,
                   pan_capture=0, pan_delay_ms=0, fg_snap=0, fg_snap_any=1,
                   fg_snap_delay_ms=1000, fg_state_every=30, fg_letterbox=1,
                   dump_images=0, dump_raw=0, dump_shaders=0, rt_timing_frames=120,
                   dump_png_width=1280, fg_log_presents=6, fg_snap_delay=0, rt_xfb_mb=1024,
                   **BASE)
    job = dict(name='ef1-dialogue3-attribution', save='Test Dialogue3', session=False,
               live_batch='edge-flicker', window_mode='visible_unfocused_fg', traces=0,
               hold=90, timeout=480,
               live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=1, startup_only=['rt_xfb_mb']), repeat=1, stop_on_dump=False,
               set=','.join(f'{k}={v}' for k, v in startup.items()),
               args=['-RealPresent', '-Move', '-MovePlan',
                     'captures/live_batches/edge_flicker/plans/ef1.json'],
               patterns=[r'\[pan\]', r'\[sl\]', r'\[ngx\]', r'\[hot\]',
                         r'\[watch\]', r'\[pace\]', r'\[cfg\]', r'\[rrpost\]',
                         r'GPU cost', r'FAILED', r'\[crash\]'],
               expect=[r'\[auto\] scene ready', r'\[hot\] core generation \d+ loaded',
                       r'\[ngx\] RR create .*: Success', r'\[cfg\] settings read',
                       r'\[rrpost\] exposure held .*qa_exposure_hold=1'],
               expect_every=every,
               forbid=[r'VK_ERROR_DEVICE_LOST', r'\[crash\] exception 0x',
                       r'RR (creation|evaluate) failed', r'FAILED, keeping previous',
                       r'\[pan\].*(REFUSED|INVALID|failed|dropped)',
                       r'PNG write failed', r'capture writer backlog', r'\[sl\] edge capture REFUSED',
                       r'\[rrpost\] exposure hold REFUSED'],
               expect_runner=[r'\[window-mode\].*visible_unfocused', r'^Run ended: done',
                              rf'\[motion-plan\] complete phases={len(plan)}\b'],
               estimated_phase_seconds=sum(p['seconds'] for p in plan),
               purpose='Offline prepared only. No supported dialogue pause key. Register every captured frame; exclude cuts, never warp. '
                       'Attribution only; retain default native hair. See predicted.json.')
    write(HERE/'plans/ef1.json', plan)
    write(HERE/'jobs/ef1-dialogue3-attribution.json', job)


if __name__ == '__main__':
    make()
    from tools.live_batches.live_flow import tighten_files
    tighten_files(Path(__file__).resolve().parent)
