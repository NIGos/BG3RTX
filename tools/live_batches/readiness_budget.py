"""Expected readiness delays from VALID same-save, same-setting transitions."""
from statistics import median
import re

from scripts.queue_gates import stamped_lines, windows, _apply
from tools.live_batches.pipeline_readiness import conditional

ENTRIES = []
CACHE = {}
FROZEN = False


def configure(entries):
    global FROZEN
    FROZEN = False
    ENTRIES.clear()
    CACHE.clear()
    for entry in entries:
        lines = stamped_lines('\n'.join(entry['lines']))
        wins = windows(entry.get('runner', ''), [], lines)
        previous = {}
        for win in wins:
            state = dict(previous)
            _apply(state, win.get('set') or '')
            win['changed'] = {k: v for k, v in state.items() if previous.get(k) != v}
            previous = state
        ENTRIES.append((entry.get('save'), lines, wins))


def same_value(actual, wanted):
    try:
        return float(actual) == float(wanted)
    except ValueError:
        return str(actual) == str(wanted)


def estimate(job, phase, changed, first=False):
    patterns = phase.get('wait_log', [])
    patterns = [patterns] if isinstance(patterns, str) else patterns
    if not patterns:
        return dict(seconds=0, samples=0, source='no wait')
    fresh = phase.get('wait_log_fresh', [])
    fresh = [fresh] if isinstance(fresh, str) else fresh
    key = (job.get('save'), tuple(patterns), tuple(fresh), tuple(sorted(changed.items())), first)
    if key in CACHE:
        return CACHE[key]
    if FROZEN:
        raise ValueError('readiness query absent from frozen corpus; refresh lint snapshot')
    samples = []
    for save, lines, wins in ENTRIES:
        if save != job.get('save'):
            continue
        for i, win in enumerate(wins):
            if first and i != 0:
                continue
            if not first and (not changed or any(win['changed'].get(k) != v for k, v in changed.items())):
                continue
            delays = []
            if fresh and any(conditional(p) for p in patterns):
                ack = next((t for t, line in lines if win['start'] <= t < win['end']
                            and '[cfg] keys:' in line and (not changed or any(
                                re.search(r'(?:^|\s)'+re.escape(k)+r'=', line) for k in changed)) and all(
                                not re.search(r'(?:^|\s)'+re.escape(k)+r'=', line)
                                or same_value(re.search(r'(?:^|\s)'+re.escape(k)+r'=([^\s]+)', line)[1], v)
                                for k, v in changed.items())), None)
                if ack is None:
                    continue
                pending = set()
                historical = {}
                affected = [p for p in patterns if conditional(p) and not re.search(
                    'shaders|watch|surface Fresnel', p) and 'gi/[^ ]+' not in p]
                explicit_only = not any(conditional(p) for p in patterns)
                ready = None
                for t, line in lines:
                    if t >= win['end']:
                        break
                    start = re.search(r'gi: (\S+) building in the background', line)
                    done = re.search(r'gi: (\S+) generation ', line)
                    water_start = re.search(r'(\S+\.hlsl[^ ]*) (\w+): building in the background', line)
                    water_done = re.search(r'\[water\] (\S+\.hlsl[^ ]*) (\w+): built', line)
                    ident = start[1] if start else (water_start[1], water_start[2]) if water_start else None
                    completed = done[1] if done else (water_done[1], water_done[2]) if water_done else None
                    if ident:
                        witness = ('gi: '+ident+' generation ') if isinstance(ident, str) else (
                            '[water] '+ident[0]+' '+ident[1]+': built')
                        families = [p for p in affected if re.search(p, witness, re.I)]
                        if t >= ack or families or explicit_only:
                            pending.add(ident)
                            if t < ack:
                                historical[ident] = families
                    elif completed:
                        pending.discard(completed)
                        historical.pop(completed, None)
                        for old, families in list(historical.items()):
                            if any(re.search(p, line, re.I) for p in families):
                                pending.discard(old)
                                historical.pop(old)
                    if t >= ack and not pending:
                        ready = t
                        break
                if ready is None:
                    continue
                delays.append(max(0, ready-win['start']))
            for pattern in patterns:
                if fresh and conditional(pattern):
                    continue
                found = next((t for t, line in lines if t < win['end']
                              and (pattern not in fresh or t >= win.get('settings_applied', win['applied']))
                              and re.search(pattern, line, re.I)), None)
                if found is None:
                    break
                delays.append(max(0, found-win['start']))
            else:
                samples.append(max(delays, default=0))
    result = dict(seconds=round(median(samples), 3) if samples else 0,
                  samples=len(samples), source='VALID same-save/key median' if samples else 'unproven; no VALID same-save/key observation')
    CACHE[key] = result
    return result


def timeouts(job, plan):
    """Use learned medians plus the declared observation margin; retain unproven bounds."""
    from scripts.queue_gates import phase_states
    states = phase_states(job, plan)
    result = []
    for i, (phase, state) in enumerate(zip(plan, states)):
        changed = {k: v for k, v in state.items() if i and states[i-1].get(k) != v}
        learned = estimate(job, phase, changed, first=i == 0)
        limit = float(phase.get('wait_log_timeout', 0))
        patterns = phase.get('wait_log', [])
        patterns = [patterns] if isinstance(patterns, str) else patterns
        if learned['samples'] and phase.get('wait_log_fresh') and any(conditional(p) for p in patterns):
            margin = float(job.get('live_flow', {}).get('settling_margin_s', 1))
            latency = float(job.get('live_flow', {}).get('settings_latency_s', 1))
            limit = min(limit, max(latency, learned['seconds']) + margin)
        result.append(limit)
    return result


if __name__ == '__main__':
    import argparse
    import json
    from pathlib import Path
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--context', required=True)
    ap.add_argument('--plan', required=True)
    ap.add_argument('--cache', required=True)
    args = ap.parse_args()
    cache = Path(args.cache)
    configure(json.loads(cache.read_text(encoding='utf-8-sig')).get('entries', []) if cache.exists() else [])
    job = json.loads(Path(args.context).read_text(encoding='utf-8-sig'))['job']
    plan = json.loads(Path(args.plan).read_text(encoding='utf-8-sig'))
    print(json.dumps(timeouts(job, plan)))
