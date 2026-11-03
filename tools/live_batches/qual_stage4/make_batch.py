"""Prepare only; never stages a live queue or launches a process other than git reads."""
import copy
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def phase(label, seconds=8, settings='', direction=0, shot=False):
    p = dict(label=label, kind='static', speed='static', seconds=seconds,
             repeat_ms=0, scan=16, capture_at=0, serial=0,
             motion_start=seconds, motion_end=seconds, pixels_per_second=0,
             direction=0, set=settings)
    if direction:
        p.update(kind='rotate', turn_mode='keys', direction=direction,
                 motion_start=1, motion_end=5)
    if shot:
        p['trace_at'] = [5]
    return p


def gate(name, select, match, windows, **extra):
    return dict(name=name, select=select, match=match, windows=windows, min=1, **extra)


def finish(job, plan, stem):
    job.update(name='qs4-' + stem, live_batch='qual-stage4', window_mode='visible_unfocused_fg',
               session=False, hold=120, timeout=sum(p['seconds'] for p in plan) + 240,
               estimated_phase_seconds=sum(p['seconds'] for p in plan), stop_on_dump=False)
    job['args'] = ['-RealPresent', '-Move', '-MovePlan',
                   f'captures/live_batches/qual_stage4/plans/{stem}.json']
    job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                            rf'\[motion-plan\] complete phases={len(plan)}\b']
    for i, p in enumerate(plan):
        if p.get('trace_at'):
            job['expect_runner'].append(rf'\[motion-trace\] phase={i} label={p["label"]} repeat=0')
    write(HERE / 'plans' / (stem + '.json'), plan)
    write(HERE / 'jobs' / (stem + '.json'), job)


