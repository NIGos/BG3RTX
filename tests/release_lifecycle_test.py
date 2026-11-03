"""C1 lifecycle fixtures: temporary files only; no executable payload is launched."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PS = shutil.which('powershell.exe') if os.name == 'nt' else None


@unittest.skipUnless(PS, 'Windows PowerShell 5.1 required')
class LifecycleTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bg3rtx-c1-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dst = self.root / 'install spazio è 日'
        self.payload = self.root / 'payload'
        self.local = self.root / 'local'
        self.payload.mkdir()
        self.files = {'bin/BG3RTX.exe': b'fixture launcher, never executed', 'retired.txt': b'v1'}
        self.write_payload()
        self.script = self.root / 'lifecycle.ps1'
        shutil.copyfile(ROOT / 'scripts/release/lifecycle.ps1', self.script)

    def write_payload(self):
        entries = []
        for name, data in self.files.items():
            p = self.payload / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            entries.append({'path': name, 'sha256': hashlib.sha256(data).hexdigest()})
        self.manifest = {'schema': 1, 'files': entries}
        self.save_manifest()

    def save_manifest(self):
        (self.payload / 'payload.sha256.json').write_text(json.dumps(self.manifest), encoding='utf-8')

    def run_action(self, action, *args, ok=True, process='', script=None):
        quote = lambda s: "'" + str(s).replace("'", "''") + "'"
        # Shadow only process discovery in this child; never launch a fake or real game.
        mock = ("function Get-Process { [CmdletBinding()] param([string[]]$Name,[int]$Id) "
                "if ($Id) { Microsoft.PowerShell.Management\\Get-Process -Id $Id } "
                + ("elseif ($Name -contains '" + process + "') { [pscustomobject]@{Name='" + process + "'} } " if process else '') + "}; ")
        command = mock + '& ' + quote(script or self.script) + ' -Action ' + action + ' -Destination ' + quote(self.dst)
        if action == 'Install':
            command += ' -Payload ' + quote(self.payload)
        command += ' ' + ' '.join(str(a) if str(a).startswith('-') else quote(a) for a in args)
        result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', command],
                                env=dict(os.environ, LOCALAPPDATA=str(self.local)), cwd=self.root,
                                capture_output=True, text=True, timeout=45)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def snapshot(self, path=None):
        path = path or self.dst
        return {str(p.relative_to(path)): p.read_bytes() for p in path.rglob('*') if p.is_file()}

    def test_clean_update_restore_preserves_settings_reports_and_unrelated(self):
        self.run_action('Install')
        for name in ('bg3rtx_user.ini', 'bg3rtx_launcher.ini', 'reports/mine.zip', 'unrelated.txt'):
            p = self.dst / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'custom=unknown')
        before = self.snapshot()
        self.files = {'bin/BG3RTX.exe': b'v2', 'new.txt': b'new'}
        self.write_payload()
        self.run_action('Install')
        self.assertFalse((self.dst / 'retired.txt').exists())
        self.assertEqual((self.dst / 'bg3rtx_launcher.ini').read_bytes(), b'custom=unknown')
        self.run_action('Restore')
        self.assertEqual(self.snapshot(), before)
        self.run_action('Restore')  # Restore is itself transactional; redo keeps current until success.
        self.assertEqual((self.dst / 'bin/BG3RTX.exe').read_bytes(), b'v2')

    def test_replacement_failure_rolls_back_complete_set(self):
        self.run_action('Install')
        before = self.snapshot()
        self.files = {'bin/BG3RTX.exe': b'new', 'late.txt': b'later'}
        self.write_payload()
        self.run_action('Install', '-FailAfter', '1', ok=False)
        self.assertEqual(self.snapshot(), before)
        self.run_action('Install')

    def test_fresh_failure_and_incomplete_update(self):
        self.run_action('Install', '-FailAfter', '1', ok=False)
        self.assertEqual(self.snapshot(), {})
        self.run_action('Install')
        before = self.snapshot()
        (self.payload / 'retired.txt').unlink()
        self.run_action('Install', ok=False)
        self.assertEqual(self.snapshot(), before)
        self.run_action('Restore', ok=False)  # A clean install has no previous installed version.

    def test_interruption_blocks_launch_lifecycle_until_recovery(self):
        self.run_action('Install')
        before = self.snapshot()
        self.files['bin/BG3RTX.exe'] = b'new'
        self.write_payload()
        self.run_action('Install', '-FailAfter', '1', '-SimulateInterruption', ok=False)
        self.run_action('Install', ok=False)
        self.run_action('Uninstall', ok=False)
        self.run_action('Recover')
        self.assertEqual(self.snapshot(), before)
        self.run_action('Recover')  # Idempotent.

    def test_deferred_commit_and_setup_abort(self):
        self.run_action('Install', '-Owner', 'inno', '-DeferCommit')
        self.run_action('Recover', '-Owner', 'inno')
        self.assertEqual(self.snapshot(), {})
        self.run_action('Install', '-Owner', 'inno', '-DeferCommit')
        self.run_action('Commit', '-Owner', 'inno')
        self.assertTrue((self.dst / 'bin/BG3RTX.exe').exists())

    def test_uninstall_keeps_settings_reports_cache_default(self):
        self.run_action('Install')
        cache = self.local / 'BG3RTX/shader_cache/a.bin'
        cache.parent.mkdir(parents=True)
        cache.write_bytes(b'cache')
        summary = self.root / 'cache-size.txt'
        self.run_action('CacheInfo', '-SummaryPath', summary)
        self.assertEqual(summary.read_text(), '5 bytes in 1 files')
        for rel in ('bg3rtx_user.ini', 'bg3rtx_launcher.ini', 'reports/a.zip', 'other.txt'):
            p = self.dst / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'keep')
        self.run_action('Uninstall')
        self.assertEqual(len(self.snapshot()), 4)
        self.assertTrue(cache.exists())
        self.run_action('Install')
        self.run_action('Uninstall', '-RemoveSettings', '-RemoveShaderCache')
        self.assertFalse(cache.exists())
        self.assertEqual(len(self.snapshot()), 2)

    def test_legacy_transfer_original_collisions_and_guard(self):
        self.dst.mkdir()
        (self.dst / '.bg3rtx_install').write_text('legacy')
        (self.dst / 'install_manifest.txt').write_text('retired.txt\n')
        (self.dst / 'retired.txt').write_bytes(b'legacy mod')
        (self.dst / '.bg3rtx_backup').mkdir()
        (self.dst / '.bg3rtx_backup/retired.txt').write_bytes(b'original')
        self.run_action('Install', '-Owner', 'inno', ok=False)
        self.run_action('Install', '-Owner', 'inno', '-TransferLegacy')
        self.run_action('Uninstall', ok=False)
        self.run_action('Uninstall', '-Owner', 'inno')
        self.assertEqual((self.dst / 'retired.txt').read_bytes(), b'original')

    def test_running_processes_refused_without_mutation(self):
        for process in ('bg3', 'bg3_dx11', 'BG3RTX'):
            self.run_action('Install', process=process, ok=False)
            self.assertFalse(self.dst.exists())

    def test_legacy_entry_points_refuse_setup_ownership(self):
        self.run_action('Install', '-Owner', 'inno')
        before = self.snapshot()
        for name, option in (('install.ps1', '-Destination'), ('uninstall.ps1', '-InstallDir')):
            script = self.root / name
            shutil.copyfile(ROOT / 'scripts/release' / name, script)
            result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(script), option, str(self.dst)],
                                    env=dict(os.environ, LOCALAPPDATA=str(self.local)),
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('transactional lifecycle', result.stderr)
            self.assertEqual(self.snapshot(), before)

    def test_hash_mismatch_missing_file_duplicate_and_escape(self):
        for rel in ('../outside', 'C:\\outside', 'bin/a:stream', 'bin/NUL.dll', 'bg3rtx_user.ini', 'unins000.exe'):
            self.manifest['files'].append({'path': rel, 'sha256': '0' * 64})
            self.save_manifest()
            self.run_action('Install', ok=False)
            self.assertFalse(self.dst.exists())
            self.manifest['files'].pop()
        self.manifest['files'].append(dict(self.manifest['files'][0]))
        self.save_manifest()
        self.run_action('Install', ok=False)
        self.manifest['files'].pop()
        self.manifest['files'][0]['sha256'] = '0' * 64
        self.save_manifest()
        self.run_action('Install', ok=False)
        self.assertFalse(self.dst.exists())

    def test_corrupt_backup_fails_before_restore_changes(self):
        self.run_action('Install')
        self.files['bin/BG3RTX.exe'] = b'new'
        self.write_payload()
        self.run_action('Install')
        before = self.snapshot()
        previous = next(self.local.rglob('previous.json'))
        snap = json.loads(previous.read_text(encoding='utf-8-sig'))
        p = previous.parent / snap['folder'] / 'bin/BG3RTX.exe'
        p.write_bytes(b'corrupt')
        self.run_action('Restore', ok=False)
        self.assertEqual(self.snapshot(), before)

    def test_game_and_profile_destinations_never_modified(self):
        game = self.root / 'game'
        (game / 'bin').mkdir(parents=True)
        (game / 'bin/bg3.exe').write_bytes(b'never executed')
        before = self.snapshot(game)
        for path in (game, game / 'mods', self.root / 'Larian Studios/profile'):
            self.dst = path
            self.run_action('Install', ok=False)
        self.assertEqual(self.snapshot(game), before)

    def test_unowned_collision_refused(self):
        self.dst.mkdir()
        (self.dst / 'retired.txt').write_bytes(b'unrelated')
        before = self.snapshot()
        self.run_action('Install', ok=False)
        self.assertEqual(self.snapshot(), before)

    def test_payload_and_backup_ancestor_junctions(self):
        self.run_action('Install')
        before = self.snapshot()
        target = self.root / 'untouched'
        target.mkdir()
        (target / 'keep').write_bytes(b'keep')
        # The junctions are explicitly unlinked before TemporaryDirectory cleanup.
        links = [self.payload / 'bin', self.local / 'BG3RTX/backups']
        for link in links:
            saved = link.with_name(link.name + '-saved')
            link.rename(saved)
            q = lambda p: "'" + str(p).replace("'", "''") + "'"
            result = subprocess.run([PS, '-NoProfile', '-Command', 'New-Item -ItemType Junction -Path ' + q(link) + ' -Target ' + q(target) + ' -ErrorAction Stop | Out-Null'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            try:
                self.run_action('Install', ok=False)
                self.assertEqual(self.snapshot(), before)
                self.assertEqual(self.snapshot(target), {'keep': b'keep'})
            finally:
                os.rmdir(link)
                saved.rename(link)

    def test_junction_ancestor_in_payload_destination_backup_and_cache(self):
        target = self.root / 'unrelated'
        target.mkdir()
        (target / 'keep').write_bytes(b'keep')
        def junction(link):
            link.parent.mkdir(parents=True, exist_ok=True)
            q = lambda p: "'" + str(p).replace("'", "''") + "'"
            r = subprocess.run([PS, '-NoProfile', '-Command', 'New-Item -ItemType Junction -Path ' + q(link) + ' -Target ' + q(target) + ' -ErrorAction Stop | Out-Null'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.addCleanup(lambda: os.rmdir(link) if link.exists() and link.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT else None)
        self.dst.mkdir()
        link = self.dst / 'bin'
        junction(link)
        self.run_action('Install', ok=False)
        os.rmdir(link)
        self.run_action('Install')
        before = self.snapshot()
        cache = self.local / 'BG3RTX/shader_cache'
        junction(cache)
        self.run_action('Uninstall', '-RemoveShaderCache', ok=False)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.snapshot(target), {'keep': b'keep'})


if __name__ == '__main__':
    unittest.main(verbosity=2)
