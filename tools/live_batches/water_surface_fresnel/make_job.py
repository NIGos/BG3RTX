"""Refresh the canonical Fresnel job with shared FPS/cadence bounds. Never queues."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.live_batches import live_flow as flow
import re

HERE = Path(__file__).resolve().parent
COMPOSE = r'compose \((\d+) frames,.*?rays (\d+)/frame.*?aborted (\d+), invalid (\d+)'


def abort_bound(entries):
    """Maximum VALID compose abort share; half-ray rounding lowers denominator."""
    samples = []
    for entry in entries:
        for line in entry['lines']:
            m = re.search(COMPOSE, line)
            if m and int(m[1]) > 0 and int(m[2]) > 0 and int(m[4]) == 0:
                frames, rays, aborted, _ = map(int, m.groups())
                samples.append((aborted / (frames * (rays - .5)), entry['path'], frames, rays, aborted))
    if not samples:
        raise ValueError('no VALID compose abort-share observations')
    bound, log, frames, rays, aborted = max(samples)
    return dict(max=bound, samples=len(samples), log=Path(log).name,
                frames=frames, rays_per_frame=rays, aborted=aborted)


def main():
    index = flow.corpus(flow.default_results(), flow.ROOT / 'captures/.cache/live_flow_corpus.json', newest=100000)
    bound = abort_bound(index['entries'])
    path = HERE / 'jobs/wsf1-s8-interface-column.json'
    job = flow.read(path)
    _, plan, plan_path = flow.load_job(path)
    # Existing preview output now includes the explicit post_core water pair.
    # Keep it out of capture-free cost windows and restore it at the end.
    job['set'] += ',dump_preview_f32=0' if 'dump_preview_f32=' not in job['set'] else ''
    for phase in plan:
        state = '1' if phase['label'].startswith('partition-static-') or phase['label'] == 'prefetch-interface-on' else '0'
        phase['set'] = ','.join(s for s in phase.get('set', '').split(',')
                                if s and not s.startswith('dump_preview_f32=')) + ',dump_preview_f32=' + state
    flow.write(plan_path, plan)
    job['expect_every'] = [g for g in job['expect_every'] if g['name'] != 'surface FS GPU ms']
    job['expect_every'].append(dict(name='surface FS GPU ms',
        select=r'\[water\] surface FS GPU', match=r'surface FS GPU [0-9.]+ ms median, p90 [0-9.]+ \([1-9]\d* timed frames, [1-9]\d* passes, [1-9]\d* draws, dropped 0;',
        min=1, period_frames=120,
        windows=['partition-static-1', 'partition-static-3', 'cost-turn-1', 'cost-turn-3']))
    gate = next(g for g in job['expect_every'] if g['name'] == 'compose tiers')
    gate['match'] = r'screen hit \(composed\) [0-9.]+%, hidden [0-9.]+%.*aborted \d+, invalid 0\b'
    # Lookaheads place numerator first without duplicating producer parsing.
    gate['share'] = dict(pattern=r'compose \((?=.*?aborted (\d+), invalid 0\b)(\d+) frames,.*?rays (\d+)/frame',
                         offsets=[0, -.5], max=bound['max'])
    flow.write(path, job)
    predicted = flow.read(HERE / 'predicted.json')
    predicted['float_observable'] = 'dump_preview_f32=1 warmed without captures, then enabled for eight shots in four static windows; dump_raw=0. Read the existing water-capture-fix explicit post_core same-image/same-pass pair in frame.json, then its _p.f32 preview (1280 x 533 at S8 2560 x 1067). Never use pre-water 5c3fdabc or PNG radiance. Half-open preview ROIs: pool rim [490,75,825,285] (70,350 pixels), open water [0,75,245,340] (64,925 pixels); source texel=floor(preview coordinate * render size / preview size). Post gate rejects missing, truncated, unpaired, nonfinite or out-of-bounds data and reports both regional luminance/exposure values for each shot.'
    predicted['gpu'] = 'Require surface FS GPU ms in every ON static/turn window, separately from compose/copy/dispatch. Timer covers patched candidate water draws including their vertex/raster work; no OFF timer, so ON FS absolute ms plus compose is reported, not an inferred incremental FS delta. Static compose delta prediction +0.0255 ms from VALID wsf1; FS cost remains unknown until measured. No builds during live cost windows. Timer samples first submission of each recorded pass; query slots remain reserved until command reset. Re-executed unchanged command buffers are not sampled again. Capacity 64 passes x 256 draw pairs; every ON interval requires nonzero frames/passes/draws and dropped 0. Trailing empty fence submission is outside the timed region.'
    predicted['budget'] = 'Eight static traces only; bounded 1280 x 533 float previews, 10,915,840 bytes each. At S8 one pre-water plus one paired post-water preview/shot adds 174,653,440 bytes (166.56 MiB) to about 490 MB existing captures: about 665 MB total, below 2 GB. No raw float attachments (would add 5.86 GB). E/Q cost windows are capture-free; shared generator derives durations.'
    predicted['qualification_prediction'] = '8/8 paired regional float shots, 2 ROIs each; finite/nonnegative RGB and valid exposure, invalid 0, partition <= 9.5367431640625e-7, clean ownership > 0 in every ON interval. Full-frame ON change expected near prior +2.257% to +3.486%, descriptive only. Regional change and forward FS absolute ms have no measured prior: report them, do not invent thresholds or claim image/reference qualification. Open-water fallback should agree within this run OFF-repeat variation; rim interface change must be reviewed against reflected-column persistence, not equal-mean gating.'
    predicted['compose_abort_share'] = dict(bound, derivation='Maximum aborted / (frames * (rays_per_frame - 0.5)) over VALID compose intervals with invalid 0; lower denominator accounts for rounded rays/frame. No added tolerance. Every selected interval must comply; invalid remains exactly zero.')
    flow.write(HERE / 'predicted.json', predicted)
    flow.tighten_files(HERE)


if __name__ == "__main__":
    main()
