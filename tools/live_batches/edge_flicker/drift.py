"""Integer-only drift compensation and static RR decision evidence. No game control."""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from tools.live_batches.dialogue_shadows.pair_registration import registration


def translate(frame, shift):
    """Translate without interpolation or wrap; NaN marks unavailable support."""
    dy, dx = shift
    h, w = frame.shape[:2]
    out = np.full(frame.shape, np.nan, dtype='f4')
    y0, y1 = max(0, -dy), min(h, h-dy)
    x0, x1 = max(0, -dx), min(w, w-dx)
    if y1 > y0 and x1 > x0:
        out[y0+dy:y1+dy, x0+dx:x1+dx] = frame[y0:y1, x0:x1]
    return out


def roi(shape, box, extent=(1920, 800)):
    h, w = shape[:2]
    x0, y0, x1, y1 = box
    m = np.zeros((h, w), bool)
    m[round(y0*h/extent[1]):round(y1*h/extent[1]),
      round(x0*w/extent[0]):round(x1*w/extent[0])] = True
    if not m.any():
        raise ValueError('empty ROI')
    return m


def structure(frame, top):
    grey = frame.mean(-1) if frame.ndim == 3 else frame
    dy, dx = np.gradient(grey[:top].astype('f8'))
    return np.hypot(dx, dy)


def register_window(anchor, frames, config=None):
    from tools.live_batches.edge_flicker.analyze import DIALOGUE_ROIS
    config = config or {}
    extent = config.get('roi_extent', (1920, 800))
    top = round(frames[0].shape[0]*config.get('subtitle_top', 590)/extent[1])
    control = roi(frames[0].shape, config.get('rois', DIALOGUE_ROIS)['rigid_control'], extent)[:top]
    anchor_control = control
    local = structure(frames[0], top)
    base = structure(anchor, top)
    offset = registration(base, local)
    control = np.nan_to_num(translate(anchor_control.astype('f4'), offset['shift_px'])) > 0
    regs, errors = [], []
    for frame in frames:
        r = registration(local, structure(frame, top))
        aligned = translate(structure(frame, top), tuple(-v for v in r['shift_px']))
        support = control & np.isfinite(aligned)
        if not support.any():
            raise ValueError('no overlapping rigid control')
        # Normalize gradient magnitude: grade gain or polarity is not camera motion.
        a, b = local[support], aligned[support]
        gain = float(a.sum()/b.sum()) if a.sum() > 0 and b.sum() > 0 else 1.0
        error = float(np.abs(a-b*gain).mean())
        r['rigid_structure_residual'] = error
        regs.append(r)
        errors.append(error)
    # Two central-difference gradient components: worst-case conversion error sqrt(2)/255.
    # Add measured repeated-frame noise (six MAD).
    # A burst with a majority of mismatches cannot inflate its own acceptance band.
    floor = np.sqrt(2)/255
    med = float(np.median(errors))
    mad = float(np.median(np.abs(np.asarray(errors)-med)))
    bound = float(floor + 6*mad)
    for r in regs:
        r['valid'] = bool(r['rigid_structure_residual'] <= bound)
    runs, current = [], []
    for i, r in enumerate(regs):
        if r['valid']:
            current.append(i)
        else:
            runs.append(current)
            current = []
    runs.append(current)
    keep = max(runs, key=len)
    if len(keep) < 4:
        raise ValueError('high rigid control residual: fewer than four consecutive aligned frames')
    # Cross-window rigid consistency, allowing different colour grades.
    aligned = translate(local, tuple(-v for v in offset['shift_px']))
    support = anchor_control & np.isfinite(aligned)
    a, b = base[support], aligned[support]
    gain = float(a.sum()/b.sum()) if a.sum() > 0 and b.sum() > 0 else 1.0
    cross = float(np.abs(a-b*gain).mean()) if support.any() else float('inf')
    if cross > bound:
        raise ValueError('high cross-window rigid control residual')
    return keep, dict(frames=regs, included=keep,
        excluded=[i for i in range(len(frames)) if i not in keep],
        shift_px=offset['shift_px'], anchor_registration=offset,
        rigid_residual_bound=bound, cross_window_rigid_residual=cross, subtitle_top=top)


