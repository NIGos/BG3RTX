"""Read-only scope and gate wiring regressions; no Git mutations or game."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.merge_scope import classify, changed_paths, tooling_checks, TOOL_TESTS, tooling_tests, shader_test_regex


class MergeScopeTests(unittest.TestCase):
    def test_scopes(self):
        cases = {
            'docs': ['docs/TRACK_X.md', 'README.md', 'docs/progress/fix.md'],
            'tooling': ['tools/live_batches/x/jobs/a.json', 'scripts/test-queue.ps1',
                        'scripts/fold_progress.py', 'tests/live_flow_test.py'],
            'shaders': ['shaders/gi/a.hlsl', 'shaders/variants.txt', 'common/shader_variant.h'],
            'full': ['layer/a.cpp', 'core/a.cpp', 'common/settings.h', 'CMakeLists.txt',
                     'bg3rtx.ini', 'tools/shader_identity.py', 'unknown',
                     'scripts/qa/__main__.py', '../docs/x.md', '/docs/x.md'],
        }
        for expected, paths in cases.items():
            for path in paths:
                with self.subTest(path=path):
                    self.assertEqual(classify([path])['scope'], expected)
        self.assertEqual(classify([])['scope'], 'full')

    def test_mixed_scopes_and_order(self):
        for paths, expected in ((['docs/a.md', 'scripts/a.py'], 'tooling'),
                                (['scripts/a.py', 'shaders/a.hlsl'], 'shaders'),
                                (['shaders/a.hlsl', 'core/a.cpp'], 'full')):
            self.assertEqual(classify(paths)['scope'], expected)
            self.assertEqual(classify(paths[::-1])['scope'], expected)

    def test_mixed_shader_tooling_runs_both(self):
        for paths in (['scripts/a.py', 'shaders/a.hlsl'], ['shaders/a.hlsl', 'scripts/a.py']):
            self.assertTrue(classify(paths)['tooling'])
        self.assertFalse(classify(['shaders/a.hlsl'])['tooling'])
        source = (ROOT / 'scripts/merge-green.ps1').read_text()
        self.assertIn("$toolingChanged -and $scope -in @('shaders', 'full')", source)
        self.assertIn("Fail 'tooling checks failed'", source)

    @patch('scripts.merge_scope.subprocess.run')
    def test_diff_includes_deleted_rename_source_and_spaces(self, run):
        run.return_value.stdout = b'core/old.cpp\0docs/new name.md\0'
        paths = changed_paths(ROOT, 'abc123')
        self.assertEqual(classify(paths)['scope'], 'full')
        args = run.call_args.args[0]
        self.assertIn('--no-renames', args)
        self.assertIn('main...abc123', args)
        self.assertIn('-z', args)
        self.assertTrue(run.call_args.kwargs['check'])

    @patch('scripts.merge_scope.subprocess.run')
    def test_tooling_runs_build_free_suite(self, run):
        tooling_checks(ROOT, [])
        commands = [c.args[0] for c in run.call_args_list]
        self.assertEqual(len(commands), len(TOOL_TESTS) + 2)
        self.assertFalse(any('build.ps1' in ' '.join(c) or 'compile_shaders.py' in ' '.join(c)
                             for c in commands))
        self.assertTrue(all(c.kwargs['check'] for c in run.call_args_list))

    def test_safety_and_scope_wiring(self):
        source = (ROOT / 'scripts/merge-green.ps1').read_text()
        self.assertLess(source.index("Fail 'BG3 is running"), source.index('$scope ='))
        self.assertLess(source.index('merge-green scope:'), source.index('git merge --no-ff'))
        for safety in ('USER_PLAYING', 'MERGE_HEAD', 'Undo-OwnedMerge', 'NOT MERGED:',
                       'MERGED $Ref onto', 'test-tree.ps1'):
            self.assertIn(safety, source)
        self.assertIn("if ($scope -eq 'full')", source)
        self.assertIn("$syncArgs += '-SkipBuild'", source)
        self.assertIn("Fail \"$scope checks failed\"", source)

    def test_source_consumers(self):
        import re
        regex = shader_test_regex(ROOT)
        for name in ('shader_variant_test', 'variant_matrix_test', 'host_atomics_lint_test',
                     'shader_identity_test', 'shader_cache_test', 'water_surface_shader_test',
                     'native_hit_gi_bindings_test', 'native_hit_rf_bindings_test'):
            self.assertRegex(name, regex)
        self.assertIsNone(re.fullmatch(regex, 'queue_gates_test'))
        self.assertIn('coverage_handling', tooling_tests(ROOT, ['scripts/qa_coverage.py',
                                                              'tests/coverage_handling_test.py']))
        self.assertNotIn('water_surface_shader', tooling_tests(ROOT, ['tests/water_surface_shader_test.py']))

    @patch('scripts.merge_scope.subprocess.run', side_effect=RuntimeError('gate failed'))
    def test_gate_failure_propagates(self, run):
        with self.assertRaisesRegex(RuntimeError, 'gate failed'):
            tooling_checks(ROOT, [])

    def test_changed_plan_lints_its_job_and_rejects_failure(self):
        from tools.live_batches import live_flow as flow, lint_snapshot
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            batch = root / 'tools/live_batches/example'
            (batch / 'plans').mkdir(parents=True)
            (batch / 'jobs').mkdir()
            plan = batch / 'plans/a.json'
            plan.write_text('[]')
            job = batch / 'jobs/a.json'
            job.write_text('{"args":["-MovePlan","tools/live_batches/example/plans/a.json"]}')
            with patch.object(lint_snapshot, 'load', return_value={'matches': {}}), \
                    patch.object(flow, 'load_job', return_value=({'expect': [], 'forbid': []}, [], plan)), \
                    patch.object(flow, 'lint', return_value={'problems': ['bad window'], 'duration': {}}) as lint, \
                    patch.object(flow, 'launch_problems', return_value=[]):
                with self.assertRaisesRegex(ValueError, 'bad window'):
                    tooling_checks(root, [plan.relative_to(root).as_posix()])
                lint.assert_called_once()


if __name__ == '__main__':
    unittest.main()
