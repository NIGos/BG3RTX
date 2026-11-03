"""Find the game's Data folder and LSLib's Tools folder on this machine (offline tools only).

No path of one developer's machine is hard-coded: the offline extractors (vt_alpha_masks, vt_alpha_catalogue) must
run on a player's machine at install time (docs/GAME_DATA_AUDIT.md 4: Larian-derived data is generated locally).

game_data():  $BG3_DATA, else <Steam library>/steamapps/common/Baldurs Gate 3/Data for every Steam library listed
              in libraryfolders.vdf (Steam path from $STEAM_PATH or the registry), else "".
lslib_tools(): $BG3RTX_LSLIB (the Tools folder that holds LSLib.dll, or a Packed folder with Tools/ inside), else
              the first LSLib.dll under <repo>/external/lslib*/ or <main checkout>/external/lslib*/, else "".
An empty result means "not found": the caller asks for --game / --lslib (never a guessed default).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME_SUBDIR = Path("steamapps") / "common" / "Baldurs Gate 3" / "Data"


def _steam_root():
    env = os.environ.get("STEAM_PATH")
    if env:
        return Path(env)
    try:
        import winreg
        for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                          (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    for name in ("SteamPath", "InstallPath"):
                        try:
                            return Path(winreg.QueryValueEx(k, name)[0])
                        except OSError:
                            pass
            except OSError:
                pass
    except ImportError:
        pass
    return None


def steam_libraries(vdf_text):
    """Library roots named by a libraryfolders.vdf text ("path" entries, Steam's escaped backslashes)."""
    return [Path(m.replace("\\\\", "\\")) for m in re.findall(r'"path"\s+"([^"]+)"', vdf_text)]


def game_data():
    env = os.environ.get("BG3_DATA")
    if env:
        return env if Path(env).is_dir() else ""
    root = _steam_root()
    if root is None:
        return ""
    libs = [root]
    vdf = root / "steamapps" / "libraryfolders.vdf"
    if vdf.is_file():
        libs += steam_libraries(vdf.read_text(encoding="utf-8", errors="replace"))
    for lib in libs:
        d = lib / GAME_SUBDIR
        if d.is_dir():
            return str(d)
    return ""


def _tools_dir(p):
    p = Path(p)
    for c in (p, p / "Tools", p / "Packed" / "Tools"):
        if (c / "LSLib.dll").is_file():
            return str(c)
    return ""


def _main_checkout():
    # A worktree's .git is a file "gitdir: <main>/.git/worktrees/<name>".
    g = ROOT / ".git"
    if g.is_file():
        m = re.match(r"gitdir:\s*(.+)", g.read_text().strip())
        if m:
            return Path(m.group(1)).resolve().parents[2]
    return ROOT


def lslib_tools():
    env = os.environ.get("BG3RTX_LSLIB")
    if env:
        return _tools_dir(env)
    for base in dict.fromkeys((ROOT, _main_checkout())):
        ext = base / "external"
        if ext.is_dir():
            for d in sorted(ext.glob("lslib*")):
                t = _tools_dir(d)
                if t:
                    return t
    return ""
