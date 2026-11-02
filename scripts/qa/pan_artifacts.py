"""Strict, same-run analysis of 16-frame pan bursts. Never treats missing data as zero.

py -3 -m scripts.qa.pan_artifacts <burst directories> --out <report directory>
GPU camera headers drive reprojection. No image registration or human labels.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

REQUIRED = {'depth','normal','albedo','material','primary','sun','local','gi','final','scene_pose'}


def decode(path, entry):
    w, h, fmt = entry['width'], entry['height'], entry['format']
    data = path.read_bytes()
    if fmt in (97, 83):  # RGBA16F, RG16F
        return np.frombuffer(data, '<f2').reshape(h, w, 4 if fmt == 97 else 2).astype('f4')
    if fmt == 100:
        return np.frombuffer(data, '<f4').reshape(h, w, 1)
    if fmt in (37, 43, 44, 50):
        a = np.frombuffer(data, 'u1').reshape(h, w, 4).astype('f4') / 255
        return a[..., [2, 1, 0, 3]] if fmt in (44, 50) else a
    if fmt in (64, 58):
        v = np.frombuffer(data, '<u4').reshape(h, w)
        a = np.stack([(v >> s) & 1023 for s in (0, 10, 20)], -1).astype('f4') / 1023
        return a if fmt == 64 else a[..., ::-1]
    if fmt == 122:
        v = np.frombuffer(data, '<u4').reshape(h, w)
        channels = []
        for shift, bits in ((0, 6), (11, 6), (22, 5)):
            c = (v >> shift) & ((1 << (bits + 5)) - 1)
            e, m = c >> bits, c & ((1 << bits) - 1)
            finite = np.where(e == 0, np.ldexp(m.astype('f4'), -14-bits),
                              np.ldexp(1 + m.astype('f4') / (1 << bits), e.astype('i4') - 15))
            channels.append(np.where(e == 31, np.where(m == 0, np.inf, np.nan), finite))
        return np.stack(channels, -1)
    raise ValueError(f'unsupported format {fmt}')


def camera(primary):
    raw = np.rint(primary[0, :48]).astype('u1').tobytes()
    values = np.frombuffer(raw, '<f4').astype('f8')
    if not np.isfinite(values).all():
        raise ValueError('nonfinite GPU camera header')
    rays = values[:12].reshape(3, 4)[:, :3]
    matrix = np.stack([rays[0], rays[1]-rays[0], rays[2]-rays[0]], axis=1)
    return matrix, values[12:14], values[14:16], values[16:19]


def scene_camera_origin(snapshot):
    """Only the camera matrix and origin are floating-point pose fields.

    The native 224-byte UBO also contains packed/scalar metadata. In particular
    word 55 can be 0xffffffff, a NaN when interpreted as float, but is not part
    of the camera origin. Preserve its raw capture; never use it as image data.
    """
    values = snapshot.reshape(-1)
    if len(values) != 56 or not np.isfinite(values[32:48]).all() or not np.isfinite(values[52:55]).all():
        raise ValueError('invalid GPU scene camera matrix/origin')
    return values[52:55]


def world_normals(encoded, primary):
    values = np.frombuffer(np.rint(primary[0,:48]).astype('u1').tobytes(),'<f4')
    rotation = values[20:36].reshape(4,4)[:3,:3]
    e = encoded*3.5554-1.7777
    s = 2/(np.sum(e*e,axis=-1)+1)
    n = np.concatenate([e*s[...,None],(1-s)[...,None]],axis=-1) @ rotation
    n /= np.maximum(np.linalg.norm(n,axis=-1,keepdims=True),1e-20)
    return n


def texture_compatible(source, target, iy, ix):
    """Two half-texel footprints plus both UNORM8 conversion intervals.

    Exact RGB equality would reject a stationary textured surface under raster
    jitter. Material class/flags are checked separately, without interpolation.
    """
    sy, sx = np.gradient(source, axis=(0, 1))
    ty, tx = np.gradient(target, axis=(0, 1))
    footprint = .5*(np.abs(sx)+np.abs(sy)+np.abs(tx[iy,ix])+np.abs(ty[iy,ix]))
    return np.all(np.abs(source-target[iy,ix]) <= footprint+2/255, axis=-1)


def static_camera(cameras, movements):
    # Native origins must be identical. Two raster samples can differ by one
    # pixel in each axis (DLSS/TAA +/-0.5), i.e. sqrt(2) pixels radially.
    known = [m for m in movements if m is not None]
    return (bool(known) and max(known) <= np.sqrt(2) and
            all(np.array_equal(c[3], cameras[0][3]) for c in cameras))


def reproject(depth, source_camera, target_camera):
    """Source pixels -> target pixels and expected target linear depth."""
    h, w = depth.shape
    y, x = np.indices((h, w), dtype='f8')
    matrix, scale, bias, origin = source_camera
    ab = (np.stack([(x+.5)/w, (y+.5)/h], -1) - bias) / scale * .5
    rays = np.einsum('ij,hwj->hwi', matrix, np.concatenate([np.ones((h,w,1)), ab], -1))
    target, ts, tb, to = target_camera
    relative = rays * depth[..., None] + origin - to
    coeff = np.einsum('ij,hwj->hwi', np.linalg.inv(target), relative)
    z = coeff[..., 0]
    uv = coeff[..., 1:] / np.where(z[...,None] != 0, z[...,None], np.nan) * (2*ts) + tb
    xx, yy = uv[...,0]*w-.5, uv[...,1]*h-.5
    good = np.isfinite(xx) & np.isfinite(yy) & (z > 0)
    ix = np.rint(np.nan_to_num(xx)).astype('i4').clip(0,w-1)
    iy = np.rint(np.nan_to_num(yy)).astype('i4').clip(0,h-1)
    good &= (xx >= 0) & (xx <= w-1) & (yy >= 1) & (yy <= h-1)
    return iy, ix, z, good, np.hypot(xx-x, yy-y)


def stable_samples(anchor, target):
    depth = anchor['depth'][...,0]
    iy, ix, z, good, motion = reproject(depth, anchor['camera'], target['camera'])
    td = target['depth'][...,0]
    # Nearest sampling covers half a pixel per axis. Local depth range bounds
    # that footprint; float32 roundoff is added. Silhouettes fail albedo/material.
    dy, dx = np.gradient(td)
    bound = (np.abs(dx) + np.abs(dy))*.5 + np.abs(td)*np.finfo('f4').eps*8
    good &= (depth > 0) & (np.abs(td[iy,ix]-z) <= bound[iy,ix])
    good &= texture_compatible(anchor['albedo'], target['albedo'], iy, ix)
    good &= texture_compatible(anchor['material'][...,:3], target['material'][...,:3], iy, ix)
    good &= anchor['material'][...,3] == target['material'][iy,ix,3]
    na, nt = anchor['world_normal'], target['world_normal'][iy,ix]
    # Stereographic decoding has derivative <= 2; the encoding scale is 3.5554.
    # FP16 may choose either adjacent value. Add half-pixel normal variation.
    ny,nx = np.gradient(target['world_normal'],axis=(0,1))
    footprint = (np.linalg.norm(nx,axis=-1)+np.linalg.norm(ny,axis=-1))*.5
    rounding = 2*3.5554*np.sqrt(2)*2**-10*2
    good &= np.linalg.norm(na-nt,axis=-1) <= rounding + footprint[iy,ix]
    good[0] = False
    return iy, ix, good, motion


def transient(sequence, valid, threshold):
    """1-3-frame valleys bracketed by recovery; sustained shadows are excluded."""
    t, h, w = sequence.shape
    flags = np.zeros((t,h,w), bool)
    events = 0
    for duration in (1,2,3):
        for start in range(1, t-duration):
            before, after = sequence[start-1], sequence[start+duration]
            low = sequence[start:start+duration]
            mask = np.all(valid[start-1:start+duration+1], axis=0)
            mask &= np.abs(before-after) <= threshold
            mask &= np.all(np.minimum(before,after)[None]-low > threshold, axis=0)
            new = mask & ~np.any(flags[start:start+duration], axis=0)
            events += int(new.sum())
            flags[start:start+duration] |= mask
    return flags, events


def coverage_mask(rgb8, shape):
    """Traced-water pixels of an rt_debug 21 view (shaders/rt/water_view.hlsl: exact codes, green = water in
    front of the bed, blue = water over an empty G-buffer pixel), resized (nearest) to `shape` (h, w).

    A code pixel has its other two channels at 0: at most one UNORM8 step (the PNG's rounding) is allowed, and the
    code channel at least half the range (1.0 written; a darker pixel is the game's colour, not a code).
    """
    a = np.asarray(Image.fromarray(np.asarray(rgb8, dtype='u1')).resize((shape[1], shape[0]),
                                                                           Image.Resampling.NEAREST)).astype('i4')
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return ((g >= 128) & (r <= 1) & (b <= 1)) | ((b >= 128) & (r <= 1) & (g <= 1))


def calibration_noise(sequence, valid, quantum):
    """Observed static change at each world sample, never a scene-wide maximum."""
    good = valid[1:] & valid[:-1]
    change = np.abs(np.diff(sequence, axis=0))
    return np.max(np.where(good, change, 0), axis=0)+quantum, np.all(valid, axis=0)


def storage_quantum(fmt, maximum):
    if fmt in (58, 64):
        return 1/1023
    if fmt in (37, 43, 44, 50):
        return 1/255
    if fmt == 97:
        # Distance to the next native half-float value, including subnormals.
        v = np.float16(maximum)
        return float(np.float32(np.nextafter(v, np.float16(np.inf)))-np.float32(v))
    raise ValueError(f'no signal precision bound for format {fmt}')


def positive_control_status(reports, minimum_fps=18):
    """Reproduction is distinct from a renderer-fix gate or proof of lag."""
    controls = [r for r in reports if r.get('role') == 'static-evaluation' and
                r.get('calibrated_pixel_frames', 0) and r.get('primary_match_fraction')]
    if not controls:
        return 'INCOMPLETE: no independent static evaluation'
    static_rate = max(r['effects']['luma']['pixel_frames']/r['calibrated_pixel_frames'] for r in controls)
    static_primary = min(v for r in controls for v in r['primary_match_fraction'] if v is not None)
    evaluated = 0
    for r in reports:
        if r.get('role') != 'panning' or not r.get('calibrated_pixel_frames', 0):
            continue
        if r.get('burst_fps', 0) < minimum_fps:
            r['positive_control_status'] = 'INCOMPLETE: capture cadence below requested baseline'
            continue
        primary = [v for v in r['primary_match_fraction'] if v is not None]
        if not primary:
            continue
        evaluated += 1
        rate = r['effects']['luma']['pixel_frames']/r['calibrated_pixel_frames']
        reproduced = rate > static_rate and min(primary) < static_primary
        r['positive_control_status'] = 'REPRODUCED' if reproduced else 'NOT_REPRODUCED'
    if any(r.get('positive_control_status') == 'REPRODUCED' for r in reports):
        return 'REPRODUCED: final-colour transient excess and primary mismatch at comparable cadence'
    return 'NOT_REPRODUCED' if evaluated else 'INCOMPLETE: no calibrated pan at comparable cadence'


def index_burst_samples(meta):
    if not meta['valid_transport']:
        raise ValueError('transport invalid')
    if meta.get('K', 16) != 16:
        raise ValueError('burst does not declare K16')
    frames = {}
    for e in meta['samples']:
        if e['name'] not in REQUIRED:
            continue
        f = frames.setdefault(e['record_frame'], {})
        generation = e.get('command_recording')
        if generation is not None:
            if not generation:
                raise ValueError('invalid command recording')
            # G-buffer, RT debug masks and pose share one lighting recording.
            # Native final tonemap is intentionally recorded in another CB.
            if e['name'] != 'final' and f.setdefault('command_recording', generation) != generation:
                raise ValueError('mixed command recordings in one captured frame')
        if e['name'] in f:
            raise ValueError('duplicate captured view in one frame')
        f[e['name']] = e
    indices = sorted(frames)
    if len(indices) != 16 or np.any(np.diff(indices) != 1):
        raise ValueError('burst is not 16 consecutive frames')
    for i in indices:
        missing = REQUIRED-frames[i].keys()
        if missing:
            raise ValueError(f'frame {i}: missing {sorted(missing)}')
    return indices, frames


def load_burst(directory):
    meta = json.loads((directory/'burst.json').read_text())
    indices, entries = index_burst_samples(meta)
    frames = {}
    for i in indices:
        f = frames.setdefault(i, {})
        for name in REQUIRED:
            e = entries[i][name]
            f[name] = decode(directory/e['file'], e)
            f.setdefault('formats', {})[name] = e['format']
            f.setdefault('submits', {})[name] = e['submit_frame']
            if name == 'depth': f['time_ms'] = e.get('time_ms')
    for i in indices:
        f = frames[i]
        missing = REQUIRED-f.keys()
        if missing:
            raise ValueError(f'frame {i}: missing {sorted(missing)}')
        f['camera'] = camera(f['primary'])
        f['world_normal'] = world_normals(f['normal'],f['primary'])
        f['scene_origin'] = scene_camera_origin(f['scene_pose'])
        if not all(np.isfinite(f[k]).all() for k in REQUIRED-{'scene_pose'}):
            raise ValueError(f'frame {i}: nonfinite capture')
        h,w = f['depth'].shape[:2]
        final = f['final'][...,:3]
        # Resize RGB channels separately, preserving final encoded luminance.
        f['luma'] = sum(np.asarray(Image.fromarray(final[...,c],mode='F').resize((w,h)),dtype='f4')*weight
                        for c,weight in enumerate((.2126,.7152,.0722)))
        f['sun_mask'] = f['sun'][...,0]
        f['local_mask'] = f['local'][...,2]
        gi = f['gi'][...,:3] @ np.array([.2126,.7152,.0722],dtype='f4')
        f['gi_luma'] = np.asarray(Image.fromarray(gi,mode='F').resize((w,h)),dtype='f4')
    return meta, indices, frames


def analyze(directory, out):
    meta, ids, frames = load_burst(directory)
    anchor = frames[ids[7]]
    keys = ('luma','sun_mask','local_mask','gi_luma')
    stacks = {key: [] for key in keys}
    valid, movements, primary = [], [], []
    for i in ids:
        f = frames[i]
        iy, ix, good, movement = stable_samples(anchor, f)
        valid.append(good)
        movements.append(float(np.median(movement[good])) if good.any() else None)
        for key in keys:
            stacks[key].append(f[key][iy,ix])
        p = f['primary'][1:]
        receivers = p[...,3] != 0
        matched = (p[...,3] > 0) & (np.abs(p[...,0]) < 1)
        primary.append(float(matched[receivers].mean()) if receivers.any() else None)
    valid = np.stack(valid)
    common = np.all(valid,axis=0)
    report = {'directory':str(directory.resolve()), 'serial':meta['serial'], 'frames':ids,
              'stable_pixels':int(common.sum()), 'median_motion_pixels':movements,
              'stable_pixel_frames':int(valid.sum()),
              'primary_match_fraction':primary, 'effects':{},
              'rt_gbuffer_pose_offset_frames':None,
              'pose_offset_status':'not established: record/submit indices are not pose identity',
              'gate':'INCOMPLETE: requires independent static calibration and pose alignment evidence'}
    times = [frames[i]['time_ms'] for i in ids]
    if all(t is not None for t in times) and times[-1] > times[0]:
        report['burst_fps'] = 15000/(times[-1]-times[0])
        report['burst_fps_source'] = 'consecutive G-buffer record timestamps; real presentation verified separately'
        report['frame_intervals_ms'] = np.diff(times).tolist()
    offsets = []
    for index in range(3,13):
        light_origin = frames[ids[index]]['camera'][3]
        candidates = []
        for delta in range(-3,4):
            scene = frames[ids[index+delta]]['scene_origin']
            if np.array_equal(scene,light_origin.astype('f4')): candidates.append(delta)
        offsets.append(candidates)
    report['conversion_camera_offset_candidates'] = offsets
    unique = [x[0] for x in offsets if len(x)==1]
    if len(unique)==10 and len(set(unique))==1:
        report['rt_gbuffer_pose_offset_frames'] = unique[0]
        report['pose_offset_status'] = 'exact GPU conversion-camera origin match; instance-transform timing not yet proven'
    is_static = static_camera([frames[i]['camera'] for i in ids], movements)
    arrays = {key:np.stack(value) for key,value in stacks.items()}
    report['static'] = bool(is_static)   # numpy bool: json.dumps refused it (wf1 093458)
    report['storage_quantum'] = {
        key:max(storage_quantum(frames[i]['formats'][source], float(np.max(frames[i][source]))) for i in ids)
        for key,source in (('luma','final'),('sun_mask','sun'),('local_mask','local'),('gi_luma','gi'))}
    # Retain one full anchor for static-world calibration transfer. Other
    # frames only need colour/depth/camera for contact sheets; avoid keeping
    # four entire 16-frame G-buffer sets resident during a multi-burst run.
    for i in ids:
        if i != ids[7]:
            frames[i] = {k:v for k,v in frames[i].items() if k in ('final','depth','camera')}
    return report, arrays, valid, frames, ids


def fg_burst_evidence(frame_ids, log):
    """Existing per-frame SDK rows, not disk-write time or a fix gate."""
    from .fg_motion_gate import normalize_clocks
    from .rebase_gate import clock
    log, _ = normalize_clocks(log, '')
    states = {}
    duplicates = []
    for line in log.splitlines():
        m = re.search(r'DLSS-G frame (\d+): status (0x[\da-f]+), presented per app frame (\d+).*api errors (\d+)',line)
        if m and clock(line) is not None:
            frame,status,presented,errors = m.groups()
            if int(frame) in states: duplicates.append(int(frame))
            states[int(frame)] = dict(time=clock(line),status=int(status,16),presented=int(presented),api_errors=int(errors))
    selected = [states[i] for i in frame_ids if i in states]
    missing = [i for i in frame_ids if i not in states]
    ambiguous = sorted(set(frame_ids) & set(duplicates))
    intervals = [1000*(b['time']-a['time']) for a,b in zip(selected,selected[1:])] if not missing else []
    duration = selected[-1]['time']-selected[0]['time'] if len(selected)>1 else 0
    return {'fg_frame_states':selected,'fg_missing_frames':missing,'fg_duplicate_frames':ambiguous,
            'fg_active_every_captured_frame':bool(frame_ids) and not missing and not ambiguous and
                all(row['status']==0 and row['presented']>=2 and row['api_errors']==0 for row in selected),
            'presentation_fps':(len(selected)-1)/duration if duration>0 and intervals and min(intervals)>0 else None,
            'presentation_intervals_ms':intervals,
            'presentation_evidence_source':'SDK present-end log rows matched to exact captured record-frame indices; no GPU scanout timing claim'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bursts',nargs='*',type=Path)
    ap.add_argument('--log',type=Path,help='Optional same-run log; also resolves exact burst paths when none are supplied')
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--mask',action='append',default=[],
                    help='rt_debug 21 water-coverage PNG (coverage_mask), PATH for every burst or SERIAL=PATH for one; '
                         'applied in the burst anchor\'s pixels (frame 7): exact for a static burst at the mask\'s pose')
    args = ap.parse_args()
    masks_by_serial = {}
    for item in args.mask:
        serial, _, path = item.rpartition('=') if '=' in item else ('', '', item)
        masks_by_serial[serial] = np.asarray(Image.open(path).convert('RGB'))
    log = args.log.read_text(errors='replace') if args.log else ''
    if not args.bursts and args.log:
        args.bursts = [Path(m[1].strip()) for line in log.splitlines()
                       if (m := re.search(r'\[pan\] burst \d+ complete:.* -> (.+)$',line))]
    if not args.bursts: ap.error('no captured bursts')
    args.out.mkdir(parents=True,exist_ok=True)
    results, loaded = [], []
    for directory in args.bursts:
        try:
            item = analyze(directory,args.out)
            if args.log: item[0].update(fg_burst_evidence(item[0]['frames'],log))
            loaded.append(item)
            results.append(item[0])
        except (ValueError,KeyError,OSError,np.linalg.LinAlgError) as e:
            results.append({'directory':str(directory),'gate':'INVALID','error':str(e)})
    static = [item for item in loaded if item[0]['static'] and item[0]['stable_pixels']]
    if static:
        # First static burst calibrates; a second is independent evaluation.
        # Transfer bounds only to matching world surfaces. Unseen surfaces
        # remain unscored, never a false report of zero flashes.
        base_report, base_arrays, base_valid, base_frames, base_ids = static[0]
        baseline = base_frames[base_ids[7]]
        thresholds = {key:calibration_noise(seq,base_valid,base_report['storage_quantum'][key])
                      for key,seq in base_arrays.items()}
        for report, arrays, valid, frames, ids in loaded:
            iy,ix,calibrated,_ = stable_samples(frames[ids[7]],baseline)
            report['role'] = ('calibration' if report is base_report else
                              'static-evaluation' if report['static'] else 'panning')
            report['calibrated_pixels'] = int((calibrated & np.all(valid,axis=0) & thresholds['luma'][1][iy,ix]).sum())
            report['calibrated_pixel_frames'] = int((valid & (calibrated & thresholds['luma'][1][iy,ix])[None]).sum())
            masks = {}
            for key, seq in arrays.items():
                bound, known = thresholds[key]
                usable = calibrated & known[iy,ix]
                local_bound = bound[iy,ix]
                flags, count = transient(seq, valid & usable[None], local_bound)
                masks[key] = flags
                report['effects'][key] = {'pixel_events':count,'pixel_frames':int(flags.sum()),
                    'unique_area_pixels':int(flags.any(axis=0).sum()),
                    'threshold_method':'same-world static repeat maximum plus native storage quantum',
                    'threshold_range':([float(local_bound[usable].min()),float(local_bound[usable].max())]
                                       if usable.any() else None)}
            # Flashes UP (1-3-frame peaks bracketed by recovery, the same rule on -luma): "transient" scores dips
            # only, so a glint switching on for a frame was invisible to it (wf2-wf4). Reported, not in `masks`.
            bound, known = thresholds['luma']
            usable = calibrated & known[iy,ix]
            peaks, peak_count = transient(-arrays['luma'], valid & usable[None], bound[iy,ix])
            report['effects']['luma_peak'] = {'pixel_events':peak_count,'pixel_frames':int(peaks.sum()),
                                              'unique_area_pixels':int(peaks.any(axis=0).sum())}
            cover = masks_by_serial.get(str(report['serial']), masks_by_serial.get(''))
            if cover is not None:
                water = coverage_mask(cover, valid.shape[1:])
                cal = valid & (calibrated & thresholds['luma'][1][iy,ix] & water)[None]
                frames_in = int((masks['luma'] & water[None]).sum())
                peaks_in = int((peaks & water[None]).sum())
                report['water_mask'] = {'pixels':int(water.sum()), 'calibrated_pixel_frames':int(cal.sum()),
                                        'luma_pixel_frames':frames_in,
                                        'luma_rate':frames_in/int(cal.sum()) if cal.any() else None,
                                        'peak_pixel_frames':peaks_in,
                                        'peak_rate':peaks_in/int(cal.sum()) if cal.any() else None}
            report['attribution_pixel_frames'] = {
                key:int((masks['luma'] & masks[key]).sum()) for key in ('sun_mask','local_mask','gi_luma')}
            flagged = np.any(np.stack(list(masks.values())),axis=0)
            selected = np.flatnonzero(flagged.any(axis=(1,2)))
            report['contact_sheets'] = []
            for chunk in range(0,len(selected),4):
                chosen = selected[chunk:chunk+4]
                sheet = Image.new('RGB',(640*len(chosen),390),'#202020')
                for column,index in enumerate(chosen):
                    rgb = np.clip(frames[ids[index]]['final'][...,:3],0,1)
                    im = Image.fromarray((rgb*255).astype('u1')).resize((640,360))
                    anchor = frames[ids[7]]
                    iy,ix,_,_,_ = reproject(anchor['depth'][...,0],anchor['camera'],frames[ids[index]]['camera'])
                    mask = np.zeros_like(flagged[index])
                    mask[iy[flagged[index]],ix[flagged[index]]] = True
                    # Outline every flagged region using the binary four-neighbour boundary.
                    inside = mask.copy()
                    inside[1:] &= mask[:-1]; inside[:-1] &= mask[1:]
                    inside[:,1:] &= mask[:,:-1]; inside[:,:-1] &= mask[:,1:]
                    edge = Image.fromarray(((mask & ~inside)*255).astype('u1')).resize((640,360),Image.Resampling.NEAREST)
                    im.paste((255,0,0),(0,0,640,360),edge)
                    sheet.paste(im,(column*640,30))
                    ImageDraw.Draw(sheet).text((column*640+8,8),f'frame {ids[index]}',fill='white')
                dest = args.out/f'burst-{report["serial"]}-strip-{chunk//4}.png'
                sheet.save(dest)
                report['contact_sheets'].append(str(dest.resolve()))
    result = {'gate':'INCOMPLETE', 'calibration_bursts':min(len(static),1),
              'independent_static_bursts':max(len(static)-1,0),
              'positive_control_status':positive_control_status(results),'bursts':results}
    (args.out/'report.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
