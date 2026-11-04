"""Prepare the final release receipts. Never enqueue or launch anything."""
import argparse
from copy import deepcopy
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches.release_qual import make_jobs as release
from tools.live_batches import live_flow as flow
from tools.live_batches.combine import assignments, settings, combine_group
from tools.live_batches.release_qual.analyze_final import costs, energy

HERE = Path(__file__).resolve().parent
FINAL = HERE / 'final'


def predictions():
    evidence = flow.default_results().parents[2]
    references = {}
    for key, stem in [('rel1', 'rel1-smoke-ultra-s8.1.205853'),
                      ('rel2', 'rel2-smoke-high-s8.1.121305'),
                      ('rel3', 'rel3-stock-hair-dialogue1.1.210304')]:
        log = evidence/'captures/queue/logs'/f'{stem}.log'
        runner = log.with_name(stem+'.runner.txt')
        references[key] = dict(log='captures/queue/logs/'+log.name,
            runner='captures/queue/logs/'+runner.name, gpu_costs=costs(log, runner),
            energy=None, energy_status='No float capture in this smoke; signed historical delta unavailable.')
        if key == 'rel3':
            capture = evidence/'.claude/worktrees/test-main/captures/20261104-210456'
            references[key].update(energy=energy(capture), energy_status='Single static float baseline; no baseline noise repeats.')
    jobs = {}
    for path in flow.job_files(HERE):
        job, plan, _ = flow.load_job(path)
        budget = flow.duration_budget(job, plan)
        record = dict(status='READY_FOR_LIVE', save=job['save'], max_minutes=5,
            hard_timeout_seconds=300, load_allowance_seconds=120,
            expected_minutes=round((120+budget['seconds']+budget['expected_readiness_seconds'])/60, 2),
            scheduled_seconds=budget['seconds'], readiness_seconds=budget['expected_readiness_seconds'],
            captures=sum(len(p.get('trace_at', [])) for p in plan),
            disk_estimate_mb=10+60*sum(len(p.get('trace_at', [])) for p in plan),
            expect=job['expect'], expect_every=job['expect_every'], forbid=job['forbid'])
        jobs[job['name']] = record
        if path.parent == HERE/'jobs':
            flow.write(HERE/job['name']/'predicted.json', record)
    flow.write(HERE/'predicted_final.json', dict(status='READY_FOR_LIVE', base='ab66c855',
        baseline_release='ce1448bf (rel1/rel3); High OMM receipt rel2 12:13',
        queue_only=['final-s8-ultra-high', 'final-stock-hair-dialogue1'],
        launches=2, scenes=2, shader_request_delta=0, device_lost=0, crashes=0,
        references=references, jobs=jobs,
        cost_prediction='Within the same-scene rel1/rel3 observed GPU range unless a shader fix explains a signed change. '
            'Use per-pass ms, not frame time or occluded presented captures. Report every signed delta; '
            'single-sample ranges are not a statistical noise estimate. No build or compile overlaps.',
        cost_exceptions=dict(rr_post='Parallel log-sum arithmetic can increase post-chain GPU ms; report the signed measured cost, no fabricated ms allowance.',
            bounds='Mip/UV/finite guards add small ALU; expected within observed noise. Corrected visibility can change ray traversal cost.',
            lifetime='Layer lifetime/FG/init changes predict no sustained per-pass GPU increase after warmup.'),
        energy_prediction=dict(expected='within noise', measurement='Float full-image scene C before post: rrpost slot 2 dumps.json mean RGB, luminance 0.2126 R + 0.7152 G + 0.0722 B.',
            rr_post='Log-domain geometric mean prevents intermediate product overflow/underflow and preserves ordinary rounding; no systematic pre-post C shift expected.',
            alpha_mip='Only clamped out-of-range mip addresses change; corrected cutout visibility can add or remove occlusion. No global sign or percent can be derived.',
            uv_bounds='Rejecting UV == 1 removes false edge hits; local sign depends on replaced sample. No systematic global shift expected.',
            reprojection='Invalid history falls back to fresh/native. Previously black unavailable bounce regions may brighten; valid-history regions remain within noise. Uniform darkening is not expected.',
            half_float='Finite positive tails above 65504 are capped (local downward delta); negative tails clamp to zero (upward). Avoided Inf and downstream rejection can change the observed sign. Ordinary radiance remains within noise.',
            acceptance='Report signed absolute/percent deltas where a same-scene baseline exists. Never call lower energy an improvement. '
                'A material shift requires physically expected attribution or PT evidence, not an invented tolerance. '
                'S8 historical energy is unavailable in rel1/rel2; retain current floats and mark this comparison unavailable, not PASS.'),
        visual='Inspect High cutout silhouettes and holes, Ultra water/edges, stock hair cards and face/neck shadows. No cross-run image A/B.',
        pending='Live paused. Coordinator must commit predictions and obtain explicit steering before staging; no receipt grants authority.'))


