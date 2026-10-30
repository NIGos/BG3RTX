"""Scratch-only inherited cancellation tests; no game, real captures or GPU."""
import ast
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import json


SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'live_phases.py'


class CancellationTest(unittest.TestCase):
    def run_wait_case(self, started):
        with tempfile.TemporaryDirectory(prefix='bg3-phase-cancel-') as directory:
            root = Path(directory)
            (root / 'scripts').mkdir()
            (root / 'captures').mkdir()
            script = root / 'scripts' / SCRIPT.name
            shutil.copyfile(SCRIPT, script)
            ini = root / 'captures' / 'auto.ini'
            original = b'preserve=1\n'
            ini.write_bytes(original)
            spec = root / 'spec.json'
            spec.write_text(json.dumps({'start': 'READY', 'timeout': 30,
                'phases': [{'at': 2 if started else 0, 'set': {'preserve': 'BAD'}}],
                'after_trace': [{'set': {'preserve': 'ALSO_BAD'}}]}))
            quit_file = root / 'quit'
            env = dict(os.environ, BG3RTX_QUEUE_QUIT_FILE=str(quit_file))
            output = root / 'stdout.txt'
            with output.open('w') as stream:
                process = subprocess.Popen([sys.executable, str(script), str(spec)],
                                           env=env, stdout=stream, stderr=stream)
                try:
                    # Both initial log wait and the clock-started phase wait must
                    # notice cancellation without needing another log event.
                    if started:
                        time.sleep(.1)
                        (root / 'bg3rtx.log').write_text('READY\n')
                        deadline = time.monotonic() + 5
                        while 'clock started' not in output.read_text():
                            self.assertIsNone(process.poll())
                            self.assertLess(time.monotonic(), deadline)
                            (root / "bg3rtx.log").touch()
                            time.sleep(.02)
                    else:
                        time.sleep(.1)
                    quit_file.write_text('revoked')
                    process.wait(timeout=2)
                    self.assertEqual(process.returncode, 0)
                    # A later trigger cannot resurrect a cancelled injector.
                    (root / 'bg3rtx.log').write_text('READY\n[trace] frame 1 written\n')
                    self.assertEqual(ini.read_bytes(), original)
                    self.assertFalse((root / 'captures' / 'phases.log').exists())
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()

    def test_initial_log_wait(self):
        self.run_wait_case(False)

    def test_started_phase_wait(self):
        self.run_wait_case(True)

    def test_apply_checks_before_read_or_write(self):
        # Run the actual apply body without top-level script effects. A cancelled
        # call must not even read an ini that has already been removed by cleanup.
        module = ast.parse(SCRIPT.read_text())
        apply = next(node for node in module.body
                     if isinstance(node, ast.FunctionDef) and node.name == 'apply')
        namespace = {'cancelled': lambda: True}
        exec(compile(ast.Module(body=[apply], type_ignores=[]), str(SCRIPT), 'exec'), namespace)
        namespace['apply']({'key': 'BAD'}, 'must not execute')

    def test_cancellation_during_ini_read(self):
        state = {'cancelled': False}

        class Ini:
            def read_text(self):
                state['cancelled'] = True
                return 'preserve=1'

            def write_text(self, text):
                raise AssertionError('wrote settings after cancellation')

        module = ast.parse(SCRIPT.read_text())
        apply = next(node for node in module.body
                     if isinstance(node, ast.FunctionDef) and node.name == 'apply')
        namespace = {'cancelled': lambda: state['cancelled'], 'ini': Ini()}
        exec(compile(ast.Module(body=[apply], type_ignores=[]), str(SCRIPT), 'exec'), namespace)
        namespace['apply']({'preserve': 'BAD'}, 'must not write')


if __name__ == '__main__':
    unittest.main()
