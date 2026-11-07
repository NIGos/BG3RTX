"""scripts/linked-path.ps1: junctions are refused, whole-volume mount points are allowed."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "linked-path.ps1"


def ps(body: str) -> str:
    script = f". '{HELPER}'\n{body}"
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                       capture_output=True, text=True, timeout=60)
    if r.returncode:
        raise AssertionError(r.stdout + r.stderr)
    return r.stdout.strip()


class LinkedPathTest(unittest.TestCase):
    def test_volume_targets(self):
        out = ps("@('Volume{e1f85920-69b9-4d14-a2e6-86f60091b44b}\\', '\\\\?\\Volume{e1f85920-69b9-4d14-a2e6-86f60091b44b}\\', "
                 "'\\??\\Volume{e1f85920-69b9-4d14-a2e6-86f60091b44b}\\', 'C:\\external', 'E:\\Code Projects\\BG3RTX', '') | "
                 "ForEach-Object { [int](Test-VolumeMountTarget $_) }")
        self.assertEqual(out.split(), ["1", "1", "1", "0", "0", "0"])

    def test_junction_refused_plain_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            target, link = Path(tmp) / "target", Path(tmp) / "link"
            (target / "sub").mkdir(parents=True)
            out = ps(f"New-Item -ItemType Junction -Path '{link}' -Target '{target}' | Out-Null\n"
                     f"try {{ 'L=' + (Find-LinkedAncestor '{link / 'sub' / 'out'}'); 'P=' + (Find-LinkedAncestor '{target / 'sub' / 'out'}') }}\n"
                     f"finally {{ [IO.Directory]::Delete('{link}', $false) }}")
            lines = dict(line.split("=", 1) for line in out.splitlines())
            self.assertEqual(Path(lines["L"]), link)
            self.assertEqual(lines["P"], "")
            self.assertTrue((target / "sub").is_dir())  # the junction target survives the unlink

    def test_checkout_allowed(self):
        # The checkout itself must be usable (on this PC it sits on a disk mounted at E:\Storage).
        self.assertEqual(ps(f"Find-LinkedAncestor '{ROOT / '.scratch' / 'probe'}'"), "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
