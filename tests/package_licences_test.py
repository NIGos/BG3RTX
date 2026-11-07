"""Notice payload checks only: never build or create a player package."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


class PackageLicencesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('LICENSE', 'LICENSE-EXCEPTION.md'):
            shutil.copyfile(ROOT / name, self.root / name)
        (self.root / "scripts").mkdir()
        shutil.copyfile(ROOT / "scripts/package.ps1", self.root / "scripts/package.ps1")
        shutil.copyfile(ROOT / "scripts/linked-path.ps1", self.root / "scripts/linked-path.ps1")
        shutil.copyfile(ROOT / "scripts/package-third-party.json",
                        self.root / "scripts/package-third-party.json")
        self.release = self.root / "docs/release"
        shutil.copytree(ROOT / "docs/release/licences", self.release / "licences")
        shutil.copyfile(ROOT / "docs/release/THIRD_PARTY.md", self.release / "THIRD_PARTY.md")
        inventory = (self.release / "THIRD_PARTY.md").read_text(encoding="utf-8")
        section = inventory.split("## Required notice files\n", 1)[1].split("\n## ", 1)[0]
        self.required = re.findall(r"^- `([^`]+)`$", section, re.M)
        self.assertGreaterEqual(len(self.required), 8)
        # Wiring checks must use the real collected release texts.
        for name in self.required:
            path = self.release / "licences" / name
            self.assertTrue(path.is_file(), f"Missing real release notice: {name}")

    def list_notices(self):
        self.assertIsNotNone(POWERSHELL, "PowerShell is needed for package -ListOnly checks")
        result = subprocess.run(
            [POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(self.root / "scripts/package.ps1"), "-ListOnly"],
            capture_output=True, text=True, timeout=30,
        )
        self.assertFalse((self.root / "dist").exists(), "ListOnly wrote package output")
        return result

    def test_all_files_including_additional_nested_notices(self):
        extra = self.release / "licences/extra/nested-notice.txt"
        extra.parent.mkdir()
        extra.write_text("Additional notice\n", encoding="utf-8")
        result = self.list_notices()
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        actual = {entry["Destination"].replace("\\", "/"): Path(entry["Source"])
                  for entry in payload}
        expected = {"THIRD_PARTY.md": self.release / "THIRD_PARTY.md"}
        expected.update({name: self.root / name for name in ('LICENSE', 'LICENSE-EXCEPTION.md')})
        expected.update({"licences/" + path.relative_to(self.release / "licences").as_posix(): path
                         for path in (self.release / "licences").rglob("*") if path.is_file()})
        self.assertEqual(actual, expected)
        script = (ROOT / "scripts/package.ps1").read_text(encoding="utf-8")
        self.assertIn("foreach ($notice in $noticeFiles) { Copy-To $notice.Source $notice.Destination }", script)
        self.assertLess(script.index("if ($ListOnly)"), script.index('"low-priority.ps1"'))

    def test_missing_collected_notice_fails(self):
        name = "NVIDIA-Reflex.txt"
        (self.release / "licences" / name).unlink()
        result = self.list_notices()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(name, result.stderr)
        self.assertIn("missing required notices", result.stderr)

    def test_launcher_dry_run_lists_runtime_assets_without_output(self):
        # Disposable source fixture only: never assemble a player package.
        shutil.copytree(ROOT / "assets/fonts", self.root / "assets/fonts")
        shutil.copytree(ROOT / "assets/branding", self.root / "assets/branding")
        ui = self.root / "tools/launcher/ui"
        ui.mkdir(parents=True)
        for name in ("index.html", "app.css", "app.js"):
            (ui / name).write_text("Fixture UI only\n", encoding="utf-8")
        command = [POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                   "-File", str(self.root / "scripts/package.ps1"), "-DryRun"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        by_destination = {entry["Destination"].replace("\\", "/"): Path(entry["Source"])
                          for entry in payload}
        for name in ("index.html", "app.css", "app.js"):
            self.assertEqual(by_destination["bin/ui/" + name], ui / name)
        self.assertEqual(by_destination["bin/ui/fonts/OFL.txt"].read_bytes(),
                         (ROOT / "assets/fonts/OFL.txt").read_bytes())
        self.assertIn("bin/ui/fonts/LatoWeb-Regular.ttf", by_destination)
        self.assertIn("bin/ui/fonts/LatoWeb-Semibold.ttf", by_destination)
        self.assertIn("bin/ui/branding/bg3rtx.svg", by_destination)
        for asset in (ROOT / "assets/branding/ornaments").iterdir():
            if asset.is_file():
                self.assertIn("bin/ui/branding/ornaments/" + asset.name, by_destination)
        self.assertFalse((self.root / "dist").exists())
        self.assertFalse((self.root / "bin").exists())
        (ui / "app.js").unlink()
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing launcher asset", result.stderr)
        self.assertFalse((self.root / "dist").exists())

    def test_full_dry_run_matches_materialized_fixture_and_reports_missing_lean(self):
        # All bytes are disposable fixtures. Never execute payload binaries.
        for folder in ('assets/fonts', 'assets/branding', 'tools/launcher/ui'):
            shutil.copytree(ROOT / folder, self.root / folder)
        for folder in ('bin', 'shaders', 'common', 'data/assetdb'):
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        manifest = json.loads((self.root / 'scripts/package-third-party.json').read_text())
        names = [e['destination'] for e in manifest['binaries']]
        names += ['bin/VkLayer_bg3rtx.json', 'bin/bg3rtx_core.dll', 'bin/BG3RTX.exe']
        for name in names:
            path = self.root / name
            if name == 'bin/VkLayer_bg3rtx.json' or name.startswith('bin/sl/'):
                path = self.root / 'build-nodiag' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture, never executed')
        (self.root / 'shaders/rt').mkdir()
        for name in ('alpha_fs.txt', 'camera_fade_fs.txt'):
            shutil.copyfile(ROOT / 'shaders/rt' / name, self.root / 'shaders/rt' / name)
        (self.root / 'shaders/main.hlsl').write_text('#include "../common/fixture.h"')
        (self.root / 'common/fixture.h').write_text('// fixture')
        (self.root / 'shaders/dev.py').write_text('must not ship')
        (self.root / 'shaders/stale.spv').write_bytes(b'must not ship')
        (self.root / 'bin/private.pdb').write_bytes(b'must not ship')
        (self.root / 'data/assetdb/bg3rtx_assetdb.bin').write_bytes(b'fixture database')
        (self.root / 'bg3rtx.ini').write_text('dev=0')
        (self.root / 'data/release.ini').write_text('dev=0')
        (self.release / 'README.md').write_text('fixture readme')
        (self.root / 'docs/USER_SETTINGS.md').write_text('fixture settings')
        shutil.copyfile(ROOT / 'scripts/low-priority.ps1', self.root / 'scripts/low-priority.ps1')
        shutil.copytree(ROOT / 'scripts/release', self.root / 'scripts/release')
        def pack(version, dry=False):
            cmd = ("function git { 'fixture' }; & '" + str(self.root / 'scripts/package.ps1').replace("'", "''")
                   + "' -NoBuild -NoZip -Version " + version + " -OutDir '"
                   + str(self.root / 'output').replace("'", "''") + "'" + (' -DryRun' if dry else ''))
            result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy',
                                     'Bypass', '-Command', cmd], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return self.root / 'output' / ('BG3RTX-' + version)
        audit = pack('audit', dry=True)
        report = json.loads((audit / 'audit.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(report['ManifestDifferences'], 0)
        self.assertEqual(json.loads((audit / 'files.diff.json').read_text(encoding='utf-8-sig')), [])
        self.assertEqual([e['Destination'] for e in report['Missing']], ['bin\\VkLayer_bg3rtx.dll'])
        lean = self.root / 'build-nodiag/bin/VkLayer_bg3rtx.dll'
        lean.parent.mkdir(parents=True, exist_ok=True)
        lean.write_bytes(b'fake lean layer, never executed')
        package = pack('fixture')
        planned = set((audit / 'files.txt').read_text(encoding='utf-8-sig').splitlines())
        actual = {str(p.relative_to(package)) for p in package.rglob('*') if p.is_file()}
        self.assertEqual(planned, actual)
        self.assertEqual(planned, set((package / 'files.txt').read_text(encoding='utf-8-sig').splitlines()))
        self.assertFalse(any(name.endswith(('.pdb', '.py', '.spv')) for name in actual))
        self.assertIn('Diagnostics.cmd', actual)
        self.assertIn('diagnostics.ps1', actual)
        for name in ('LICENSE', 'LICENSE-EXCEPTION.md'):
            self.assertIn(name, actual)
            self.assertEqual((package / name).read_bytes(), (ROOT / name).read_bytes())

    def test_missing_dxc_and_vulkan_fail_together(self):
        names = ["DirectXShaderCompiler-LICENSE.txt", "Vulkan-Headers-LICENSE.txt"]
        for name in names:
            (self.release / "licences" / name).unlink()
        result = self.list_notices()
        self.assertNotEqual(result.returncode, 0)
        for name in names:
            self.assertIn(name, result.stderr)

    def test_every_shipped_third_party_binary_requires_its_notices(self):
        manifest = json.loads((self.root / "scripts/package-third-party.json").read_text())
        expected = {"bin/dxcompiler.dll"} | {"bin/sl/" + name for name in (
            "sl.interposer.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.reflex.dll", "sl.pcl.dll",
            "nvngx_dlssg.dll", "NvLowLatencyVk.dll", "nvngx_dlss.dll", "nvngx_dlssd.dll")}
        self.assertEqual({entry["destination"] for entry in manifest["binaries"]}, expected)
        script = (ROOT / "scripts/package.ps1").read_text()
        self.assertIn("foreach ($entry in $thirdParty.binaries)", script)
        self.assertIn("Copy-To $source $entry.destination", script)
        # No parallel hardcoded DLL list can bypass the notice manifest.
        copies = re.findall(r'Copy-To[^\n]+', script)
        self.assertEqual([line for line in copies if '"bin' in line and '.dll"' in line],
                         ['Copy-To (Join-Path $layerBin "VkLayer_bg3rtx.dll") "bin\\VkLayer_bg3rtx.dll"'])
        components = manifest["binaries"] + manifest["compiled_components"]
        for entry in components:
            self.assertTrue(entry["licences"], entry)
            for notice in entry["licences"]:
                self.assertIn(notice, self.required)
        # Each binary/header dependency fails with one missing real notice at a time.
        for notice in sorted({n for e in components for n in e["licences"]}):
            with self.subTest(notice=notice):
                path = self.release / "licences" / notice
                data = path.read_bytes()
                path.unlink()
                try:
                    result = self.list_notices()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(notice, result.stderr)
                finally:
                    path.write_bytes(data)

    def test_manifest_cannot_add_an_unlicensed_binary(self):
        path = self.root / "scripts/package-third-party.json"
        manifest = json.loads(path.read_text())
        for licences, message in (([], "has no licences"),
                                  (["unreviewed.txt"], "unlisted third-party licence")):
            manifest["binaries"].append(dict(destination="bin/unreviewed.dll", licences=licences))
            path.write_text(json.dumps(manifest))
            result = self.list_notices()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
            manifest["binaries"].pop()

    def test_release_text_hashes_match_verbatim_copies(self):
        inventory = (ROOT / "docs/release/THIRD_PARTY.md").read_text(encoding="utf-8")
        hashes = dict(re.findall(r"\| `([^`]+)` \| `([0-9a-f]{64})` \|", inventory))
        for path in (ROOT / "docs/release/licences").rglob("*"):
            if path.is_file():
                data = path.read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), hashes[path.name], path.name)
        self.assertNotIn(b"\r", (ROOT / "docs/release/THIRD_PARTY.md").read_bytes())


if __name__ == "__main__":
    unittest.main()