def appearance(frames):
    values = np.stack(frames)
    finite = np.isfinite(values).all(-1)
    rgb = values[finite]
    return dict(mean_rgb=rgb.mean(0).tolist(), mean_code=float(rgb.mean()),
        histogram_rgb=[np.histogram(rgb[:, c], bins=64, range=(0, 1))[0].tolist() for c in range(3)],
        histogram_edges=np.linspace(0, 1, 65).tolist(),
        highlight_percentiles=np.percentile(rgb, [95, 99, 99.9], axis=0).tolist(),
        clipping_percent=float((rgb >= 1).any(-1).mean()*100))


def details(frames, mask):
    y = np.stack([f.mean(-1) for f in frames])
    support = mask & np.isfinite(y).all(0)
    support[[0, -1]] = False
    support[:, [0, -1]] = False
    for axis in (0, 1):
        support &= np.roll(np.isfinite(y).all(0), 1, axis) & np.roll(np.isfinite(y).all(0), -1, axis)
    if not support.any():
        raise ValueError('no detail support')
    lap = sum(np.roll(y, d, axis) for axis in (1, 2) for d in (-1, 1))-4*y
    grad = (np.abs(np.roll(y, 1, 1)-y)+np.abs(np.roll(y, 1, 2)-y))/2
    return dict(gradient=float(grad[:, support].mean()), high_frequency=float(np.abs(lap[:, support]).mean()))


def save_panels(paths, config, destination):
    from tools.live_batches.edge_flicker.analyze import presents
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    images, meta = [], []
    anchor = None
    for label, path in paths.items():
        presents(path['presented'])  # Validate the complete P16, including ordinal zero.
        source = path['presented']/'native.png'
        if not source.exists():
            raise ValueError('missing native ordinal-zero presented frame')
        with Image.open(source) as native:
            if list(native.size) != config['presented_extent']:
                raise ValueError('native presented image extent differs from swapchain')
            pixels = np.asarray(native.convert('RGB'), dtype='f4')/255
        anchor = pixels if anchor is None else anchor
        # Register at native resolution so odd-pixel offsets are never rounded
        # through the compact P16 grid. This does not interpolate crop pixels.
        _, reg = register_window(anchor, [pixels]*4, config)
        shift = reg['shift_px']
        mask = roi(pixels.shape, config['rois']['hair_box'], config['roi_extent'])
        yy, xx = np.where(mask)
        dy, dx = shift
        box = (int(xx.min()+dx), int(yy.min()+dy), int(xx.max()+dx+1), int(yy.max()+dy+1))
        if box[0] < 0 or box[1] < 0 or box[2] > pixels.shape[1] or box[3] > pixels.shape[0]:
            raise ValueError('hair crop outside full-resolution presented frame')
        image = Image.open(source).convert('RGB').crop(box)
        image.save(destination/(label+'-native.png'))
        images.append(image)
        compact = sorted(path['presented'].glob('p*.png'), key=lambda p: int(p.stem[1:]))[0]
        meta.append(dict(label=label, source=str(source), same_present_as=str(compact), ordinal=0, native_shift_px=shift, box=box))
    width, height = images[0].size
    panel = Image.new('RGB', (width*len(images), height+24))
    draw = ImageDraw.Draw(panel)
    for i, (image, row) in enumerate(zip(images, meta)):
        panel.paste(image, (i*width, 24))
        draw.text((i*width, 4), row['label'], fill='white')
    panel.save(destination/'hair-native-side-by-side.png')
    diffs = []
    for image in images[1:]:
        diff = np.abs(np.asarray(image, dtype='i2')-np.asarray(images[0], dtype='i2')).astype('u1')
        diffs.append(Image.fromarray(diff).resize((width*6, height*6), Image.Resampling.NEAREST))
    panel = Image.new('RGB', (width*6*len(diffs), height*6+24))
    draw = ImageDraw.Draw(panel)
    for i, image in enumerate(diffs):
        panel.paste(image, (i*width*6, 24))
        draw.text((i*width*6, 4), meta[i+1]['label']+' vs '+meta[0]['label']+' |diff| 6x', fill='white')
    panel.save(destination/'hair-secondary-diff-6x.png')
    return meta


