"""Exercise Invoke-Build with mocked native commands and a tiny cache."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BuildConfiguration(unittest.TestCase):
    @unittest.skipUnless(shutil.which("powershell"), "PowerShell unavailable")
    def test_cached_configuration_controls_reconfigure(self):
        source = (ROOT / "scripts/build.ps1").read_text()
        function = source[source.index("function Invoke-Build"):source.index("\nEnter-VsShell")]
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "build.ninja").write_text("")
            cache = folder / "CMakeCache.txt"
            script = folder / "check.ps1"
            script.write_text("$ErrorActionPreference='Stop'\n" + function + """
$root=$PSScriptRoot
$build=$PSScriptRoot
$Config='Release'
$global:calls=@()
function cmake { $global:calls += ,@($args); $global:LASTEXITCODE=0 }
Invoke-Build ''
if ($calls.Count -ne EXPECTED) { throw "unexpected invocation count $($calls.Count)" }
if (EXPECTED -eq 2 -and $calls[0] -notcontains '-DCMAKE_BUILD_TYPE=Release') { throw 'missing requested configuration' }
""")
            template = script.read_text()
            for cached, expected in (("RelWithDebInfo", 2), ("Release", 1), (None, 2), ("", 2), ("MISSING_KEY", 2)):
                with self.subTest(cached=cached):
                    if cached is None:
                        cache.unlink(missing_ok=True)
                    else:
                        cache.write_text("OTHER:STRING=x\n" if cached == "MISSING_KEY" else f"CMAKE_BUILD_TYPE:STRING={cached}\n")
                    script.write_text(template.replace("EXPECTED", str(expected)))
                    result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
