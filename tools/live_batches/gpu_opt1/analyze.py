"""Read-only same-run float energy and static image gate for gpu_opt1."""
import argparse
import json
import math
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.m4_promotion_gate import Run, TRACE, SCENE_FS, WATER_AFTER_FS, ALPHA_FW, t_quantile
from scripts.qa.decode import Trace
from scripts import queue_gates

OFF=tuple('shot-off-'+c for c in 'abcd')
PAIRS={'compact':('shot-off-a','shot-compact','shot-off-b'),
       'alpha':('shot-off-b','shot-alpha','shot-off-c'),
       'water':('shot-off-c','shot-water','shot-off-d')}

def evaluate(means,images):
    """Existing Student/noise model, using only this run's four OFF shots.
    Image family controls every RGB component across all three ON comparisons.
    Zero OFF noise permits zero output change; no hand-tuned tolerances.
    """
    labels=set(OFF)|{p[1] for p in PAIRS.values()}
    if set(means)!=labels or set(images)!=labels:raise ValueError('missing or extra shot labels')
    if any(not math.isfinite(v) or v<=0 for v in means.values()):raise ValueError('invalid scene float mean')
    shapes={np.asarray(a).shape for a in images.values()}
    if len(shapes)!=1 or any(not np.isfinite(a).all() for a in images.values()):raise ValueError('image shape mismatch or nonfinite pixels')
    off=[np.asarray(images[x],dtype=np.float64) for x in OFF]
    scalar_sigma=math.sqrt(sum((means[b]-means[a])**2 for a,b in zip(OFF,OFF[1:]))/6)
    image_sigma=np.sqrt(sum((b-a)**2 for a,b in zip(off,off[1:]))/6)
    # Two attachment families (pre-water scene and pre-post water output),
    # three key comparisons. Same family-wise error rate as m4_promotion_gate.
    energy_band=t_quantile(1-ALPHA_FW/(2*2*3),3)*scalar_sigma*math.sqrt(1.5)
    image_band=t_quantile(1-ALPHA_FW/(2*2*3*image_sigma.size),3)*image_sigma*math.sqrt(1.5)
    rows={}
    for key,(a,on,b) in PAIRS.items():
        delta=means[on]-(means[a]+means[b])/2
        residual=np.abs(np.asarray(images[on],dtype=np.float64)-(np.asarray(images[a],dtype=np.float64)+np.asarray(images[b],dtype=np.float64))/2)
        excess=int(np.count_nonzero(residual>image_band))
        rows[key]=dict(energy_delta=delta,energy_noise_band=energy_band,energy_pass=abs(delta)<=energy_band,
                       image_excess_components=excess,image_pass=excess==0,image_max_residual=float(residual.max()),
                       image_max_noise_band=float(image_band.max()),
                       bit_identical_to_both_off=bool(np.array_equal(images[on],images[a]) and np.array_equal(images[on],images[b])))
    return rows

def capture(run,label):
    phase=run.phase_of(label)
    if not phase:raise ValueError(label+': missing phase')
    hits=[TRACE.search(txt) for t,txt in run.lines if phase[1]<=t<=phase[2]]
    hits=[m for m in hits if m]
    if len(hits)!=1:raise ValueError(label+': expected exactly one static trace')
    path=Path(hits[0].group(2).strip())
    return run.captures_root/path.name if run.captures_root else path

def image_detail(path,fs):
    trace=Trace(path);key=trace.find(fs,'R11G11B10F')
    if not key:raise ValueError(str(path)+': missing '+fs+' scene attachment')
    d=trace.dumps[key];png=path/d['png'];raw=png.with_name(png.stem+'_p.f32')
    if fs == WATER_AFTER_FS:
        # The exporter writes float previews only for SCENE_FS. After-water
        # PNG is used solely for image detail; energy always uses dumps.json.
        with Image.open(png) as im:return np.asarray(im.convert('RGB'),dtype=np.float64)/255.0
    if not raw.exists():raise ValueError(str(raw)+': required dump_preview_f32=1')
    with Image.open(png) as im:w,h=im.size
    pixels=np.fromfile(raw,np.float32)
    if pixels.size!=w*h*4:raise ValueError(str(raw)+': malformed float preview')
    return pixels.reshape(h,w,4)[...,:3]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log',type=Path);ap.add_argument('--captures-root',type=Path);ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();run=Run(args.log,captures_root=args.captures_root)
    game=args.log.read_text(encoding='utf-8-sig',errors='replace')
    runner=args.log.with_suffix('.runner.txt').read_text(encoding='utf-8-sig',errors='replace')
    failures=queue_gates.check(run.job['expect_every'],game,runner,run.plan)
    if run.failed_drift():failures.append(run.drift)
    for pattern in run.job.get('forbid',[]):
        if re.search(pattern,game):failures.append('forbidden log line: '+pattern)
    for pattern in run.job.get('expect',[]):
        if not re.search(pattern,game):failures.append('missing required log line: '+pattern)
    for pattern in run.job.get('expect_runner',[]):
        if not re.search(pattern,runner,re.M):failures.append('missing runner line: '+pattern)
    report={'log':str(args.log),'same_run_only':True,'families':{},'failures':failures}
    try:
        labels=(*OFF,*(p[1] for p in PAIRS.values()))
        paths={label:capture(run,label) for label in labels}
        for fs in (SCENE_FS,WATER_AFTER_FS):
            means={};images={}
            for label in labels:
                value=run.scene_colour(label,fs=fs)
                if not value or isinstance(value[0],str) or value[1]:raise ValueError(f'{label}/{fs}: invalid float mean {value}')
                means[label]=value[0] # Held exposure; full-resolution dump mean, never PNG mean.
                images[label]=image_detail(paths[label],fs)
            rows=evaluate(means,images);report['families'][fs]={'scene_means':means,'image_source':'float_preview' if fs==SCENE_FS else 'PNG detail only; float energy from dumps.json','keys':rows}
            failures.extend(f'{fs}/{key}: changed energy or image beyond same-run OFF noise' for key,row in rows.items() if not(row['energy_pass'] and row['image_pass']))
    except (ValueError,OSError,KeyError) as exc:failures.append(str(exc))
    report['passed']=not failures
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2));return int(bool(failures))
if __name__=='__main__':sys.exit(main())
