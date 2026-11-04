"""Expected readiness delays from VALID same-save, same-setting transitions."""
from statistics import median
import re

from scripts.queue_gates import stamped_lines, windows, _apply

ENTRIES = []
CACHE = {}


def configure(entries):
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
            for pattern in patterns:
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
