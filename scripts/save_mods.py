"""List the mods each BG3 save was made with (meta.lsf, ModuleSettings), read-only.

A save made with mods that are not installed now makes the game show a
"missing mods" dialog when it loads, which a background test run has to
accept (auto-run.ps1 -AcceptModWarning, docs/TEST_SCENES.md). This lists, per
save, the modules beyond the game's own, and which of them are not installed
(not in Mods\\ or the modsettings.lsx of the player's profile).

  python scripts/save_mods.py                    every save
  python scripts/save_mods.py "Rivington - 74h"  saves whose folder contains this
  python scripts/save_mods.py --json             machine-readable (auto-run.ps1)

Saves are only read, never modified.
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.assets import lspk, lsf  # noqa: E402

LOCAL = os.path.expandvars(r"%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3")
STORY = os.path.join(LOCAL, r"PlayerProfiles\Public\Savegames\Story")
MODSETTINGS = os.path.join(LOCAL, r"PlayerProfiles\Public\modsettings.lsx")
# The game's own modules (Gustav/GustavX, Shared, the UI and dice-set modules, Honour).
BASE_PREFIX = ("Gustav", "Shared", "MainUI", "ModBrowser", "Honour", "DiceSet_", "FW3", "Engine", "Game")


def _s(v):
    return v[0] if isinstance(v, tuple) else v


def save_mods(folder):
    lsv = [x for x in os.listdir(folder) if x.lower().endswith(".lsv")]
    if not lsv:
        return None
    res = lsf.parse(lspk.Package(os.path.join(folder, lsv[0])).read("meta.lsf"))
    mods = []
    for n in res.find_all("ModuleShortDesc"):
        mods.append({"name": str(_s(n.get("Name", ""))), "uuid": str(_s(n.get("UUID", ""))),
                     "folder": str(_s(n.get("Folder", "")))})
    return mods


def installed():
    """Module names and UUIDs the game will load now: modsettings.lsx + Mods\\*.pak names."""
    names, uuids = set(), set()
    try:
        import re
        txt = open(MODSETTINGS, encoding="utf-8", errors="replace").read()
        uuids |= set(re.findall(r'id="UUID"[^>]*value="([0-9a-fA-F-]{36})"', txt))
        names |= set(re.findall(r'id="Name"[^>]*value="([^"]+)"', txt))
        names |= set(re.findall(r'id="Folder"[^>]*value="([^"]+)"', txt))
    except OSError:
        pass
    mods_dir = os.path.join(LOCAL, "Mods")
    if os.path.isdir(mods_dir):
        names |= {os.path.splitext(f)[0] for f in os.listdir(mods_dir) if f.lower().endswith(".pak")}
    return names, uuids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("match", nargs="?", default="")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    names, uuids = installed()
    out = []
    for d in sorted(os.listdir(STORY)):
        if a.match and a.match not in d:
            continue
        full = os.path.join(STORY, d)
        if not os.path.isdir(full):
            continue
        try:
            mods = save_mods(full)
        except Exception as e:  # a damaged or foreign package: report, never touch
            out.append({"folder": d, "error": str(e)})
            continue
        if mods is None:
            continue
        extra = [m for m in mods if not m["name"].startswith(BASE_PREFIX)]
        missing = [m for m in extra if m["uuid"] not in uuids and m["name"] not in names and m["folder"] not in names]
        out.append({"folder": d, "mods": len(mods), "extra": [m["name"] for m in extra],
                    "missing": [m["name"] for m in missing]})
    if a.json:
        json.dump(out, sys.stdout, ensure_ascii=False)
        return
    for r in out:
        if "error" in r:
            print(f"{r['folder']}: ERROR {r['error']}")
            continue
        flag = "MISSING " + ", ".join(r["missing"]) if r["missing"] else "ok"
        print(f"{r['folder'].split('__', 1)[-1][:48]:48} {len(r['extra']):2} extra  {flag}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