def decision_report(paths, config, evidence, destination=None):
    from tools.live_batches.edge_flicker.analyze import burst, presents, aligned_frames, energy
    rows, anchors = {}, {}
    for label, path in paths.items():
        print('analyzing '+label, flush=True)
        row = {}
        for domain in ('rendered', 'presented'):
            raw = burst(path[domain])['codes'] if domain == 'rendered' else presents(path[domain])
            anchor = anchors.setdefault(domain, raw[0])
            keep, reg = register_window(anchor, raw, config)
            aligned = aligned_frames(raw, reg)
            shift = reg['shift_px']
            from tools.live_batches.edge_flicker.analyze import shifted_mask
            metrics = {}
            for name, box in config['rois'].items():
                mask = shifted_mask(roi(raw[0].shape, box, config['roi_extent']), shift)
                metrics[name] = dict(energy(aligned, mask), **details(aligned, mask))
            row[domain] = dict(registration=reg, rois=metrics, appearance=appearance(raw))
        rows[label] = row
    comparisons = {}
    for domain in ('rendered', 'presented'):
        comparisons[domain] = {}
        for state, names in [('post-early', ['base-a','base-b']), ('post-late', ['post-late-a','post-late-b']),
                             ('resolve-off', ['resolve-off-a','resolve-off-b']), ('resolve-on', ['resolve-on-a','resolve-on-b'])]:
            comparisons[domain][state] = {metric: [min(rows[n][domain]['rois']['hair_box'][metric] for n in names),
                max(rows[n][domain]['rois']['hair_box'][metric] for n in names)]
                for metric in ('residual_x1000','sparkle_percent','mean_code','gradient','high_frequency')}
    appearance_comparison = {}
    for domain in ('rendered', 'presented'):
        appearance_comparison[domain] = {}
        for label in ('post-late-a', 'post-late-b'):
            current = rows[label][domain]['appearance']
            comparisons_to_base = {}
            for baseline in ('base-a', 'base-b'):
                reference = rows[baseline][domain]['appearance']
                hist_a = np.asarray(current['histogram_rgb'], dtype='f8')
                hist_b = np.asarray(reference['histogram_rgb'], dtype='f8')
                hist_a /= hist_a.sum(1, keepdims=True)
                hist_b /= hist_b.sum(1, keepdims=True)
                comparisons_to_base[baseline] = dict(
                    mean_rgb_delta=(np.asarray(current['mean_rgb'])-reference['mean_rgb']).tolist(),
                    mean_code_delta=current['mean_code']-reference['mean_code'],
                    histogram_rgb_total_variation=(np.abs(hist_a-hist_b).sum(1)/2).tolist(),
                    highlight_percentile_delta=(np.asarray(current['highlight_percentiles'])-reference['highlight_percentiles']).tolist())
            appearance_comparison[domain][label] = comparisons_to_base
    crops = save_panels(paths, config, destination) if destination else []
    return dict(status='DECISION_EVIDENCE_ONLY', quality_pass=False, windows=rows,
        repeat_ranges=comparisons, appearance_comparison=appearance_comparison, rate_evidence=evidence, native_crops=crops,
        limits=['PQ appearance is not float energy. Read matching-stage dumps.json means and GPU logs.',
                'Ordinal zero per burst is matched by capture index, not animation or generated-frame identity.',
                'Static S8 framing and rigid/hair ROI content require visual verification.',
                'Integer alignment cannot recover subpixel drift; rigid residual reports its effect.'])
