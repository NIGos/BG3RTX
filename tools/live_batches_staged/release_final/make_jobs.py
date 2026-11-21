"""release_final: the final release smoke for 0.9.6. STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.release_final.make_jobs --profile-exe <compiled release_qual/profile.cpp>

Reuses the release_qual generators (make_jobs.py, make_final.py) unchanged, with their output redirected here, so the
jobs are the 2026-11-04 final qualification regenerated from today's exported release profile (common/settings.h,
bg3rtx.ini, data/release.ini): Low-latency scene On in High and Ultra, the four VRAM policies on, RTX Hair Off,
stock visible hair. Two launches: final-s8-ultra-high (Ultra then High at Water Queens House) and
final-stock-hair-dialogue1. On top of the 2026-11-04 gates, the S8 windows gate what changed since: the scene
handout hands out every first RT query with 0 fallbacks, and no opaque draw is dropped with the VRAM policies.
predicted.json is written before any job. Nothing here touches captures/queue.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot
from tools.live_batches.combine import settings
from tools.live_batches.release_qual import make_jobs as release, make_final as final

BATCH = 'tools/live_batches_staged/release_final'
QUEUE = ['final-s8-ultra-high', 'final-stock-hair-dialogue1']
COST = release.COST
HANDOUT = r'\[rt\] scene handout: \d+ frames, reserved'
# p95 capture-frame stall of the VALID corpus on 2026-11-15 (hx1-s8-hair-stable 190905, 194658, 001645: 18 traces,
# 5.17137 s). The snapshot's older 2.65 s sized the energy window to 3.8 s, which today's preflight refuses.
TRACE_STALL_S = 5.18
PERIOD = dict(period_frames=120, sample_frames=120)   # rt_scene_wait_log_frames=120: the handout line follows the scene wait line
# What the release profile must export today; main() refuses to generate from any other profile.
PROFILE = dict(dev='0', rt_scene_handout='2', rt_scene_handout_layout='1', rt_scene_handout_hit_wait='1',
               rt_capture_peak_reserve='1', rt_capture_demand_size='1', rt_capture_split_mb='1', rt_mesh_pool_trim='1',
               rt_hair_groom_primary='0', rt_hair_groom='0')


def predicted(exe):
    for preset in ('ultra', 'high'):
        state = settings(subprocess.check_output([exe, preset], cwd=ROOT, text=True).replace('\n', ','))
        wrong = {k: state.get(k) for k, v in PROFILE.items() if state.get(k) != v}
        assert not wrong, (preset, wrong, 'the exported release profile is not the 0.9.6 profile')
    flow.write(HERE / 'predicted.json', dict(
        _rule='Prepared BEFORE the run (AGENTS.md). STAGED, not queued, not authorized: needs a user go for this batch.',
        status='READY_FOR_LIVE', queue_only=QUEUE, launches=2, scenes=2,
        profile='data/release.ini + launcher preset through common/settings.h (release_qual/profile.cpp), dev=0; '
                'qualification-only overrides are log periods, detailed_logging and the one float export per static phase',
        input_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
                      for p in ('common/settings.h', 'data/release.ini', 'bg3rtx.ini')},
        release_profile_checked=PROFILE,
        observable='The release profile of 0.9.6 (Ultra and High at Water Queens House, Ultra in Dialogue1) runs every '
                   'measured window without a fault after the changes of 2026-11-14/15: scene handout default, VRAM '
                   'policies, hair fixes with RTX Hair Off.',
        pass_rule='Every gate of the 2026-11-04 final qualification (water traced and composed, finite, driver-budget '
                  'VRAM, 0 RT capture drops, 0 alpha region overflow, cascade buffer full 0, scene wait 0 timeouts, '
                  'current TLAS, RR/post/GI cost lines, OMM same-ray check on High, native hair and scalp records in '
                  'the dialogue, one master OFF/ON cycle) plus, in the five S8 cost windows of each preset: every '
                  'first RT query handed out, handout fallbacks 0, pairing mismatches 0, hit-material query '
                  'failures and fallbacks 0. No crash, device loss or strand producer. These are counts of real '
                  'faults: 0, no tolerance. OMM downgrades are INFO with their count (the slot traces the reference '
                  'scene: a correct image).',
        predicted_today={
            'scene wait S8 (High and Ultra, handout On)': 'mean about 0.3 ms (0.28 in hq-s8-modes 205520; 3.5 without); '
                '0 timeouts; reported, the gate is the fault counts (docs/SCENE_HANDOUT_DESIGN.md 8.7)',
            'handout': '0 fallbacks, 0 pairing mismatches in every cost window (8.5 to 8.8)',
            'VRAM policies': 'RT capture first buffer 1024 MB, cascade 3 x 427 MB, 0 dropped draws, guard quiet '
                '(hx1 001645, docs/HANDOFF_SESSION.md); VRAM used about 0.8 GB below the 2026-11-04 receipts at S8; reported',
            'hair': 'stock cards: no [hair primary] or [hair groom] activity line in any window',
            'GPU ms': 'RT, GI, RR and post within the rel1/rel2/rel3 observed ranges; GI trace may read 0.2-0.4 ms '
                'lower with the handout (seen in one rerun, EXPLAIN): report it, do not count it as a gain',
            'energy': 'one float pre-post export per profile, reported; no same-profile baseline exists, so no energy pass or fail',
        },
        not_judged='frame time and presented captures (the window may be occluded); GPU ms deltas against the '
                   '2026-11-04 references are descriptive (release_qual/predicted_final.json); card or strand counts.',
        if_it_fails='A handout fallback or mismatch in a steady window: report the reason counts of the line; the '
                    'release decision is Low-latency scene Off in the presets (one choice index per preset). A dropped '
                    'opaque draw: report the capture line; the decision is rt_capture_demand_size or rt_capture_peak_reserve off.',
        human_review='Dialogue PNG: stock cards visible, face and neck shadows. S8 PNGs: cutout silhouettes, water edges.'))


def handout_gates(job):
    """Per-window gates for what changed since 2026-11-04, on the combined S8 job (its windows are prefixed)."""
    windows = [w for g in job['expect_every'] for w in g.get('windows', []) if w.rsplit('--', 1)[-1] in COST]
    windows = list(dict.fromkeys(windows))
    assert len(windows) == 2 * len(COST), windows
    job['patterns'] = list(dict.fromkeys(job['patterns'] + [r'\[rt\] scene', r'\[vram\]', r'\[mem\]']))
    job['expect'] = list(dict.fromkeys(job['expect'] + [HANDOUT]))
    job['expect_every'] += [
        release.gate('handout: every first RT query handed out', r'\[rt\] scene wait: \d+ frames',
                     r'handed out [1-9]\d*', windows),
        release.gate('handout: 0 fallbacks and 0 pairing mismatches', HANDOUT,
                     r'in order [1-9]\d*, fallbacks 0 \(.*\), pairing mismatches 0,', windows, **PERIOD),
        # An OMM downgrade traces the reference scene for that slot (a correct image, AGENTS.md sensible gates): INFO,
        # counted by the analyzer (release_qual/analyze_final.py omm_downgrades). High has about 1 per 60 frames
        # (final-s8-ultra-high 052606: rt_hit_material=0, no pass waits for the commit). Hit-material failures still fail.
        release.gate('handout: no hit-material query failure or fallback', HANDOUT,
                     r'hit material queries at the commit \d+, failed 0 \(fallbacks: query 0, camera 0\)', windows, **PERIOD),
        release.gate('VRAM policies: no opaque draw dropped', r'\[geo\] RT capture, last \d+ frames:',
                     r'opaque draws dropped \(buffer full\) 0\.00,', windows)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile-exe', required=True)
    ap.add_argument('--extend-snapshot', action='store_true', help='append the unseen readiness queries of this batch to lint_corpus_snapshot.json')
    a = ap.parse_args()
    predicted(a.profile_exe)   # before any job
    # Redirect the release_qual generators; their own tree stays byte-identical (git status after a run).
    release.HERE = final.HERE = HERE
    final.FINAL = HERE / 'final'
    final.predictions = lambda: None   # release_qual's reference table; this batch has predicted.json
    # Windows are sized with the live VALID corpus, the model the runner preflight lints with (2026-11-21: the
    # snapshot's p10 fps, 10.0 in Dialogue1 against 7.1 live, sized energy-static 0.06 s short and the preflight
    # refused it). A readiness query neither corpus has is estimated without samples, exactly as
    # lint_snapshot.extend_readiness records it; --extend-snapshot appends those queries (append only) for --frozen.
    tighten, load, finish = flow.tighten_files, lint_snapshot.load, release.finish
    def finish_with_stall(job, *args):
        meta = job.setdefault('live_flow', {})
        meta['trace_stall_s'] = max(float(meta.get('trace_stall_s', 0)), TRACE_STALL_S)
        finish(job, *args)
    release.finish = finish_with_stall
    def unfrozen(*args, **kw):
        data = load(*args, **kw)
        flow.readiness_budget.FROZEN = False
        return data
    lint_snapshot.load = unfrozen
    flow.tighten_files = lambda directory, **kw: tighten(directory)
    try:
        _run_final(a.profile_exe)
    finally:
        flow.tighten_files, lint_snapshot.load, release.finish = tighten, load, finish
    # The generators name release_qual paths for the runner: point them at this batch.
    moves = [('tools/live_batches/release_qual/final/', BATCH + '/final/'),
             ('captures/live_batches/release_qual/plans/', BATCH + '/plans/'),
             ('tools/live_batches/release_qual/jobs/', BATCH + '/jobs/')]
    for path in sorted(HERE.rglob('*.json')):
        text = path.read_text(encoding='utf-8')
        for old, new in moves:
            text = text.replace(old, new)
        path.write_text(text, encoding='utf-8', newline='\n')
    path = HERE / 'final/jobs/final-s8-ultra-high.json'
    job = flow.read(path)
    handout_gates(job)
    flow.write(path, job)
    schedule = {}
    for name in QUEUE:
        job, plan, _ = flow.load_job(HERE / 'final/jobs' / (name + '.json'))
        assert job['window_mode'] == 'visible_unfocused_fg' and job['timeout'] >= 300 and job['max_minutes'] * 60 >= job['timeout']
        assert job['hold'] >= 90 and job['patterns'] and job['expect'] and job['expect_every'] and job['fail_fast']
        assert job['post'].startswith('py -3 ') and BATCH in job['post'] and BATCH in job['args'][-1]
        budget = flow.duration_budget(job, plan)
        captures = sum(len(p.get('trace_at', [])) for p in plan)
        schedule[name] = dict(save=job['save'], load_allowance_seconds=120, scheduled_seconds=budget['seconds'],
            readiness_seconds=budget['expected_readiness_seconds'],
            expected_minutes=round((120 + budget['seconds'] + budget['expected_readiness_seconds']) / 60, 2),
            hard_timeout_seconds=job['timeout'], captures=captures, disk_estimate_mb=10 + 60 * captures)
    flow.write(HERE / 'schedule.json', schedule)
    if a.extend_snapshot:
        print('snapshot readiness queries appended:', lint_snapshot.extend_readiness(flow.job_files(HERE)))
    print('Generated', ', '.join(QUEUE), '; no queue staging')


def _run_final(exe):
    argv = sys.argv
    sys.argv = [argv[0], '--profile-exe', exe]
    try:
        final.main()
    finally:
        sys.argv = argv


if __name__ == '__main__':
    main()