def energy_phase():
    return dict(label='energy-static', kind='static', speed='static', seconds=8,
                repeat_ms=0, scan=16, capture_at=0, serial=0, motion_start=8,
                motion_end=8, pixels_per_second=0, direction=0, trace_at=[1],
                set='qa_exposure_hold=1,rt_stats=0,rt_primary_check=0,rt_debug=0')


def qualify(job, plan):
    # Capture only in a separate static phase. Full-image float means are always
    # written to dumps.json even with raw/preview-f32 disabled.
    for p in plan:
        p.pop('trace_at', None)
    insert = next(i for i, p in enumerate(plan) if p['label'] == 'static-a')
    plan.insert(insert, energy_phase())
    state = settings(job['set'])
    state.update(headless='0', rt_timing_frames='120', rt_scene_wait_log_frames='120',
                 fg_state_every='30', dump_images='1', dump_png_width='1280', dump_raw='0', dump_preview_f32='0')
    job.update(set=assignments(state), timeout=300, max_minutes=5, live_batch='final-qual')
    job['patterns'] = list(dict.fromkeys(job['patterns'] + [r'\[trace\]', r'\[dump\]',
        r'\[ngx\] gpu RR:', r'\[rrpost\] gpu', 'gi: trace pass', 'gpu:']))
    job['expect'] = [rx for rx in job['expect'] if 'trace' not in rx]
    job['expect_every'].append(release.gate('float pre-post energy exported',
        r'\[trace\] frame \d+ written:', r'\[trace\] frame \d+ written:', ['energy-static'], event=True))
    labels = [p['label'] for p in plan]
    cost = [x for x in release.COST + ['cost-hair'] if x in labels]
    for sel, match in [('rt passes: GPU cost', r'TLAS this frame [1-9]\d*, previous frame 0, none 0'),
                       ('gi: GPU cost', r'gi: GPU cost [0-9.]+ ms/frame'),
                       (r'\[rrpost\] gpu:', r'render-res chain [\d.]+ ms/frame, output-res grade [\d.]+ ms/frame'),
                       (r'\[ngx\] gpu RR:', r'gpu RR: guides [\d.]+ ms \+ evaluate [\d.]+ ms')]:
        job['expect_every'].append(release.gate('release cost: '+sel, sel, match, cost))
    job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
        rf'\[motion-plan\] complete phases={len(plan)}\b',
        rf'\[motion-trace\] phase={insert} label=energy-static repeat=0']
    return job, plan


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', required=True)
    a = ap.parse_args()
    release.smoke(a.profile_exe)
    release.dialogue(a.profile_exe)
    ultra, up, _ = flow.load_job(HERE / 'jobs/rel1-smoke-ultra-s8.json')
    hair, hp, _ = flow.load_job(HERE / 'jobs/rel3-stock-hair-dialogue1.json')
    high, highp = deepcopy(ultra), deepcopy(up)
    high['name'] = 'rel2-smoke-high-s8'
    high['args'][-1] = 'captures/live_batches/release_qual/plans/rel2-smoke-high-s8.json'
    high['set'] = assignments(release.profile(a.profile_exe, 'high'))
    # Reuse the original same-ray OMM gate and diagnostic. Its output is read
    # because the mip-clamp fix touches the High alpha/OMM path.
    source, sp, _ = flow.load_job(ROOT / 'tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json')
    highp = [p for p in highp if not p['label'].startswith('ab-')]
    for p in sp:
        if p['label'] in ('warmup-diagnostic-settings', 'diag-warm-omm', 'diag-omm'):
            p = deepcopy(p)
            p['set'] = p['set'].replace('rt_scene_wait_ms=100', 'rt_scene_wait_ms=12').replace('rt_scene_wait_log_frames=60', 'rt_scene_wait_log_frames=120')
            highp.append(p)
    highp.append(dict(energy_phase(), label='restore-diagnostics', trace_at=[], seconds=2,
                      set='qa_exposure_hold=0,rt_stats=0,rt_primary_check=0,rt_debug=0,rt_scene_wait_ms=12,rt_scene_wait_log_frames=120'))
    high['expect'] = [rx for rx in high['expect'] if '[ab' not in rx.replace('\\', '') and 'toggle latency' not in rx]
    high['expect'] += [rx for rx in source['expect'] if 'omm' in rx]
    high['forbid'] += [rx for rx in source['forbid'] if 'omm' in rx]
    high['patterns'] += [r'\[omm', 'omm check', r'\[vt alpha\]']
    high['expect_every'] += [deepcopy(g) for g in source['expect_every'] if g['name'].startswith('OMM:')]
    for job, plan in ((ultra, up), (high, highp), (hair, hp)):
        qualify(job, plan)
        # Preserve every historical receipt's gates; High also requires real
        # mask residency, not merely a preset value or startup capability line.
        if job is high:
            job['expect'].append(r'\[vt alpha\].*')
        flow.write(HERE / 'jobs' / (job['name']+'.json'), job)
        flow.write(HERE / 'plans' / (job['name']+'.json'), plan)
    flow.tighten_files(HERE, frozen=True)
    # Frame count ends these motions. Retired seconds-based travel endpoints
    # must not inflate the watchdog estimate or request a longer camera pan.
    for path in (HERE / 'jobs').glob('*.json'):
        j, phases, pp = flow.load_job(path)
        for p, state in zip(phases, flow.phase_states(j, phases)):
            if p.get('frames'):
                p['seconds'] = round(p['frames']/flow.fps_windows.expected(j, state)['fps'] + .1, 3)
                p['motion_end'] = p['seconds']
                p['motion_start'] = min(p.get('motion_start', 0), p['seconds'])
        flow.retime(j, phases)
        # Hard launch watchdog includes loading. A missed readiness signal
        # invalidates this receipt instead of extending a final smoke.
        for i, p in enumerate(phases):
            if p.get('wait_log'):
                p['wait_log_timeout'] = 60 if i == 0 else 10
        j['timeout'] = 300
        flow.write(path, j)
        flow.write(pp, phases)
    sources = []
    for name in (ultra['name'], high['name']):
        path = HERE / 'jobs' / (name+'.json')
        job, plan, _ = flow.load_job(path)
        sources.append((path, job, plan))
    name = 'final-s8-ultra-high'
    job, plan, manifest = combine_group(sources, f'tools/live_batches/release_qual/final/plans/{name}.json')
    job.update(name=name, live_batch='final-qual', timeout=300, max_minutes=5)
    for g in job['expect_every']:
        if 'no strand producer' in g['name']:
            g.pop('windows', None)  # full-log absence; warmup is not measured
    reset = next(p for p in plan if p['label'].endswith('settings-reset'))
    reset['wait_log'] = [r'\[cfg\] keys:.*fps_cap=', r'\[cfg\] keys:.*rt_native_hair=',
                         r'\[cfg\] keys:.*rt_native_scalp=']
    flow.retime(job, plan)
    for i, p in enumerate(plan):
        if p.get('wait_log'):
            p['wait_log_timeout'] = 60 if i == 0 else 10
    job['timeout'] = 300
    job['post'] = ('py -3 tools/live_batches/release_qual/analyze_final.py '
        '--job tools/live_batches/release_qual/final/jobs/final-s8-ultra-high.json '
        '--log "{log}" --runner "{runner}" --out "{log}.final.json"')
    job['post_gate'] = True
    flow.write(FINAL / 'jobs' / (name+'.json'), job)
    flow.write(FINAL / 'plans' / (name+'.json'), plan)
    manifest['plan'] = plan
    flow.write(FINAL / (name+'.manifest.json'), manifest)
    hair, hp, _ = flow.load_job(HERE / 'jobs/rel3-stock-hair-dialogue1.json')
    hair['args'][-1] = 'tools/live_batches/release_qual/final/plans/final-stock-hair-dialogue1.json'
    hair['name'] = 'final-stock-hair-dialogue1'
    hair['post'] = ('py -3 tools/live_batches/release_qual/analyze_final.py '
        '--job tools/live_batches/release_qual/final/jobs/final-stock-hair-dialogue1.json '
        '--log "{log}" --runner "{runner}" --out "{log}.final.json"')
    hair['post_gate'] = True
    flow.write(FINAL / 'jobs/final-stock-hair-dialogue1.json', hair)
    flow.write(FINAL / 'plans/final-stock-hair-dialogue1.json', hp)
    predictions()


if __name__ == '__main__':
    main()
