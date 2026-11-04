"""Prepare scene round-1 A/B receipts offline; never stage or launch."""
from copy import deepcopy
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot, pipeline_readiness
from tools.live_batches.combine import assignments
from tools.live_batches.opt_profile.make_jobs import phase
from tools.live_batches.release_qual import make_final

HERE = Path(__file__).resolve().parent
STATES = [('off-a', 0), ('off-b', 0), ('on', 1), ('off-c', 0)]
TIMERS = ['scene build GPU', 'rt passes: GPU cost', 'gi: trace pass',
          'ls: local-light shadow pass GPU median', 'rf: reflection pass GPU median']


def generate(results=None):
    lint_snapshot.load()
    # New transitions have no historical median. Preserve samples=0 rather
    # than borrowing an unrelated frozen transition's receipt.
    flow.readiness_budget.configure([])
    if results:
        corpus=flow.corpus(results, 'NUL', newest=4)
        flow.fps_windows.configure(corpus['entries'])
        flow.configure_trace_exports(corpus['entries'])
        flow.readiness_budget.configure(corpus['entries'])
    source, profile_plan, _ = flow.load_job(ROOT/'tools/live_batches/opt_profile/jobs/opt-profile-inn.json')
    ultra, high = [s for p, s in zip(profile_plan, flow.phase_states(source, profile_plan))
                   if p['label'] in ('ultra--static-a', 'high--static-a')]
    for area, key, preset, serial in [('scene_holds', 'rt_scene_compact_holds', 'high', 99520)]:
        name = 'opt-scene-compact-holds-inn'
        startup = dict(ultra, rt_scene_compact_holds='0',
                       detailed_logging='1', dev='1', cpu_timing='0', pan_prepare='1', pan_edge='1',
                       pan_post='1', pan_capture='0', dump_images='0', dump_raw='0',
                       dump_preview_f32='0', dump_shaders='0', qa_exposure_hold='0',
                       rt_hair_pool_trim='1', rt_stats_period_frames='60', rt_timing_frames='60')
        target = dict(startup if preset == 'ultra' else high, detailed_logging='1', cpu_timing='0',
                      dev='1', rt_scene_compact_holds='0',
                      rt_hair_pool_trim='1' if preset == 'ultra' else '0',
                      rt_stats_period_frames='60', rt_timing_frames='60',
                      pan_prepare='1', pan_edge='1', pan_post='1', pan_capture='0',
                      dump_images='0', dump_raw='0', dump_preview_f32='0', dump_shaders='0',
                      qa_exposure_hold='1')
        plan = [phase('warmup', 29, set=assignments(startup),
                      wait_log=[r'\[shaders\] cache: prefetched ', r'\[auto\] scene ready'], wait_log_timeout=60)]
        every = []
        # Both specializations/presets are warmed before the first measured gate.
        for value in (1, 0):
            plan.append(phase('warm-key-'+str(value), 5, set=assignments(dict(target, **{key:str(value)}))))
        if preset == 'high':
            smoke=dict(startup, **{key:'1'})
            plan.append(phase('warm-smoke-ultra', 5, set=assignments(smoke)))
            plan.append(phase('smoke-ultra--static-a', 5, set=assignments(smoke),
                              frames=120, settle_frames=0, stats_reset=True,
                              wait_stats=TIMERS, wait_log_timeout=15))
            for timer in TIMERS:
                every.append(dict(name='Ultra smoke '+timer, select=timer, match=timer,
                                  windows=['smoke-ultra--static-a'], min=1,
                                  period_frames=60, sample_frames=60))
            plan.append(phase('warm-return-high', 5, set=assignments(target)))
        for i, (label, value) in enumerate(STATES):
            state = dict(target, **{key:str(value)}, pan_capture='0')
            plan.append(phase('ready-'+label, 3, set=assignments(state)))
            cost_label = label+'--static-a'
            plan.append(phase(cost_label, 15, set=assignments(state), frames=180,
                              settle_frames=0, stats_reset=True, wait_stats=TIMERS,
                              wait_log_timeout=15))
            for timer in TIMERS:
                every.append(dict(name=label+' '+timer, select=timer, match=timer,
                                  windows=[cost_label], min=2, period_frames=60, sample_frames=60))
            every.append(dict(name=label+' hold work', select=r'\[bvh\] last 60 builds',
                                  match=(r'hold pack [1-9]\d* jobs.*unavailable 0 builds' if value else
                                         r'hold pack \d+ jobs.*unavailable 0 builds'),
                              windows=[cost_label], min=2, period_frames=60, sample_frames=60))
            plan.append(phase('energy-'+label, 8, set=assignments(state), trace_at=[1]))
            plan.append(dict(phase('qa-'+label, 3, set=assignments(dict(state, pan_capture=str(serial+i)))),
                             serial=serial+i, capture_at=.1))
            every += [dict(name=label+' float pre-post export', select=r'\[trace\] frame \d+ written:',
                           match=r'\[trace\] frame \d+ written:', event=True, min=1, windows=['energy-'+label]),
                      dict(name=label+' rendered QA transport', select=rf'\[pan\] burst {serial+i} complete',
                           match=rf'\[pan\] burst {serial+i} complete: 8 frames, \d+ samples, transport ok',
                           event=True, min=1, windows=['qa-'+label])]
        plan.append(phase('restore', 2, set=assignments(dict(target, **{key:'0'},
                          qa_exposure_hold='0', pan_capture='0', pan_prepare='0', pan_edge='0', pan_post='0'))))
        plan_path = HERE/area/'plans'/f'{name}.json'
        job_path = HERE/area/'jobs'/f'{name}.json'
        job = deepcopy(source)
        job.update(name=name, live_batch='opt-r1-scene', set=assignments(startup),
                   args=['-RealPresent', '-Move', '-MovePlan', plan_path.relative_to(ROOT).as_posix()],
                   expect_every=every, hold=90, timeout=300, max_minutes=5, stop_on_dump=False,
                   live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=1),
                   candidate=dict(key=key, area=area, preset=preset, states=STATES, serial_base=serial),
                   purpose='Offline prepared only. Same-launch OFF repeats / ON; exact offline proof must pass before live authorization.')
        job['patterns'] += [r'\[cascade\]', r'\[bvh\]', r'\[pan\]', r'\[trace\]', r'\[dump\]']
        job['expect_every'].append(dict(name='current-frame scene', select='rt passes: GPU cost',
            match=r'TLAS this frame [1-9]\d*, previous frame 0, none 0',
            windows=[label+'--static-a' for label, _ in STATES], min=2, period_frames=60, sample_frames=60))
        job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                               rf'\[motion-plan\] complete phases={len(plan)}\b']
        job['post'] = (f'python tools/live_batches/opt_r1/scene_analyze.py --job {job_path.relative_to(ROOT).as_posix()} '
                       '--log "{log}" --runner "{runner}" --out "{log}.scene.json"')
        job['post_gate'] = True
        for p, wanted in zip(plan, pipeline_readiness.requirements(job, plan)):
            for changed in wanted:
                if changed not in pipeline_readiness.TABLE:
                    value = flow.settings(p['set'])[changed]
                    p.setdefault('wait_log', []).append(r'\[cfg\] keys:.*\b'+re.escape(changed)+'='+re.escape(value)+r'(?:\s|$)')
        make_final.retime_final(job, plan)
        # Keep capture service above its measured ceil boundary after retiming.
        for p in plan:
            if p['label'].startswith('energy-'):
                p.update(seconds=max(p['seconds'], 5), motion_end=max(p['seconds'], 5))
            if p.get('wait_log'):
                p['wait_log_timeout']=60 if p['label']=='warmup' else 10
        job.update(timeout=300, max_minutes=5)
        job['estimated_phase_seconds'] = round(sum(p['seconds'] for p in plan), 3)
        flow.repair_known_forbids(job, plan)
        flow.write(job_path, job)
        flow.write(plan_path, plan)
        flow.write(HERE/area/'predicted.json', dict(job=name, status='OFFLINE_PROOF_PENDING', key=key,
            expected_saving_ms=[.05, .30], measured_saving_ms=None, scene=source['save'], preset=preset,
            device_startup='Ultra stride 88; High target only after warmup' if preset == 'high' else 'Ultra',
            limits='No game launch or staging authorized. Reject below 0.05 ms. Three OFF repeats derive energy noise; missing cost, export, QA or readiness invalidates.',
            offline_exactness='Required separately: identical converted geometry or hold-pack trace attributes.',
            energy='Float pre-post scene RGB and luminance; ON versus adjacent OFF bracket, band derived from all OFF repeats.',
            flicker='Existing QA temporal test, ON versus every OFF; geometry motion excluded from both windows.',
            disk='No raw images; K8 edge bursts plus dumps.json means. Retain summaries; batch target below 2 GB.'))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results',help='Optional recent VALID results.jsonl for actual FPS/readiness sizing')
    generate(parser.parse_args().results)
