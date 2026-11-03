"""Regenerate the offline compact join job and its diagnostic plan. Never queues."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Keep expensive statistics confined to observe windows; this batch measures no cost.
JOB = {'save': 'Campsite - 27h',
 'session': False,
 'traces': 0,
 'hold': 120,
 'repeat': 1,
 'set': 'preset=ultra,fg=2,headless=0,fps_cap=0,rt=1,rr=1,reflex=1,crash_dump=0,rt_timing_frames=120,rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,rt_order_trace=0,rt_stability_log=0,rt_stats=0,rt_primary_check=0,rt_primary_epoch=0,fg_state_every=30,rt_native_hair=0,rt_native_scalp=0,rt_hair_physical_origin=0,rt_omm=0,rt_sun_cascade_current=0,rt_native_tileset=0,rt_alpha_spill=0,ls_box=0,ls_k=0,rt_hit_reference=0,sharc=0,rt_bvh_split=1,rt_alpha=1,rt_cascade_casters=1,gi=1,gi_rt=1,gi_rt_fallback=0,rt_reflections=1,rt_hit_material=1,rt_emissive=1,rt_em_fx_pulse=0,rt_xfb_mb=1024,rt_cascade_mb=427,gi_hit_direct=1,gi_hit_sun=1,rf_hit_shade=0,rt_hit_material_instanced=1,rt_hit_material_wind_rgb=1,rt_hit_material_ip_grass=0,rt_hit_material_generated=0,rt_hit_material_paks=1,rt_hit_emission=1,qa_exposure_hold=0,rt_scene_wait_log_frames=120,rt_mesh_content_id=1,fg_log_presents=6,fg_snap=0,fg_snap_any=0,fg_snap_delay=0,fg_snap_delay_ms=0',
 'patterns': ['\\[hot\\]',
              '\\[ngx\\] RR create',
              '\\[sl\\] DLSS-G frame',
              '\\[pace\\]',
              '\\[watch\\]',
              'rt passes: GPU cost',
              'rt applied',
              '\\[rt\\] scene wait',
              '\\[rt\\] scene build GPU',
              '\\[trace\\]',
              '\\[shaders\\].*\\?',
              '\\[crash\\]',
              'FAILED',
              '\\[geo\\] transform feedback',
              '\\[geo\\] \\d+ vertex shader modules',
              'gi: GPU cost',
              'gi: trace pass',
              'GI hit direct',
              'hit material',
              'rt stats',
              '\\[rrpost\\] exposure (held|hold|probe)',
              '\\[hit material pool pairs\\]',
              '\\[hit material instanced\\]',
              '\\[geo\\] RT capture',
              '\\[cascade\\] casters',
              '\\[hit material paks\\]',
              '\\[hit material paks join\\]',
              '\\[mesh content'],
 'forbid': ['\\[crash\\] exception 0x',
            'VK_ERROR_DEVICE_LOST',
            '\\[hot\\] core (rejected|faulted)',
            'FAILED, keeping previous',
            'compile call failed',
            'RR creation failed',
            'RR evaluate failed'],
 'window_mode': 'visible_unfocused_fg',
 'name': 'cp4-compact-camp-join',
 'live_batch': 'cov-paks-compact',
 'timeout': 370,
 'args': ['-RealPresent',
          '-Move',
          '-MovePlan',
          'captures/live_batches/cov_paks_compact/plans/compact_camp.json'],
 'expect': ['\\[auto\\] scene ready',
            '\\[hit material paks\\] loaded 40983 candidates',
            '\\[hit material paks join\\] attempts \\d+ unique \\d+ missing \\d+ ambiguous \\d+ '
            'invalid \\d+ witness \\d+ recipe_cull \\d+ inputs_pending \\d+ id_equal \\d+ '
            'id_different \\d+ draws [1-9]\\d*'],
 'expect_every': [{'name': 'draw attempts and rejection reasons in each ON observation',
                   # cp4: 240 frames in 16.020 s = 8.010 s per 120-frame line.
                   # Round up; do not infer diagnostic FPS from the 30 FPS cap.
                   'period_s': 9,
                   'select': '\\[hit material paks join\\]',
                   'match': '\\[hit material paks join\\] attempts \\d+ unique \\d+ missing \\d+ '
                            'ambiguous \\d+ invalid \\d+ witness \\d+ recipe_cull \\d+ '
                            'inputs_pending \\d+ id_equal \\d+ id_different \\d+ draws [1-9]\\d*',
                   'windows': ['observe-on', 'observe-on-repeat'],
                   'min': 1}],
 'expect_runner': ['\\[window-mode\\].*visible_unfocused',
                   '^Run ended: done',
                   '\\[motion-plan\\] complete phases=5\\b'],
 'purpose': 'Unqueued diagnostic only. Count UV candidates, exact witnesses and reasons. Zero '
            'candidates is a valid diagnostic result; no material promotion.',
 'estimated_phase_seconds': 75,
 'live_flow': {'expected_fps': 30,
               'settings_latency_s': 1,
               'trace_stall_s': 1,
               'startup_only': ['rt_xfb_mb']}}


def make():
    plan = []
    for label, seconds, paks, stats in [
        ("warmup", 10, 1, 0), ("warmup-off", 5, 0, 0),
        ("observe-on", 20, 1, 1), ("observe-off", 10, 0, 1),
        ("observe-on-repeat", 20, 1, 1), ("restore", 5, 0, 0),
    ]:
        plan.append(dict(label=label, kind="static", speed="static", seconds=seconds,
                         repeat_ms=0, scan=16, capture_at=0, serial=0, motion_start=seconds,
                         motion_end=seconds, pixels_per_second=0, direction=0,
                         set=f"rt_hit_material_paks={paks},rt_mesh_content_id=1,rt_stats={stats},qa_exposure_hold=0"))
    # The failed 18:05 camp run kept compiling baseline shaders long after ten
    # seconds. Wait for completed prefetch AND actual renderer pipelines, not
    # a cache-stat line (which can appear between queued compile requests).
    plan[0].update(wait_log=[
        r"\[shaders\] cache: prefetched ",
        r"gi: gi/gi_trace_rt_hit_sun_em\.hlsl\?[^ ]* generation ",
        r"\[water\] rt/water_reflect\.hlsl\?[^ ]*WATER_HIT_SHADE_LAYOUT=1[^ ]* water_reflect: built",
        r"\[water\] rt/water_compose\.hlsl\?[^ ]*WATER_HIT_SHADE_LAYOUT=1[^ ]* water_compose: built",
    ], wait_log_timeout=600)
    job = dict(JOB)
    # Participation now requires a retired UV attempt, not just keyed draws.
    job["expect"] = [rx.replace(r"attempts \d+", r"attempts [1-9]\d*") for rx in job["expect"]]
    job["expect_every"][0]["match"] = job["expect_every"][0]["match"].replace(r"attempts \d+", r"attempts [1-9]\d*")
    job["estimated_phase_seconds"] = sum(p["seconds"] for p in plan)
    job["timeout"] = job["estimated_phase_seconds"] + 300 + 600
    job["expect_runner"] = [r"\[window-mode\].*visible_unfocused", r"^Run ended: done",
                            rf"\[motion-plan\] complete phases={len(plan)}\b"]
    for path, value in [(HERE / "plans/compact_camp.json", plan),
                        (HERE / "jobs/cp4-compact-camp-join.json", job)]:
        path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    from tools.live_batches.live_flow import tighten_files, read
    tighten_files(HERE)
    return read(HERE / "jobs/cp4-compact-camp-join.json"), read(HERE / "plans/compact_camp.json")


if __name__ == "__main__":
    make()
    from tools.live_batches.live_flow import tighten_files
    tighten_files(Path(__file__).resolve().parent)
