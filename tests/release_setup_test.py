"""Setup metadata/preflight fixtures; no compiler or payload executable runs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PS = shutil.which('powershell.exe') if os.name == 'nt' else None


@unittest.skipUnless(PS, 'Windows PowerShell 5.1 required')
class SetupMetadataTest(unittest.TestCase):
    def setUp(self):
        (ROOT / '.scratch').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='setup-fixture-', dir=ROOT / '.scratch')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.payload = self.base / 'payload spazio'
        self.payload.mkdir()
        self.out = self.base / 'setup output'
        self.data = {'VERSION.txt': b'BG3RTX 0.1.0-beta1\r\n',
                     'LICENSE': b'GPL licence', 'LICENSE-EXCEPTION.md': b'NVIDIA exception',
                     'bin/fixture.bin': b'never executable\x00\xff'}
        for name, value in self.data.items():
            target = self.payload / name
            target.parent.mkdir(exist_ok=True)
            target.write_bytes(value)
        self.manifest = sorted([*self.data, 'files.txt'])
        self.write_manifest()

    def write_manifest(self):
        (self.payload / 'files.txt').write_text('\n'.join(self.manifest), encoding='utf-8')

    def run_build(self, out=None):
        result = subprocess.run(
            [PS, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
             str(ROOT / 'scripts/release/build_setup.ps1'), '-PayloadDir', str(self.payload),
             '-OutDir', str(out or self.out), '-Compiler', str(self.base / 'absent-compiler.exe')],
            capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        return result.stdout + result.stderr

    def test_exact_hashes_combined_license_and_unchanged_payload(self):
        before = {p.relative_to(self.payload).as_posix(): p.read_bytes() for p in self.payload.rglob('*') if p.is_file()}
        self.assertIn('absent-compiler.exe', self.run_build())
        metadata = json.loads((self.out / 'payload.sha256.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(metadata['schema'], 1)
        self.assertEqual({f['path']: f['sha256'] for f in metadata['files']},
                         {name: hashlib.sha256(value).hexdigest() for name, value in before.items()})
        license_text = (self.out / 'setup-license.txt').read_text(encoding='utf-8-sig')
        self.assertIn('GPL licence', license_text)
        self.assertIn('NVIDIA exception', license_text)
        self.assertEqual(before, {p.relative_to(self.payload).as_posix(): p.read_bytes() for p in self.payload.rglob('*') if p.is_file()})

    def test_missing_extra_and_duplicate_files_rejected_before_metadata(self):
        for change in ('missing', 'extra', 'duplicate'):
            with self.subTest(change=change):
                original = list(self.manifest)
                if change == 'missing':
                    self.manifest.append('absent.bin')
                elif change == 'extra':
                    self.manifest.remove('bin/fixture.bin')
                else:
                    self.manifest.append('LICENSE')
                self.write_manifest()
                self.assertIn('Payload differs from files.txt', self.run_build())
                self.assertFalse(self.out.exists())
                self.manifest = original

    def test_invalid_version_rejected(self):
        (self.payload / 'VERSION.txt').write_text('BG3RTX ../unsafe')
        self.assertIn('Invalid payload VERSION.txt', self.run_build())
        self.assertFalse(self.out.exists())

    def test_output_outside_scratch_rejected(self):
        self.assertIn('Setup output must be under', self.run_build(ROOT / 'forbidden-setup-fixture'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
