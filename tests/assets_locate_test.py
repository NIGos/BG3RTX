"""tools/assets/locate.py: no machine path is hard-coded; env overrides; Steam library parsing (offline)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.assets import locate  # noqa: E402


class Locate(unittest.TestCase):
    def setUp(self):
        self.saved = {k: os.environ.get(k) for k in ("BG3_DATA", "BG3RTX_LSLIB", "STEAM_PATH")}

    def tearDown(self):
        for k, v in self.saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def test_vdf(self):
        vdf = '"libraryfolders"\n{\n "0"\n {\n  "path"\t\t"C:\\\\Program Files (x86)\\\\Steam"\n }\n' \
              ' "1"\n {\n  "path"\t\t"D:\\\\SteamLibrary"\n }\n}\n'
        self.assertEqual(locate.steam_libraries(vdf), [Path(r"C:\Program Files (x86)\Steam"), Path(r"D:\SteamLibrary")])

    def test_steam_library_search(self):
        with tempfile.TemporaryDirectory() as t:
            steam, lib = Path(t) / "steam", Path(t) / "lib"
            (steam / "steamapps").mkdir(parents=True)
            data = lib / locate.GAME_SUBDIR
            data.mkdir(parents=True)
            (steam / "steamapps" / "libraryfolders.vdf").write_text('"path" "%s"' % str(lib).replace("\\", "\\\\"))
            os.environ.pop("BG3_DATA", None)
            os.environ["STEAM_PATH"] = str(steam)
            self.assertEqual(Path(locate.game_data()), data)

    def test_env_overrides(self):
        with tempfile.TemporaryDirectory() as t:
            os.environ["BG3_DATA"] = t
            self.assertEqual(locate.game_data(), t)
            os.environ["BG3_DATA"] = str(Path(t) / "missing")
            self.assertEqual(locate.game_data(), "")
            tools = Path(t) / "Packed" / "Tools"
            tools.mkdir(parents=True)
            os.environ["BG3RTX_LSLIB"] = str(Path(t) / "Packed")
            self.assertEqual(locate.lslib_tools(), "")          # no LSLib.dll yet
            (tools / "LSLib.dll").write_bytes(b"")
            for p in (t, str(Path(t) / "Packed"), str(tools)):
                os.environ["BG3RTX_LSLIB"] = p
                self.assertEqual(Path(locate.lslib_tools()), tools)

    def test_no_hard_coded_user_paths(self):
        for f in ("tools/assets/vt_alpha_masks.py", "tools/assets/locate.py"):
            src = (ROOT / f).read_text(encoding="utf-8")
            self.assertNotIn(r"C:\Users", src, f)
            self.assertNotIn(".codex", src, f)


if __name__ == "__main__":
    unittest.main()
