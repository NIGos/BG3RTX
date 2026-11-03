"""Prepare ef2 outside all watched inboxes. Never enqueue or launch."""
import json
from pathlib import Path
from tools.live_batches.edge_flicker.make_job import BASE as OLD_BASE, write
HERE = Path(__file__).resolve().parent
BASE = dict(OLD_BASE, rr_hair_resolve=0)
WINDOWS = [('base-a', {}), ('post-late', {'rr_post': 0}),
           ('hair-resolve', {'rr_hair_resolve': 1}), ('base-b', {})]


def make():
    old = json.loads((HERE.parent/'edge_flicker/jobs/ef1-dialogue3-attribution.json').read_text())
    plan, every, traces = [], [], []
    def step(label, seconds, keys, **extra):
        plan.append(dict(label=label, kind='static', speed='static', seconds=seconds,
                         repeat_ms=0, scan=16, capture_at=0, serial=0,
                         motion_start=seconds, motion_end=seconds, pixels_per_second=0,
                         direction=0, registration_policy='per-window-exclude-subtitles',
                         set=','.join(f'{k}={v}' for k,v in keys.items()), **extra))
    for label in ('warmup', 'warmup-camera'):
        step(label, 20, dict(BASE, fg_snap=0, pan_capture=0, qa_exposure_hold=0))
    for i, (label, change) in enumerate(WINDOWS):
        state = dict(BASE, **change, fg_snap=0, pan_capture=0, qa_exposure_hold=1)
        step('settle-'+label, 5, state)
        step('rendered-'+label, 5, dict(state, pan_capture=98400+i))
        plan[-1].update(capture_at=0.1, serial=98400+i)
        step('presented-'+label, 5, dict(state, fg_snap=16))
        step('energy-'+label, 5, state, trace_at=[1])
        phase = len(plan)-1
        traces.append(rf'\[motion-trace\] phase={phase} label=energy-{label} repeat=0')
        step('timing-'+label, 10, state)
        every.extend([
            dict(name='K8 '+label, select=rf'\[pan\] burst {98400+i} complete',
                 match=rf'\[pan\] burst {98400+i} complete: 8 frames, 20 samples, transport ok',
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
        if label != 'post-late':
            every.append(dict(name='post GPU timing '+label, select=r'\[rrpost\] gpu:',
                              match=r'render-res chain [\d.]+ ms/frame, output-res grade [\d.]+ ms/frame',
                              min=1, windows=['timing-'+label]))
        if change.get('rr_hair_resolve'):
            every.append(dict(name='hair resolve actually active '+label,
                              select=r'\[rrpost\] hair resolve:',
                              match=r'\[rrpost\] hair resolve: current class-5 occupancy, \d+x\d+, mixed 4x4 cells',
                              event=True, settle_s=1, min=1,
                              windows=['timing-'+label]))
    step('restore', 5, dict(BASE, fg_snap=0, pan_capture=0, pan_edge=0, pan_prepare=0, qa_exposure_hold=0))
    startup = dict(x.split('=',1) for x in old['set'].split(','))
    startup.update(BASE, dump_images=1, dump_png_width=1024, dump_raw=0, dump_shaders=0)
    old.update(name='ef2-dialogue3-hair-guide', live_batch='rr-hair-guide', timeout=480,
               set=','.join(f'{k}={v}' for k,v in startup.items()),
               args=['-RealPresent','-Move','-MovePlan','captures/live_batches/rr_hair_guide/plans/ef2.json'],
               expect_every=every,
               expect_runner=[r'\[window-mode\].*visible_unfocused', r'^Run ended: done',
                              rf'\[motion-plan\] complete phases={len(plan)}\b']+traces,
               estimated_phase_seconds=sum(p['seconds'] for p in plan),
               purpose='Offline prepared only. One still-camera Dialogue3 run. Coverage resolve hypothesis; no supported bare-opacity RR guide. See predicted.json. Capture, energy and timing windows are separate.')
    old['patterns'] += [r'\[trace\]', r'\[dump\]']
    write(HERE/'plans/ef2.json', plan)
    write(HERE/'jobs/ef2-dialogue3-hair-guide.json', old)


if __name__ == '__main__':
    make()
