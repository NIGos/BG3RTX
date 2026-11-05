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
        trace_capture_stall=dict(flow.TRACE_STALL),
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
    flow.TRACE_STALL.clear()
    flow.TRACE_STALL.update(data.get('trace_capture_stall', {}))
    flow.readiness_budget.configure([])
    flow.readiness_budget.CACHE.update({(v['save'], tuple(v['patterns']), tuple(v['fresh']),
        tuple(tuple(pair) for pair in v['changed']), v['first']): v['estimate'] for v in data['readiness']})
    flow.readiness_budget.FROZEN = True
    return data


def extend_readiness(paths, path=PATH):
    """Add only previously unseen queries; preserve every frozen receipt.

    New transitions without a VALID receipt remain explicitly unproven.
    This never refreshes regex matches, FPS models or existing readiness data.
    """
    data = load(path)
    flow.readiness_budget.FROZEN = False
    before = set(flow.readiness_budget.CACHE)
    for target in paths:
        job, plan, _ = flow.load_job(target)
        flow.duration_budget(job, plan)
        flow.readiness_budget.timeouts(job, plan)
    for key, estimate in flow.readiness_budget.CACHE.items():
        if key not in before:
            data['readiness'].append(dict(save=key[0], patterns=key[1], fresh=key[2],
                changed=key[3], first=key[4], estimate=estimate))
    flow.write(path, data)
    return len(flow.readiness_budget.CACHE) - len(before)


def main():
    index = flow.corpus(flow.default_results(), flow.ROOT / 'captures/.cache/live_flow_corpus.json', newest=100000)
    if not index['entries']:
        raise ValueError('cannot freeze an empty VALID corpus')
    # merge-green also lints changed qualification and historical jobs outside
    # the active inventory. Freeze every checked-in job's queries together.
    flow.write(PATH, freeze(index, flow.job_files(flow.ROOT / 'tools/live_batches')))
    print(f'Frozen {len(index["entries"])} VALID logs into {PATH.name}; no logs retained', flush=True)


if __name__ == '__main__':
    main()
