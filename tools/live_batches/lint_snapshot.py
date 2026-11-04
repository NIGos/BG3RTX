"""Explicit refresh: python -m tools.live_batches.lint_snapshot.

Only derived statistics are checked in. Runtime preflight still uses live_flow.corpus.
Historical replay needs original logs and is deliberately not part of this fixture.
"""
from pathlib import Path

from tools.live_batches import live_flow as flow

PATH = flow.ROOT / 'tools/live_batches/lint_corpus_snapshot.json'


def freeze(index, paths):
    flow.fps_windows.configure(index['entries'])
    flow.configure_trace_exports(index['entries'])
    flow.readiness_budget.configure(index['entries'])
    memo = {}
    paths = list(paths)
    for i, path in enumerate(paths):
        print(f'Freezing {i + 1}/{len(paths)}: {path.stem}', flush=True)
        job, plan, _ = flow.load_job(path)
        flow.regex_problems(job, index['entries'], memo)
        flow.duration_budget(job, plan)
        # Generators also query unfiltered transitions when sizing wait timeouts.
        flow.readiness_budget.timeouts(job, plan)
    return dict(version=1, valid_logs=len(index['entries']), saves=index['saves'],
        fps=[dict(save=save, state=list(sig), fps=v['fps'], windows=v['windows'])
             for (save, sig), v in sorted(flow.fps_windows.MODEL.items())],
        trace_export=dict(flow.TRACE_EXPORT),
        readiness=[dict(save=k[0], patterns=k[1], fresh=k[2], changed=k[3], first=k[4], estimate=v)
                   for k, v in flow.readiness_budget.CACHE.items()],
        matches={rx: [count, [Path(first[0]).name, 'frozen VALID match'] if first else None]
                 for rx, (count, first) in sorted(memo.items())})


def load(path=PATH):
    data = flow.read(path)
    if data['version'] != 1 or not data['valid_logs']:
        raise ValueError('unsupported or empty lint snapshot')
    flow.fps_windows.configure([])
    flow.fps_windows.MODEL.update({(v['save'], tuple(v['state'])):
        dict(fps=v['fps'], windows=v['windows']) for v in data['fps']})
    flow.TRACE_EXPORT.clear()
    flow.TRACE_EXPORT.update(data['trace_export'])
    flow.readiness_budget.configure([])
    flow.readiness_budget.CACHE.update({(v['save'], tuple(v['patterns']), tuple(v['fresh']),
        tuple(tuple(pair) for pair in v['changed']), v['first']): v['estimate'] for v in data['readiness']})
    flow.readiness_budget.FROZEN = True
    return data


def main():
    from tools.live_batches.regenerate_active import active_paths
    index = flow.corpus(flow.default_results(), flow.ROOT / 'captures/.cache/live_flow_corpus.json', newest=100000)
    if not index['entries']:
        raise ValueError('cannot freeze an empty VALID corpus')
    flow.write(PATH, freeze(index, active_paths()))
    print(f'Frozen {len(index["entries"])} VALID logs into {PATH.name}; no logs retained', flush=True)


if __name__ == '__main__':
    main()
