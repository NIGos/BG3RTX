"""Shared reset-aligned A/B receipts; complete_period_lines returns text, not pairs."""
import re
import statistics
from tools.live_batches.edge_flicker.analyze import burst as edge_burst, resize_mask
from scripts import queue_gates as gates


def gpu_samples(picked, window, pattern):
    return [float(m[1]) for line in gates.complete_period_lines(picked, window)
            if (m := re.search(pattern, line))]


def summary(costs, floats, labels, off_labels, on_labels):
    """Report each window and signed deltas against measured OFF repeat spread."""
    if not isinstance(next(iter(costs.values())), dict):
        costs = {label: {'origin': values} for label, values in costs.items()}
    gpu = {}
    for name in next(iter(costs.values())):
        med = {label: statistics.median(costs[label][name]) if costs[label][name] else None for label in labels}
        off = [med[label] for label in off_labels]
        if any(v is None for v in off):
            gpu[name] = dict(window_ms=med, available=False)
            continue
        mean = statistics.mean(off)
        gpu[name] = dict(window_ms=med, off_mean_ms=mean, off_repeat_spread_ms=max(off)-min(off),
                         on_delta_ms={label: med[label]-mean if med[label] is not None else None for label in on_labels})
    energy = {}
    for i, channel in enumerate(('r', 'g', 'b', 'luminance')):
        values = {label: floats[j]['mean_rgb'][i] if i < 3 else floats[j]['luminance'] for j, label in enumerate(labels)}
        off = [values[label] for label in off_labels]
        mean, spread = statistics.mean(off), max(off)-min(off)
        energy[channel] = dict(window_mean=values, off_mean=mean, off_repeat_spread=spread,
            on={label: dict(delta=values[label]-mean, delta_vs_off_spread=(values[label]-mean)/spread if spread else None,
                            within_off_spread=abs(values[label]-mean)<=spread) for label in on_labels})
    return dict(gpu=gpu, energy=energy, energy_windows=dict(zip(labels, floats)))


def print_verdict(result):
    print('VERDICT '+result['status']+': '+'; '.join(result['errors']))
    for name, row in result.get('receipt', {}).get('gpu', {}).items():
        print(f"GPU {name}: windows={row['window_ms']} OFF spread={row.get('off_repeat_spread_ms')} ms ON delta={row.get('on_delta_ms')} ms")
    row = result.get('receipt', {}).get('energy', {}).get('luminance')
    if row:
        print(f"ENERGY pre-post float luminance: windows={row['window_mean']} OFF spread={row['off_repeat_spread']} ON={row['on']}")
    for name, row in result.get('qa', {}).items():
        qa = row["qa"]
        metrics = qa.get("metrics", {})
        print(f"QA flicker {name}: pass={qa['pass']} score={qa.get('score')} threshold={qa.get('threshold')} excess_area={metrics.get('excess_area')} excess_threshold={metrics.get('excess_threshold')} inconclusive={metrics.get('inconclusive')}")


def burst(directory):
    # Detailed post logging adds two diagnostics to the same valid edge transport.
    return edge_burst(directory, extras=('rr_post_ac', 'rr_post_eb'))


def moving_mask(off, on, shape):
    from scripts.qa import detectors as det
    import numpy as np
    native = off[0].shape[:2]
    if any(frame.shape[:2] != native or not np.isfinite(frame).all() for frame in off + on):
        raise ValueError("nonfinite or mismatched QA albedo frames")
    mask = det.animated(off + on, [], native)
    return resize_mask(mask, shape) if mask.shape != shape else mask
