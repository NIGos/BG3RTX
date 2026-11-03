"""Exercise only disposable package/game folders. No binaries are executed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which('powershell.exe') if os.name == 'nt' else None


@unittest.skipUnless(POWERSHELL, 'Windows PowerShell is required')
class ReleaseInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bg3rtx-release-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / 'package'
        self.install = self.root / 'installed [beta]'
        self.game = self.root / 'fake game'
        (self.game / 'bin').mkdir(parents=True)
        (self.game / 'bin/bg3.exe').write_bytes(b'fake game, never executed')
        self.env = dict(os.environ, LOCALAPPDATA=str(self.root / 'local'))
        self.package.mkdir()
        for name in ('install.ps1', 'uninstall.ps1'):
            shutil.copyfile(ROOT / 'scripts/release' / name, self.package / name)
        (self.package / 'bin').mkdir()
        (self.package / 'bin/BG3RTX.exe').write_bytes(b'fake launcher v1')
        (self.package / 'retired.txt').write_bytes(b'old payload')
        self.files = ['install.ps1', 'uninstall.ps1', 'bin/BG3RTX.exe', 'retired.txt']
        self.manifest(self.files)

    def manifest(self, files):
        (self.package / 'files.txt').write_text('\n'.join(files), encoding='utf-8')

    def run_script(self, name, *args, success=True):
        # Always run a temporary copy, never an installed or repository script.
        # The user's game may be open. Mock process discovery only in this child
        # shell; the production guard stays intact. All I/O uses temp folders.
        def quote(value):
            return "'" + str(value).replace("'", "''") + "'"
        command = (
            'function Get-Process { [CmdletBinding()] param([string[]]$Name, [int]$Id) '
            'if ($Id) { Microsoft.PowerShell.Management\\Get-Process -Id $Id } }; & '
            + quote(self.package / name) + ' '
            + ' '.join(str(a) if str(a).startswith('-') else quote(a) for a in args))
        result = subprocess.run(
            [POWERSHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
             '-Command', command],
            cwd=self.root, env=self.env, capture_output=True, text=True, timeout=40)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def install_args(self):
        return ('-Destination', self.install, '-GameDir', self.game, '-NoShortcut')

    def uninstall_args(self):
        return ('-InstallDir', self.install, '-GameDir', self.game,
                '-NoShortcut', '-KeepShaderCache')

    def snapshot(self, root):
        return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}

    def test_update_restore_and_settings(self):
        (self.install / 'bin').mkdir(parents=True)
        original = b'original launcher'
        (self.install / 'bin/BG3RTX.exe').write_bytes(original)
        (self.install / 'retired.txt').write_bytes(b'original retired')
        (self.install / 'unrelated.txt').write_bytes(b'keep')
        (self.install / 'bg3rtx_user.ini').write_bytes(b'preset=low')
        game_before = self.snapshot(self.game)
        self.run_script('install.ps1', *self.install_args(), '-WhatIf')
        self.assertFalse((self.install / '.bg3rtx_install').exists())
        self.run_script('install.ps1', *self.install_args())
        self.run_script('install.ps1', *self.install_args())
        self.assertEqual((self.install / '.bg3rtx_backup/bin/BG3RTX.exe').read_bytes(), original)
        (self.package / 'bin/BG3RTX.exe').write_bytes(b'fake launcher v2')
        self.manifest(self.files[:-1])
        self.run_script('install.ps1', *self.install_args())
        self.assertFalse((self.install / 'retired.txt').exists())
        self.assertEqual((self.install / 'bg3rtx_user.ini').read_bytes(), b'preset=low')
        before = self.snapshot(self.install)
        self.run_script('uninstall.ps1', *self.uninstall_args(), '-WhatIf')
        self.assertEqual(self.snapshot(self.install), before)
        self.run_script('uninstall.ps1', *self.uninstall_args(), '-KeepSettings')
        self.assertEqual(self.snapshot(self.install), {
            'bin\\BG3RTX.exe': original, 'retired.txt': b'original retired',
            'unrelated.txt': b'keep', 'bg3rtx_user.ini': b'preset=low'})
        self.assertEqual(self.snapshot(self.game), game_before)

    def test_fresh_install_removes_runtime_files(self):
        self.run_script('install.ps1', *self.install_args())
        for rel in ('bg3rtx_user.ini', 'bg3rtx.log', 'bin/hot/core.dll', 'captures/test.txt'):
            path = self.install / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'runtime')
        self.run_script('uninstall.ps1', *self.uninstall_args())
        self.assertFalse(self.install.exists())

    def test_invalid_packages_do_not_modify_destination(self):
        for entry in ('../escaped.txt', 'C:\\escaped.txt', 'bin/file:stream',
                      'bin/*.dll', '.bg3rtx_install', 'bg3rtx_user.ini',
                      'install_manifest.txt.', 'missing.dll'):
            with self.subTest(entry=entry):
                self.manifest(self.files + [entry])
                self.run_script('install.ps1', *self.install_args(), success=False)
                self.assertFalse(self.install.exists())

    def test_protected_destinations(self):
        before = self.snapshot(self.game)
        for target in (self.game, self.game / 'bin', self.game / 'mods',
                       self.root / 'Larian Studios/Baldurs Gate 3', self.package):
            with self.subTest(target=target):
                self.run_script('install.ps1', '-Destination', target, '-NoShortcut', success=False)
        self.assertEqual(self.snapshot(self.game), before)

    def test_uninstall_manifest_preflight(self):
        self.run_script('install.ps1', *self.install_args())
        manifest = self.install / 'install_manifest.txt'
        manifest.write_text(manifest.read_text(encoding='utf-8-sig') + '\n../outside.txt')
        before = self.snapshot(self.install)
        self.run_script('uninstall.ps1', *self.uninstall_args(), success=False)
        self.assertEqual(self.snapshot(self.install), before)

    def test_incomplete_update_preserves_old_install(self):
        self.run_script('install.ps1', *self.install_args())
        before = self.snapshot(self.install)
        self.manifest(self.files + ['missing.dll'])
        self.run_script('install.ps1', *self.install_args(), success=False)
        self.assertEqual(self.snapshot(self.install), before)

    def test_linked_destination_is_refused(self):
        target = self.root / 'unrelated target'
        target.mkdir()
        (target / 'keep.txt').write_bytes(b'keep')
        # Junction creation needs no elevation. Remove only the junction itself.
        quote = lambda p: "'" + str(p).replace("'", "''") + "'"
        result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command',
            'New-Item -ItemType Junction -Path ' + quote(self.install)
            + ' -Target ' + quote(target) + ' -ErrorAction Stop | Out-Null'],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            self.run_script('install.ps1', *self.install_args(), success=False)
            self.assertEqual(self.snapshot(target), {'keep.txt': b'keep'})
        finally:
            os.rmdir(self.install)  # Never recurse through a junction.


if __name__ == '__main__':
    unittest.main(verbosity=2)
