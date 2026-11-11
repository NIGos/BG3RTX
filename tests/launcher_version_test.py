"""The launcher's displayed version has one source: the package's VERSION.txt (tools/launcher/version.h
display_version), with the build's base number only for the executable resource and developer builds."""
import pathlib
import re
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = (ROOT / "tools/launcher/version.h").read_text(encoding="utf-8")


class LauncherVersion(unittest.TestCase):
    def test_header_string_and_number_agree(self):
        text = re.search(r'#define BG3RTX_PRODUCT_VERSION "(\d+\.\d+\.\d+\.\d+)"', HEADER).group(1)
        number = re.search(r"#define BG3RTX_PRODUCT_VERSION_NUM (\d+,\d+,\d+,\d+)", HEADER).group(1)
        self.assertEqual(text.split("."), number.split(","))

    def test_only_the_resource_and_the_header_use_the_base_version(self):
        # Everything shown to the player goes through product_version() / Bridge::set_version.
        users = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "tools/launcher").rglob("*")
                       if p.is_file() and p.suffix in {".cpp", ".h", ".rc", ".js", ".html"} and not p.name.endswith("_test.cpp")
                       and re.search(r"\bBG3RTX_PRODUCT_VERSION\b", p.read_text(encoding="utf-8", errors="replace")))
        self.assertEqual(users, ["tools/launcher/launcher.rc", "tools/launcher/version.h"])

    def test_no_version_literal_in_the_page_or_its_tests(self):
        for rel in ["tools/launcher/ui/app.js", "tools/launcher/ui/index.html", "tests/launcher_ui_test.js",
                    "tests/launcher_web_bridge_json_test.py"]:
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIsNone(re.search(r"""["']\d+\.\d+\.\d+(\.\d+)?["']""", text), rel)

    def test_package_refuses_a_number_the_executable_does_not_carry(self):
        ps = shutil.which("pwsh") or shutil.which("powershell")
        self.assertIsNotNone(ps)
        result = subprocess.run([ps, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                                 str(ROOT / "scripts/package.ps1"), "-ListOnly", "-Version", "99.0.1-test"],
                                capture_output=True, text=True, timeout=120)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match tools\\launcher\\version.h", result.stdout + result.stderr)
        base = re.search(r'#define BG3RTX_PRODUCT_VERSION "(\d+\.\d+\.\d+)\.', HEADER).group(1)
        result = subprocess.run([ps, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                                 str(ROOT / "scripts/package.ps1"), "-ListOnly", "-Version", base + "-test"],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