def main():
    # Prediction and untouched historical gates are written BEFORE jobs/plans.
    branches = ['codex-perf-hitmat', 'codex-perf-hooks', 'codex-perf-submit2',
                'hm-recipe-rows', 'variant-migration', 'gi-reproj-guards',
                'hair-cost2', 'hair-cost3', 'hair-cost4', 'hair-anyhit-perf',
                'claude/rtx-hair-groom-33655b', 'worktree-agent-a78f48005e3b350bd',
                'worktree-agent-a53f0b584fa39317b', 'promote-omm', 'fixes4',
                'edge-flicker', 'wo-fixes', 'wf9-compose-shift', 'rt-reflections-offscreen',
                'fix-elastic-drops', 'worktree-agent-a0c9be1ab336eae1a',
                'scc1-regate', 'scc1-regate2', 'vram-default-on', 'box-ultra',
                'codex-as-compact', 'codex-vram-motion', 'hm-camp-grass',
                'worktree-agent-a044365d27d7f43c4']
    audit = dict(main=git('rev-parse', 'main'), head=git('rev-parse', 'HEAD'), branches={})
    for branch in branches:
        audit['branches'][branch] = dict(tip=git('rev-parse', branch),
            unmerged=git('log', '--oneline', audit['main'] + '..' + branch).splitlines())
    audit['main_key_lines'] = [line for line in git('show', audit['main'] + ':bg3rtx.ini').splitlines()
        if line.startswith(('perf_hitmat_lists=', 'perf_hook_state=', 'perf_submit_batch=',
                            'gi_temporal_mv_sample=', 'gi_prev_radiance_guard=', 'rt_hit_material_generated='))]
    audit['main_spec_lines'] = [line for line in git('show', audit['main'] + ':common/shader_variant.h').splitlines()
                               if '1001' in line or '1002' in line]
    write(HERE / 'merge_audit.json', audit)
    prediction = dict(status='PREPARED_SCREENING_ONLY_NOT_AUTHORIZED', queued=False,
        limits=dict(seconds_per_save=300, disk_mb=2048),
        blockers=[
            'Submit gate requires four 90 s windows: 360 s before warmup. Lean CPU job is screening only.',
            'CPU camp repetition, exact per-key steady-frame/sample minima and reference checks remain required.',
            'Original GI transient gate needs five K16 full float bursts (~11 GiB). PNG Q/E pairs cannot pass it.',
            'No promotion from a dryrun or from screening. No live permission inherited.'],
        cpu={}, generated=json.loads((HERE.parent / 'm4_generated/predicted.json').read_text()),
        guards=json.loads(git('show', 'gi-reproj-guards:tools/live_batches/gi_reproj_guards/predicted.json')),
        common=dict(energy='Static pre-post float scene mean in dumps.json, within same-run OFF-repeat noise; never PNG/bloom mean.',
                    costs='CPU ms normalized by presents/draws/builds; frame time recorded but not judged unfocused. GPU pass ms within OFF repeat uncertainty.',
                    restoration='All experimental CPU/GI/generated keys 0; qa_exposure_hold and diagnostics 0; runner restores original INI on all exit paths.'))
    for b in branches[:3]:
        prediction['cpu'][b] = json.loads((ROOT / 'docs/progress' / b / 'predicted.json').read_text())
    write(HERE / 'predicted.json', prediction)

    templates = []
    for idx, scene in enumerate(('camp', 'inn'), 1):
        old = HERE.parent / 'm4_generated'
        job = json.loads(next((old / 'jobs').glob(f'hgn{idx}-*.json')).read_text())
        plan = json.loads((old / 'plans' / f'hgn_{scene}.json').read_text())
        # Keep every existing m4 gate and its labels. Use actual qualified Ultra.
        job['set'] += ',ls_box=2,rt_omm=1,rt_omm_dyn=1,rt_vt_alpha_masks=1,rt_alpha_fine_lod=1,gi_far=1,gi_far_m=6,rf_hit_shade=0,crash_dump=0,perf_hitmat_lists=0,perf_hook_state=0,perf_submit_batch=0,gi_temporal_mv_sample=0,gi_prev_radiance_guard=0'
        templates.append(copy.deepcopy(job))
        if scene == 'camp':
            for label, mv, prev in [('off-a', 0, 0), ('mv', 1, 0), ('prev', 0, 1), ('off-b', 0, 0)]:
                settings = f'rt_stats=0,rt_hit_material_generated=0,qa_exposure_hold=1,gi_temporal_mv_sample={mv},gi_prev_radiance_guard={prev}'
                plan += [phase('guard-shot-' + label, settings=settings, shot=True),
                         phase('guard-q-' + label, settings=settings, direction=-1),
                         phase('guard-e-' + label, settings=settings, direction=1)]
            plan.append(phase('restore-guards', 5, 'gi_temporal_mv_sample=0,gi_prev_radiance_guard=0,qa_exposure_hold=0'))
            job['purpose'] += ' GI static float-energy / Q/E PNG screening only; original temporal gate remains pending.'
        finish(job, plan, f'{idx:02d}-{scene}')

    # Individual-key CPU screening; no ensemble attribution or hidden qualification claim.
    job = templates[0]
    job['save'] = 'Water Queens House - 92h 14m'
    s8 = json.loads((HERE.parent / 'spec_cascade/jobs/scc1-spec-cascade-s8-fg2.json').read_text())
    job['save'] = s8['save']
    job['set'] += ',rt_hit_material_generated=0,cpu_timing=1,watchdog_s=1'
    job['expect'] = [r'\[auto\] scene ready', r'\[cpu\] hit material', r'\[cpu\] capture hooks',
                     r'\[cpu\] submit-time instance matrix copies',
                     r'\[watch\].*submit CPU [0-9.]+ ms.*descriptor tracker CPU [0-9.]+ ms',
                     r'\[pace\] rendered frame time over 600 frames']
    job['patterns'] += [r'\[cpu\]', r'\[watch\]', r'\[mem\]']
    plan = [phase('warmup', 20, 'rt_stats=0,qa_exposure_hold=0')]
    keys = ['perf_hitmat_lists', 'perf_hook_state', 'perf_submit_batch']
    names = []
    for short, key in zip(('hitmat', 'hooks', 'submit'), keys):
        for state in ('off-a', 'on-a', 'off-b', 'on-b'):
            label = f'cost-{short}-{state}'
            settings = ','.join(f'{k}={int(k == key and state.startswith("on"))}' for k in keys)
            plan.append(phase(label, 20, settings))
            names.append(label)
    plan.append(phase('restore', 5, ','.join(f'{k}=0' for k in keys) + ',cpu_timing=0,watchdog_s=0,qa_exposure_hold=0'))
    # Reuse existing per-window current-frame/capacity gates, applying to ALL cost windows.
    job['expect_every'] = [copy.deepcopy(g) for g in job['expect_every']
                           if g['name'].startswith(('scene wait:', 'capacity:', 'gi trace'))]
    for g in job['expect_every']:
        g['windows'] = names
    for short, key, select in [('hitmat', keys[0], r'\[cpu\] hit material'),
                              ('hooks', keys[1], r'\[cpu\] capture hooks')]:
        for state in ('off-a', 'on-a', 'off-b', 'on-b'):
            job['expect_every'].append(gate(f'{short} tagged CPU sample {state}', select,
                key + (' 1' if state.startswith('on') else ' 0'), [f'cost-{short}-{state}']))
    for state in ('off-a', 'on-a', 'off-b', 'on-b'):
        job['expect_every'].append(gate('submit and descriptor tracker CPU ' + state,
            r'\[watch\]', r'submit CPU [0-9.]+ ms.*descriptor tracker CPU [0-9.]+ ms', ['cost-submit-' + state]))
        job['expect_every'].append(gate('matrix samples ' + state,
            r'\[cpu\] submit-time instance matrix copies',
            'perf_hook_state ' + ('1' if state.startswith('on') else '0'), ['cost-hooks-' + state]))
    job['purpose'] = 'CPU SCREENING ONLY: individual-key OFF/ON/OFF/ON. Existing qualification minima in predicted.json are mandatory and exceed this lean schedule; no qualification or promotion permitted.'
    finish(job, plan, '03-s8')


if __name__ == '__main__':
    main()
