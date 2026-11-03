"""Synthetic box energy outcomes: unresolved recovery is a report, darkening fails."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.box_lights_art import energy_check as gate


def main():
    values = {f'shot-off-{c}': 1.0 for c in 'abcde'}
    values.update({f'shot-{kind}-{r}': 1.0 for kind in ('old', 'fixed') for r in 'ab'})
    # Exercise the exit status and JSON report, retaining the real energy evaluator.
    class Run:
        labels = list(values)
        def __init__(self, *args, **kwargs): pass
        def failed_drift(self): return False
        def scene_colour(self, label): return values[label], 0

    with tempfile.TemporaryDirectory() as tmp:
        log = Path(tmp) / 'run.txt'
        log.write_text('', encoding='utf-8')
        log.with_suffix('.runner.txt').write_text('', encoding='utf-8')
        with patch.object(gate, 'Run', Run), patch.object(gate.queue_gates, 'check', side_effect=lambda *args: []), \
             patch.object(sys, 'argv', ['energy_check.py', str(log)]):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                assert gate.main() == 0
            report = json.loads(out.getvalue())
            assert len(report['inconclusive']) == 2 and not report['failures']
            values['shot-fixed-a'] = 0.9
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                assert gate.main() == 1
            assert json.loads(out.getvalue())['failures'] == ['fixed-a: energy below OFF minus noise']
            values['shot-fixed-a'] = values['shot-fixed-b'] = 1.0
            values['shot-old-a'] = values['shot-old-b'] = 0.9
            with contextlib.redirect_stdout(io.StringIO()):
                assert gate.main() == 0
            assert not gate.evaluate(values)[4]
            values['shot-fixed-a'] = float('nan')
            try:
                gate.evaluate(values)
            except ValueError:
                pass
            else:
                raise AssertionError('nonfinite energy must fail')
    print('box art energy: inconclusive exits 0, darkening exits 1, resolved recovery exits 0')


if __name__ == '__main__':
    main()
