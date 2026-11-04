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
    gate = next(g for g in job['expect_every'] if g['name'] == 'compose tiers')
    gate['match'] = r'screen hit \(composed\) [0-9.]+%, hidden [0-9.]+%.*aborted \d+, invalid 0\b'
    # Lookaheads place numerator first without duplicating producer parsing.
    gate['share'] = dict(pattern=r'compose \((?=.*?aborted (\d+), invalid 0\b)(\d+) frames,.*?rays (\d+)/frame',
                         offsets=[0, -.5], max=bound['max'])
    flow.write(path, job)
    predicted = flow.read(HERE / 'predicted.json')
    predicted['compose_abort_share'] = dict(bound, derivation='Maximum aborted / (frames * (rays_per_frame - 0.5)) over VALID compose intervals with invalid 0; lower denominator accounts for rounded rays/frame. No added tolerance. Every selected interval must comply; invalid remains exactly zero.')
    flow.write(HERE / 'predicted.json', predicted)
    flow.tighten_files(HERE)


if __name__ == "__main__":
    main()
