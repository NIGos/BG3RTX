"""Prepare ef3 outside all watched inboxes. Never enqueue or launch."""
import json
from pathlib import Path
from tools.live_batches.edge_flicker.make_job import BASE as OLD_BASE, write
HERE = Path(__file__).resolve().parent
BASE = dict(OLD_BASE, rr_hair_resolve=0, fg_snap_full=1)
WINDOWS = [('base-a', {}), ('post-late-a', {'rr_post': 0}),
           ('base-b', {}), ('post-late-b', {'rr_post': 0}),
           ('resolve-off-a', {}), ('resolve-on-a', {'rr_hair_resolve': 1}),
           ('resolve-off-b', {}), ('resolve-on-b', {'rr_hair_resolve': 1})]


def make():
    old = json.loads((HERE.parent/'edge_flicker/jobs/ef1-dialogue3-attribution.json').read_text())
    plan, every, traces = [], [], []
    def step(label, seconds, keys, **extra):
        plan.append(dict(label=label, kind='static', speed='static', seconds=seconds,
                         repeat_ms=0, scan=16, capture_at=0, serial=0,
                         motion_start=seconds, motion_end=seconds, pixels_per_second=0,
                         direction=0, registration_policy='integer-subwindow-rigid-residual',
                         set=','.join(f'{k}={v}' for k,v in keys.items()), **extra))
    for label in ('warmup', 'warmup-camera'):
        step(label, 20, dict(BASE, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    for i, (label, change) in enumerate(WINDOWS):
        state = dict(BASE, **change, fg_snap=0, pan_capture=0, qa_exposure_hold=1)
        step('settle-'+label, 5, state)
        step('rendered-'+label, 5, dict(state, pan_capture=98500+i))
        plan[-1].update(capture_at=0.1, serial=98500+i)
        step('presented-'+label, 5, dict(state, fg_snap=16))
        step('energy-'+label, 5, state, trace_at=[1])
        phase = len(plan)-1
        traces.append(rf'\[motion-trace\] phase={phase} label=energy-{label} repeat=0')
        step('timing-'+label, 10, state)
        every.extend([
            dict(name='K8 '+label, select=rf'\[pan\] burst {98500+i} complete',
                 match=rf'\[pan\] burst {98500+i} complete: 8 frames, 20 samples, transport ok',
                 event=True, min=1, windows=['rendered-'+label]),
            dict(name='P16 '+label, select=r'\[sl\] present capture done',
                 match=r'\[sl\] present capture done', event=True, min=1, windows=['presented-'+label]),
            dict(name='float stats '+label, select=r'\[trace\] frame \d+ written:',
                 match=r'\[trace\] frame \d+ written:', event=True, min=1, windows=['energy-'+label]),
            dict(name='FG actually presenting '+label, select=r'\[sl\] DLSS-G frame',
                 match=r'status 0x0, presented per app frame 2, displayed/rendered over the last \d+ frames (?:1\.(?:0[1-9]|[1-9]\d)|[2-9]\d*\.\d+).*api errors 0',
                 event=True, settle_s=1, min=1, windows=['presented-'+label]),
            dict(name='RR GPU timing '+label, select=r'\[ngx\] gpu RR:',
                 match=r'guides [\d.]+ ms \+ evaluate [\d.]+ ms', min=1, windows=['timing-'+label])])
        if change.get('rr_post', 1):
            every.append(dict(name='post GPU timing '+label, select=r'\[rrpost\] gpu:',
                              match=r'render-res chain [\d.]+ ms/frame, output-res grade [\d.]+ ms/frame',
                              min=1, windows=['timing-'+label]))
        if change.get('rr_hair_resolve'):
            every.append(dict(name='resolve active '+label, select=r'\[rrpost\] hair resolve:',
                match=r'current class-5 occupancy, \d+x\d+, mixed 4x4 cells',
                event=True, settle_s=1, min=1, windows=['timing-'+label]))
    step('restore', 5, dict(BASE, fg_snap=0, pan_capture=0, pan_edge=0, pan_prepare=0, qa_exposure_hold=0))
    startup = dict(x.split('=',1) for x in old['set'].split(','))
    startup.update(BASE, dump_images=1, dump_png_width=1024, dump_raw=0, dump_shaders=0)
    old.update(name='ef3-s8-post-decision', save='Water Queens House', live_batch='rr-post-decision', timeout=600,
               set=','.join(f'{k}={v}' for k,v in startup.items()),
               args=['-RealPresent','-Move','-MovePlan','captures/live_batches/rr_post_decision/plans/ef3.json'],
               expect_every=every,
               expect_runner=[r'\[window-mode\].*visible_unfocused', r'^Run ended: done',
                              rf'\[motion-plan\] complete phases={len(plan)}\b']+traces,
               estimated_phase_seconds=sum(p['seconds'] for p in plan),
               purpose='Offline prepared only. Static S8 gameplay close companion; no dialogue or camera input. Proposed ROIs require framing verification before future authorization. rr_post 1/0/1/0; then resolve 0/1/0/1 at rr_post=1. See predicted.json. Capture, float traces and clean cost windows are separate.')
    old['analysis'] = dict(roi_extent=[1920, 800], subtitle_top=800, full_resolution_presented=True,
                           rois=dict(hair_box=[800, 80, 1100, 380],
                                     rigid_control=[1150, 300, 1450, 550]))
    old['framing_status'] = 'PROPOSED_STATIC_S8_REQUIRES_VERIFICATION'
    old['patterns'] += [r'\[trace\]', r'\[dump\]']
    write(HERE/'plans/ef3.json', plan)
    write(HERE/'jobs/ef3-s8-post-decision.json', old)


if __name__ == '__main__':
    make()
