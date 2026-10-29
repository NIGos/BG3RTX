"""Generate queue fixtures only. Never enqueue or launch a game."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tests/fixtures'
VARIANTS=('baseline','rebase','current_destroy_only')


def settings(fg,variant='baseline',view='normal',static=False):
    values=dict(fg=fg,rt_scene_rebase=int(variant=='rebase'),fg_current_destroy_only=int(variant=='current_destroy_only'),
        rt_primary_check=1,rt_primary_epoch=1,rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,
        rt_stats=0,ls_stats=0,rf_stats=0,qa_exposure_hold=int(static))
    for name,key,value in (('sun','rt_debug',2),('local','ls_debug',3),('GI','gi_debug',1),('reflections','rf_debug',1),('RR','rr_show',3)):
        if view==name: values[key]=value
    return ','.join(f'{k}={v}' for k,v in values.items())


def step(kind,fg,variant='baseline',speed='static',pixels=0,direction=0,view='normal',seconds=60,static=False):
    return dict(kind=kind,fg=fg,variant=variant,view=view,speed=speed,pixels_per_second=pixels,direction=direction,
        seconds=seconds,repeat_ms=(100 if speed=='slow' else 33) if kind=='pan' else 0,scan=32,
        motion_start=5,motion_end=seconds-5,capture_at=0,serial=0,
        label=f'{kind}-{speed}-{direction}-{variant}-fg{fg}-{view}',set=settings(fg,variant,view,static))


def plans():
    static=[step('static',fg,seconds=90,static=True) for fg in (2,0,2)]
    for view in ('sun','local','GI','reflections','RR'):
        for fg in (2,0):
            s=step('static',fg,view=view,seconds=55,static=True); s['trace_at']=[20,30,40]; static.append(s)
    motion=[step('static',fg,variant) for variant in VARIANTS for fg in (2,0)]
    for speed,pixels in (('slow',60),('medium',120),('fast',240)):
        for direction in (-1,1):
            for variant in VARIANTS:
                for fg in (2,0): motion.append(step('rotate',fg,variant,speed,pixels,direction))
    for speed in ('slow','fast'):
        for variant in VARIANTS:
            for fg in (2,0): motion.append(step('pan',fg,variant,speed,direction=1))
    motion += [step('static',fg) for fg in (2,0)]
    return static,motion


def job(name,plan,seconds,post):
    return dict(name=name,tree='.',save='QuickSave_3784',session=False,traces=0,hold=90,timeout=seconds+300,
        set='preset=ultra,fg=2,headless=0,fps_cap=0,rr=1,reflex=1,rt_stats=0,ls_stats=0,rf_stats=0,ls_k=0,ls_shadowed=1,'
            'rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,pan_capture=0,pan_prepare=0,rt_scene_camera_match=0,'
            'rt_scene_rebase=0,fg_current_destroy_only=0,rt_primary_check=1,rt_primary_epoch=1,crash_dump=1,'
            'fg_state_every=30,rt_timing_frames=120,dump_images=1,dump_png_width=1280,dump_raw=0,dump_max_mb=6144',
        window_mode='visible_unfocused_fg',
        args=['-RealPresent','-Move','-MovePlan','tests/fixtures/'+plan],
        patterns=['primary-ray check','rt epoch','rt passes','rt cost scope',r'\[watch\]',r'\[sl\]',r'\[trace\]','gpu RR:',
            'hybrid GI','ls stats','rf stats','material classes','rt applied',r'\[crash\]'],
        expect=[r'\[auto\] scene ready',r'\[crash\] exception diagnostics active independently of FG; minidump armed',
            'DLSS-G supported on this adapter: ok','slDLSSGSetOptions\\(mode on','slDLSSGSetOptions\\(mode off','rt epoch','primary-ray check'],
        forbid=[r'\[crash\] exception 0x','VK_ERROR_DEVICE_LOST'],
        expect_runner=[r'\[motion-plan\] posted pointer centre',r'\[window-mode\] visible_unfocused:',r'\[motion-plan\] complete phases=',r'^Run ended: done'],
        post_gate=True,post=post)


def main():
    static,motion=plans()
    for name,plan in (('p1_fg_static_plan.json',static),('p1_fg_motion_plan.json',motion)):
        (FIX/name).write_text(json.dumps(plan,indent=2)+'\n')
    pre=job('p1-fg-static-effects','p1_fg_static_plan.json',sum(s['seconds'] for s in static),
        'py -3 -m scripts.qa.fg_static_effects --log "{log}" --runner "{runner}" --plan tests/fixtures/p1_fg_static_plan.json --out captures/p1-fg/static-effects-result.json')
    pre['expect'] += ['rt applied','gpu RR:']
    pre['expect_runner'] += [r'\[motion-trace\]']
    run=job('p1-fg-motion-matrix','p1_fg_motion_plan.json',sum(s['seconds'] for s in motion),
        'py -3 -m scripts.qa.fg_motion_gate --log "{log}" --runner "{runner}" --plan tests/fixtures/p1_fg_motion_plan.json --out captures/p1-fg/motion-gate-result.json')
    run['requires_success_of']=['p1-fg-static-effects']
    run['expect_runner'] += [r'\[rotation-input\].*direction=1',r'\[rotation-input\].*direction=-1']
    for name,value in (('p1_fg_static_job.json',pre),('p1_fg_motion_job.json',run)):
        (FIX/name).write_text(json.dumps(value,indent=2)+'\n')
    print('Prepared only:',len(static),'static phases /',sum(s['seconds'] for s in static),'s;',len(motion),'motion phases /',sum(s['seconds'] for s in motion),'s')


if __name__=='__main__': main()
