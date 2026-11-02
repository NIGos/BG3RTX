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
        (self.root / "scripts").mkdir()
        shutil.copyfile(ROOT / "scripts/package.ps1", self.root / "scripts/package.ps1")
        self.release = self.root / "docs/release"
        shutil.copytree(ROOT / "docs/release/licences", self.release / "licences")
        shutil.copyfile(ROOT / "docs/release/THIRD_PARTY.md", self.release / "THIRD_PARTY.md")
        inventory = (self.release / "THIRD_PARTY.md").read_text(encoding="utf-8")
        section = inventory.split("## Required notice files\n", 1)[1].split("\n## ", 1)[0]
        self.required = re.findall(r"^- `([^`]+)`$", section, re.M)
        self.assertGreaterEqual(len(self.required), 8)
        # Fixture-only stand-ins exercise wiring; these are not release licence texts.
        for name in self.required:
            path = self.release / "licences" / name
            if not path.exists():
                path.write_text("Fixture notice only\n", encoding="utf-8")

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

    def test_missing_dxc_and_vulkan_fail_together(self):
        names = ["DirectXShaderCompiler-LICENSE.txt", "Vulkan-Headers-LICENSE.txt"]
        for name in names:
            (self.release / "licences" / name).unlink()
        result = self.list_notices()
        self.assertNotEqual(result.returncode, 0)
        for name in names:
            self.assertIn(name, result.stderr)

    def test_release_texts_are_lf_and_hashes_match(self):
        inventory = (ROOT / "docs/release/THIRD_PARTY.md").read_text(encoding="utf-8")
        hashes = dict(re.findall(r"\| `([^`]+)` \| `([0-9a-f]{64})` \|", inventory))
        for path in (ROOT / "docs/release/licences").rglob("*"):
            if path.is_file():
                data = path.read_bytes()
                self.assertNotIn(b"\r", data, str(path))
                self.assertEqual(hashlib.sha256(data).hexdigest(), hashes[path.name], path.name)
        self.assertNotIn(b"\r", (ROOT / "docs/release/THIRD_PARTY.md").read_bytes())


if __name__ == "__main__":
    unittest.main()
