"""One ts1 experiment, two startup states. Offline only; reuse release profile and live_flow."""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.combine import assignments, settings

HERE = Path(__file__).resolve().parent


def phase(label, seconds, **state):
    return dict(label=label, kind='static', speed='static', seconds=seconds,
                repeat_ms=0, scan=16, capture_at=0, serial=0,
                motion_start=seconds, motion_end=seconds, pixels_per_second=0,
                direction=0, set=assignments(state))


def gate(name, select, match, windows, **extra):
    return dict(name=name, select=select, match=match, windows=windows, min=1, **extra)


def main():
    # Export with the existing release_qual/profile.cpp, not a copied preset.
    profile = settings((HERE / 'profile.txt').read_text(encoding='utf-8-sig').replace('\n', ','))
    predicted = dict(experiment='ts1-native-tileset', status='READY_FOR_LIVE_TWO_LAUNCHES',
        save='Test Dialogue3', retained_draws=132, supported_material_draws=48,
        supported_depth_draws=56,
        startup_constraint='layer/util.cpp retains startup_tilesets on every reload. No live gate isolates this choice. Two launches required; no fake 0/1/0/1.',
        pass_rule='Cost intervals without traces or diagnostic stats: zero scene-wait timeouts. Each measured interval: capture buffer full and alpha region full zero. ON diagnostic: complete > 0, unknown 0, capture-incomplete 0, qualified family2 > 0 and native primary matches > 0. OFF/ON coverage and RT/GI ms must be present. Float pre-post means finite and nonnegative; report signed energy/coverage deltas, no equal-energy gate for this shading-model change. Missing data INVALID. Visual quality remains user decision.',
        coverage_prediction='ON has positive family2 acquisitions, OFF has no enabled Tileset acquisitions. Report complete record counts, hybrid GI hit/screen/hidden/off-screen shares and overall material qualified/attempted. No invented minimum coverage percentage.',
        pt_reference=None,
        energy_rule='No matched Dialogue3 PT reference found. Report float pre-post scene colour and exposure per launch; independent load/camera/exposure can confound differences. Never a quality PASS from cross-run comparison.',
        budget='Two separate launches, each hard-capped at 4.9 minutes including load; fallback exceeds the original one-launch budget. 120 s load allowance each. Two static traces total, one PNG per state, no raw dumps or bursts; below 2 GB.',
        input_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                      ('common/settings.h', 'data/release.ini', 'bg3rtx.ini')})
    # Write prediction before any runnable draft.
    flow.write(HERE / 'predicted.json', predicted)
    for value, suffix in [('0', 'off'), ('1', 'on')]:
        name = 'ts1-native-tileset-' + suffix
        state = dict(profile)
        state.update(rt_native_tileset=value, rt_native_hair='1', rt_native_scalp='1',
                     rt_hit_material_paks='0', rt_stats='0', dump_images='1',
                     dump_png_width='1280', dump_raw='0', dump_preview_f32='0',
                     rt_timing_frames='120', rt_scene_wait_log_frames='120',
                     qa_exposure_hold='0', headless='0')
        plan = [phase('warmup', 19, rt_stats='0', qa_exposure_hold='0'),
                phase('settle-float', 2, dump_preview_f32='1', qa_exposure_hold='1'),
                phase('diag-' + suffix, 35, rt_stats='1'),
                phase('cost-' + suffix, 25, rt_stats='0'),
                phase('shot-' + suffix, 8),
                phase('restore', 2, qa_exposure_hold='0', dump_preview_f32='0')]
        plan[4]['trace_at'] = [5]
        diag, cost = 'diag-' + suffix, 'cost-' + suffix
        gates = [
            gate('material table capacity', r'\[hit material capacity\] frame \d+ ', r'opaque rows \d+/\d+ overflow 0; deferred rows \d+/\d+ overflow 0; samplers \d+/\d+ overflow 0; cache views \d+/\d+ overflow 0;.* tables \d+/\d+ overflow 0', [diag], event=True, settle_s=1),
            gate('scene waits', r'\[rt\] scene wait: \d+ frames', r"got this frame's scene, 0 timeouts", [cost]),
            gate('RT capture capacity', r'\[geo\] RT capture, last \d+ frames:', r'alpha draws made opaque \(region full\) 0\.00, opaque draws dropped \(buffer full\) 0\.00,', [diag, cost]),
            gate('cascade capacity', r'\[cascade\] casters: cascade \d+, \d+ frames;', r', buffer full 0\.0, added ', [diag, cost], when_present=True),
            gate('RT GPU ms', r'rt passes: GPU cost', r'rt passes: GPU cost [0-9.]+ ms/frame', [cost], period_frames=120),
            gate('GI GPU ms', r'gi: GPU cost', r'gi: GPU cost [0-9.]+ ms/frame', [cost], period_frames=120),
            gate('GI coverage', r'rt stats \(\d+ frames\): hybrid GI', r'hybrid GI [0-9.]+M rays/frame, [0-9.]+% hit;', [diag], period_frames=120),
            gate('material coverage', r'hit material \(\d+ frames\): attempted', r'attempted [1-9]\d* qualified \d+ unsupported \d+', [diag], period_frames=120)]
        if value == '1':
            gates += [gate('Tileset records: invalid means unknown/incomplete', r'\[alpha tileset\] records', r'records complete [1-9]\d* unknown 0 native samplers [1-9]\d* capture-incomplete 0 stride \d+', [diag], period_frames=120),
                      gate('Tileset GPU participation', r'hit material tileset \(\d+ frames\):', r'qualified family2 [1-9]\d*; native primary matches [1-9]\d*', [diag], period_frames=120)]
        job = dict(name=name, save='Test Dialogue3', session=False, repeat=1,
            traces=0, hold=90, timeout=294, max_minutes=4.9,
            live_batch='tileset-qual', window_mode='visible_unfocused_fg',
            set=assignments(state),
            live_flow=dict(expected_fps=30, settings_latency_s=1, trace_stall_s=3,
                           startup_only=['rt_native_tileset']),
            args=['-RealPresent', '-Move', '-MovePlan', f'captures/live_batches/tileset_qual/plans/{name}.json'],
            patterns=[r'\[cfg\]', r'\[alpha tileset\]', 'hit material', 'rt stats',
                      r'\[geo\]', r'\[cascade\]', r'\[rt\] scene wait', 'GPU cost',
                      r'\[trace\]', r'\[watch\]', r'\[shaders\]', r'\[rrpost\] exposure'],
            expect=[r'\[auto\] scene ready', r'\[trace\] frame \d+ written',
                    rf'\[cfg\] keys:.*rt_native_tileset={value}\b'],
            expect_every=gates,
            forbid=[r'\[crash\] exception 0x', 'VK_ERROR_DEVICE_LOST',
                    r'\[hot\] core (rejected|faulted)', 'compile call failed',
                    'FAILED, keeping previous', r'\[alpha tileset\] enable rejected'],
            expect_runner=[r'\[window-mode\].*visible_unfocused',
                           r'\[motion-plan\] complete phases=6\b',
                           rf'\[motion-trace\] phase=4 label=shot-{suffix} repeat=0',
                           '^Run ended: done'],
            fail_fast=True, stop_on_dump=True,
            post=f'python tools/live_batches/tileset_qual/analyze.py --state {suffix} --log "{{log}}" --runner "{{runner}}"', post_gate=True,
            purpose='ts1 startup Tileset ' + suffix + '; release Ultra + Hair shadows. Separate-launch fallback, static camera.',
            estimated_phase_seconds=sum(p['seconds'] for p in plan))
        flow.write(HERE / 'plans' / (name + '.json'), plan)
        flow.write(HERE / 'jobs' / (name + '.json'), job)
    flow.tighten_files(HERE)


if __name__ == '__main__':
    main()
