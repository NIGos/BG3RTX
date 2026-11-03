"""Refresh the canonical job/plan JSON with serialized trace timing. Never queues."""
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent


def main():
    # dump.cpp finishes one trace before the runner can issue the next. The
    # 18:56 run took about 3 seconds per trace; reserve 4 seconds, plus gaps.
    for scene, name in [('s8', 'go2-s8-medium'), ('camp', 'go2-camp-high')]:
        path = BASE / 'plans' / f'{scene}.json'
        plan = json.loads(path.read_text())
        for phase in plan:
            if phase['label'].startswith('shot-'):
                phase.update(seconds=17, trace_at=[1, 6, 11])
        path.write_text(json.dumps(plan, indent=2) + '\n')
        path = BASE / 'jobs' / f'{name}.json'
        job = json.loads(path.read_text())
        job['live_flow']['trace_stall_s'] = 4
        job['estimated_phase_seconds'] = sum(p['seconds'] for p in plan)
        path.write_text(json.dumps(job, indent=2) + '\n')


if __name__ == '__main__':
    main()
    from tools.live_batches.live_flow import tighten_files
    tighten_files(Path(__file__).resolve().parent)
