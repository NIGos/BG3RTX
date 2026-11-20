"""Guards of the maintenance scripts: remove-worktree.ps1 refuses to run anywhere but the main checkout (and never
removes test-main), and app-control.ps1 names Smart App Control when it is the reason a fresh executable cannot start.
Nothing is removed and no registry value is changed: the scripts run against a throwaway folder."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PS = shutil.which("powershell") or shutil.which("pwsh")


def run(*args):
    return subprocess.run([PS, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", *args],
                          capture_output=True, text=True, timeout=120)


class RemoveWorktreeGuards(unittest.TestCase):
    def tree(self, tmp, git_is_dir):
        root = pathlib.Path(tmp) / "checkout"
        (root / "scripts").mkdir(parents=True)
        shutil.copy(ROOT / "scripts/remove-worktree.ps1", root / "scripts/remove-worktree.ps1")
        if git_is_dir:
            (root / ".git").mkdir()
        else:
            (root / ".git").write_text("gitdir: elsewhere\n", encoding="utf-8")   # what a linked worktree has
        (root / ".claude/worktrees/victim").mkdir(parents=True)
        (root / ".claude/worktrees/victim/keep.txt").write_text("x", encoding="utf-8")
        return root

    def test_a_worktree_copy_refuses(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.tree(tmp, git_is_dir=False)
            r = run("-File", str(root / "scripts/remove-worktree.ps1"), "-Name", "victim")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("must be run from the main checkout", r.stdout + r.stderr)
            self.assertTrue((root / ".claude/worktrees/victim/keep.txt").is_file())

    def test_test_main_and_paths_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.tree(tmp, git_is_dir=True)
            r = run("-File", str(root / "scripts/remove-worktree.ps1"), "-Name", "test-main")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("never removed", r.stdout + r.stderr)
            r = run("-File", str(root / "scripts/remove-worktree.ps1"), "-Name", "..\\victim")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("without path separators", r.stdout + r.stderr)
            # A main checkout without its shared folders stops before touching anything.
            r = run("-File", str(root / "scripts/remove-worktree.ps1"), "-Name", "victim")
            self.assertNotEqual(r.returncode, 0)
            self.assertTrue((root / ".claude/worktrees/victim/keep.txt").is_file())


class SmartAppControlHint(unittest.TestCase):
    def hint(self, state):
        script = ". '%s'; Get-SmartAppControlHint 'the Setup' %s" % (ROOT / "scripts/app-control.ps1", state)
        r = run("-Command", script)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.strip()

    def test_only_the_blocking_state_is_named(self):
        on = self.hint("1")
        self.assertIn("Smart App Control is ON", on)
        self.assertIn("the Setup will not start", on)
        for state in ("0", "2", "$null"):   # off, evaluation, no such policy
            self.assertEqual(self.hint(state), "")

    def test_the_scripts_that_start_fresh_executables_use_it(self):
        for rel in ("scripts/build.ps1", "scripts/release/verify_setup.ps1"):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("app-control.ps1", text, rel)
            self.assertIn("Get-SmartAppControlHint", text, rel)


class SetupRowWithAnInstall(unittest.TestCase):
    """verify_rc.ps1: where a BG3RTX Setup is installed for this user (its HKCU Apps entry exists), the compiled Setup
    route is not run and its row is SKIPPED with the install named, never FAIL (RC 2026-11-15 failed only for this) and
    never PASS. The entries are fakes: the registry is not read or written."""
    def ps(self, body):
        r = run("-Command", ". '%s'; %s" % (ROOT / "scripts/release/setup_presence.ps1", body))
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.strip()

    def test_an_install_skips_the_row_and_names_it(self):
        fake = "[pscustomobject]@{Location='C:\\Users\\x\\AppData\\Local\\Programs\\BG3RTX';Version='0.9.6-hairtest18'}"
        reason = self.ps("Get-SetupSkipReason @(%s)" % fake)
        self.assertTrue(reason.startswith("skipped: another BG3RTX install present"), reason)
        self.assertIn("0.9.6-hairtest18", reason)
        self.assertIn("Programs\\BG3RTX", reason)
        self.assertEqual(self.ps("Get-SetupSkipReason @()"), "")
        self.assertEqual(self.ps("Get-SetupSkipReason $null"), "")

    def test_installed_version_txt_wins_over_a_stale_apps_entry(self):
        # RC 2026-11-20: the Apps entry said 0.9.6-hairtest10 (written by an old Setup), VERSION.txt hairtest18.
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "VERSION.txt").write_text("BG3RTX 0.9.6-hairtest18\ncommit 1c09e039\nlayer lean\n",
                                                           encoding="utf-8")
            self.assertEqual(self.ps("Get-InstalledVersion '%s'" % tmp), "0.9.6-hairtest18")
            fake = "[pscustomobject]@{Location='%s';Version='0.9.6-hairtest10';Installed=(Get-InstalledVersion '%s')}" % (tmp, tmp)
            reason = self.ps("Get-SetupSkipReason @(%s)" % fake)
            self.assertIn("0.9.6-hairtest18 from VERSION.txt; Apps entry says 0.9.6-hairtest10", reason)
            same = "[pscustomobject]@{Location='%s';Version='0.9.6-hairtest18';Installed='0.9.6-hairtest18'}" % tmp
            self.assertIn("(%s, 0.9.6-hairtest18)" % tmp, self.ps("Get-SetupSkipReason @(%s)" % same))
        # No folder or no VERSION.txt: the Apps entry's version, as before.
        self.assertEqual(self.ps("Get-InstalledVersion ''"), "")
        self.assertEqual(self.ps(r"Get-InstalledVersion 'C:\no\such\folder'"), "")

    def test_result_and_exit_code(self):
        self.assertEqual(self.ps("Get-RcResult @('PASS','PASS')"), "PASS")
        self.assertEqual(self.ps("Get-RcResult @('PASS','SKIPPED','PASS')"), "PASS with 1 skipped row")
        self.assertEqual(self.ps("Get-RcResult @('PASS','SKIPPED','FAIL')"), "FAIL")
        rc = (ROOT / "scripts/release/verify_rc.ps1").read_text(encoding="utf-8")
        # The check comes before the Setup route runs, and a skip is its own status (exit 2), not a pass.
        self.assertLess(rc.index("Get-SetupSkipReason (Get-BG3RTXAppsEntries)"), rc.index("Run 'verify compiled Setup'"))
        self.assertIn("elseif ($verdict -ne 'FAIL') { exit 2 }", rc)
        setup = (ROOT / "scripts/release/verify_setup.ps1").read_text(encoding="utf-8")
        self.assertIn("refusing to modify an existing installation", setup)   # the route itself still refuses


if __name__ == "__main__":
    unittest.main()
