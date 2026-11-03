"""Refresh canonical shot timing without weakening the per-window gates. Never queues."""
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent


def main():
    path = BASE / 'plans' / 'wo1_s8.json'
    plan = json.loads(path.read_text())
    for phase in plan:
        if phase['label'].startswith('shot-'):
            # Two trace stalls (4 s each) + apply (1 s) + two 120-frame
            # periods (8 s at 30 FPS), strictly before the exclusive endpoint.
            phase.update(seconds=18, motion_start=18, motion_end=18, trace_at=[4, 9])
    path.write_text(json.dumps(plan, indent=2) + '\n')
    path = BASE / 'jobs' / 'wo1-s8-hit-shade.json'
    job = json.loads(path.read_text())
    job['live_flow']['trace_stall_s'] = 4
    job['estimated_phase_seconds'] = sum(p['seconds'] for p in plan)
    # Preserve the original loading allowance after extending every shot.
    job['timeout'] = max(job['timeout'], job['estimated_phase_seconds'] + 240)
    path.write_text(json.dumps(job, indent=2) + '\n')


if __name__ == '__main__':
    main()
