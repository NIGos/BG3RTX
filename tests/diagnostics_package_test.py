"""Run the release packer under Windows PowerShell 5.1 against synthetic files only."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent
POWERSHELL = shutil.which("powershell.exe")


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell 5.1 required")
class DiagnosticsPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bg3rtx-diag-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.install = self.base / "fake install [beta]"
        self.install.mkdir()
        self.harness = self.base / "harness.ps1"
        # Mock only the light system query and process detection. Never launch a game process.
        self.harness.write_text(r'''
param([string]$InstallDir, [string]$SavePath, [switch]$Running, [string]$PlayerProfiles)
$ErrorActionPreference = 'Stop'
function Get-CimInstance {
  param($ClassName, $OperationTimeoutSec)
  if ($ClassName -ne 'Win32_VideoController') { throw 'Unexpected query' }
  [pscustomobject]@{Name='Fixture GPU'; DriverVersion='123.45'; DriverDate='2026-01-01'}
}
function Get-Process {
  param($Name, $Id, $ErrorAction)
  if ($Name) { if ($Running) { [pscustomobject]@{Name='bg3'} }; return }
  Microsoft.PowerShell.Management\Get-Process -Id $Id
}
$params = @{InstallDir=$InstallDir}
if ($SavePath) { $params.SavePath = $SavePath }
# Fixture profile folder only: the real Larian folder is never read by this test.
if ($PlayerProfiles) { $params.IncludeLatestSave = $true; $params.PlayerProfiles = $PlayerProfiles }
& $env:BG3RTX_TEST_PACKER @params
''', encoding="utf-8")

    def run_packer(self, save=None, running=False, success=True, profiles=None):
        command = [POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                   "-File", str(self.harness), "-InstallDir", str(self.install)]
        if save is not None:
            command += ["-SavePath", str(save)]
        if profiles is not None:
            command += ["-PlayerProfiles", str(profiles)]
        if running:
            command += ["-Running"]
        env = os.environ.copy()
        env["BG3RTX_TEST_PACKER"] = str(ROOT / "scripts/release/diagnostics.ps1")
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=40)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def fixture(self):
        text = r'''[layer] GPU 'Fixture RTX' api 1.3.0 driver 0x123
[cfg] knobs: preset=Ultra path=C:\Users\PrivateTester\Documents\BG3RTX
[cfg] keys: rt=1 api_key="super secret words" token=hidden-token
path=D:/Users/AnotherPerson/AppData/test
json=C:\\Users\\PrivateTester\\settings
password: "another private value"
{"client_secret":"secret-json"}
Authorization: Bearer bearer-secret.value
url=https://user:pass@example.invalid/?access_token=url-secret
key=generic-secret
provider=sk-1234567890abcdefghijklmnop
'''
        (self.install / "bg3rtx.log").write_text(text, encoding="utf-8")
        (self.install / "bg3rtx.prev.log").write_text("[cfg] keys: old=1\n", encoding="utf-8")
        (self.install / "bg3rtx.run1.prev.log").write_text("rotated tagged log", encoding="utf-8")
        (self.install / "bg3rtx.ini").write_text("preset=Ultra\napi_key=ini-secret\n", encoding="utf-8-sig")
        (self.install / "bg3rtx_user.ini").write_text("preset=Low\nsecret=utf16-secret\n", encoding="utf-16")
        (self.install / "VERSION.txt").write_text("beta-fixture", encoding="utf-8")
        (self.install / "unrelated.txt").write_text("DO NOT COLLECT", encoding="utf-8")
        crash = self.install / "captures" / "crash"
        crash.mkdir(parents=True)
        (crash / "PrivateTester.dmp").write_bytes(b"fake crash\x00\xff")
        (crash / "unrelated.txt").write_text("DO NOT COLLECT", encoding="utf-8")
        aftermath = self.install / "captures" / "aftermath"
        aftermath.mkdir()
        (aftermath / "private.nv-gpudmp").write_bytes(b"fake GPU dump")

    def archive(self):
        files = list(self.install.glob("BG3RTX-diagnostics-*.zip"))
        self.assertEqual(len(files), 1)
        return zipfile.ZipFile(files[0])

    def test_package_redaction_and_read_only_save(self):
        self.fixture()
        save = self.base / "private save.lsv"
        save.write_bytes(b"synthetic save\x00\xff")
        save.chmod(0o444)
        self.addCleanup(save.chmod, 0o666)
        before = (hashlib.sha256(save.read_bytes()).digest(), save.stat().st_mtime_ns, save.stat().st_mode)
        result = self.run_packer(save)
        after = (hashlib.sha256(save.read_bytes()).digest(), save.stat().st_mtime_ns, save.stat().st_mode)
        self.assertEqual(before, after)
        self.assertIn("Selected save:", result.stdout)
        self.assertIn("bytes", result.stdout)
        self.assertIn("Package contents:", result.stdout)
        with self.archive() as archive:
            names = set(archive.namelist())
            self.assertEqual(names, {
                "logs/bg3rtx.log", "logs/bg3rtx.prev.log", "logs/log-003.log",
                "settings/bg3rtx.ini", "settings/bg3rtx_user.ini", "settings/VERSION.txt",
                "settings/effective-log.txt", "system/device-log.txt", "system/system.txt",
                "crash/dump-001.dmp", "crash/dump-002.nv-gpudmp", "save/selected.lsv", "CONTENTS.txt"})
            self.assertEqual(archive.read("save/selected.lsv"), save.read_bytes())
            self.assertTrue(archive.getinfo("save/selected.lsv").external_attr & 1)
            self.assertEqual(archive.read("crash/dump-001.dmp"), b"fake crash\x00\xff")
            text = "\n".join(archive.read(name).decode("utf-8-sig") for name in names
                             if not name.startswith(("save/", "crash/")))
            for secret in ("PrivateTester", "AnotherPerson", "super secret words", "hidden-token",
                           "another private value", "secret-json", "bearer-secret", "url-secret",
                           "generic-secret", "ini-secret", "utf16-secret", "1234567890abcdefghijklmnop",
                           "user:pass", "DO NOT COLLECT"):
                self.assertNotIn(secret, text)
            self.assertIn("[USER]", text)
            self.assertIn("[REDACTED]", text)
            self.assertIn("knobs: preset=Ultra", archive.read("settings/effective-log.txt").decode())
            self.assertIn("old=1", archive.read("settings/effective-log.txt").decode())
            self.assertIn("Fixture RTX", archive.read("system/device-log.txt").decode())
            system = archive.read("system/system.txt").decode()
            self.assertIn("Fixture GPU", system)
            self.assertIn("123.45", system)
            self.assertIn("OS version:", system)

    def test_default_no_save_and_multiple_runs(self):
        self.fixture()
        self.run_packer()
        with self.archive() as archive:
            self.assertFalse(any(name.startswith(("save/", "saves/")) for name in archive.namelist()))
        self.run_packer()
        self.assertEqual(len(list(self.install.glob("*.zip"))), 2)

    def test_missing_inputs_reported(self):
        self.run_packer()
        with self.archive() as archive:
            contents = archive.read("CONTENTS.txt").decode()
            self.assertIn("No bg3rtx logs found", contents)
            self.assertIn("Missing: bg3rtx.ini", contents)
            self.assertEqual(len(archive.namelist()), 4)

    def test_running_refused_before_save_access(self):
        result = self.run_packer(self.base / "does-not-exist.lsv", running=True, success=False)
        self.assertIn("Baldur's Gate 3 is running", result.stderr)
        self.assertFalse(list(self.install.glob("*.zip")))

    def save_fixture(self):
        """Two Story saves in two profiles plus decoys; the 2022 QuickSave is the newest .lsv."""
        profiles = self.base / "PlayerProfiles"
        old = profiles / "Public" / "Savegames" / "Story" / "Tav-1111__Old"
        new = profiles / "Second" / "Savegames" / "Story" / "Tav-1234__QuickSave_1"
        other = profiles / "Public" / "Savegames" / "Other"
        for folder in (old, new, other):
            folder.mkdir(parents=True)
        (old / "Old.lsv").write_bytes(b"old save")
        (new / "QuickSave_1.lsv").write_bytes(b"NEWSAVE\x00\xff\x01")
        (new / "QuickSave_1.webp").write_bytes(b"RIFFthumb")
        (old / "notes.txt").write_text("not a save", encoding="utf-8")
        (other / "Elsewhere.lsv").write_bytes(b"not a Story save")
        for path, year in ((old / "Old.lsv", 2021), (new / "QuickSave_1.lsv", 2022),
                           (old / "notes.txt", 2024), (other / "Elsewhere.lsv", 2025)):
            stamp = (year - 1970) * 365 * 86400
            os.utime(path, (stamp, stamp))
        return profiles, new

    def test_latest_save_opt_in(self):
        self.fixture()
        profiles, new = self.save_fixture()
        lsv = new / "QuickSave_1.lsv"
        before = (lsv.read_bytes(), lsv.stat().st_mtime_ns, lsv.stat().st_mode)
        result = self.run_packer(profiles=profiles)
        self.assertEqual(before, (lsv.read_bytes(), lsv.stat().st_mtime_ns, lsv.stat().st_mode))
        self.assertIn("Latest save: Tav-1234__QuickSave_1 (19 bytes", result.stdout)
        with self.archive() as archive:
            saves = sorted(n for n in archive.namelist() if n.startswith(("save/", "saves/")))
            self.assertEqual(saves, ["saves/Tav-1234__QuickSave_1/QuickSave_1.lsv",
                                     "saves/Tav-1234__QuickSave_1/QuickSave_1.webp"])
            for name in saves:
                self.assertEqual(archive.read(name), (new / name.rsplit("/", 1)[1]).read_bytes())
                self.assertTrue(archive.getinfo(name).external_attr & 1)

    def test_latest_save_none_found(self):
        empty = self.base / "EmptyProfiles"
        empty.mkdir()
        self.run_packer(profiles=empty)
        with self.archive() as archive:
            self.assertFalse(any(n.startswith("saves/") for n in archive.namelist()))
            self.assertIn("none found", archive.read("CONTENTS.txt").decode())

    def test_latest_save_refused_while_running(self):
        profiles, _ = self.save_fixture()
        result = self.run_packer(running=True, success=False, profiles=profiles)
        self.assertIn("Baldur's Gate 3 is running", result.stderr)
        self.assertFalse(list(self.install.glob("*.zip")))

    def test_invalid_save_refused(self):
        save = self.base / "not-a-save.txt"
        save.write_text("fixture", encoding="utf-8")
        result = self.run_packer(save, success=False)
        self.assertIn("Select one .lsv", result.stderr)
        self.assertFalse(list(self.install.glob("*.zip")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
