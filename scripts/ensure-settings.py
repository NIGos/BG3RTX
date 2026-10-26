"""Puts BG3's graphicSettings.lsx back to the configuration the project is
mapped against: DLSS (ResolutionUpscaleType=3) in Quality mode (DLSS2Mode=2).

The game rewrites this file from defaults when it decides an upscaler is
unavailable at startup (for example while an experimental NGX hook is
loaded), which silently changes the render resolution of every later run.
Run before each launch; it only touches the two entries and keeps a backup.

Extra entries for one run (for example borderless for a background run):
  ensure-settings.py --set Fullscreen=0 FakeFullscreenEnabled=1 --prev prev.json
  ensure-settings.py --restore prev.json     (puts the player's values back)
"""
import json
import re
import shutil
import sys
from pathlib import Path

PATH = Path.home() / "AppData/Local/Larian Studios/Baldur's Gate 3/graphicSettings.lsx"
WANT = {"DLSS2Mode": 2, "ResolutionUpscaleType": 3}

ENTRY = """                <node id="ConfigEntry">
                    <attribute id="MapKey" type="FixedString" value="{key}"/>
                    <attribute id="Type" type="int32" value="0"/>
                    <attribute id="Value" type="int32" value="{value}"/>
                </node>
"""


def apply(text, want, changed, prev=None):
    for key, value in want.items():
        pat = re.compile(r'(value="%s"/>\s*<attribute id="Type" type="int32" value="0"/>\s*'
                         r'<attribute id="Value" type="int32" value=")(-?\d+)(")' % key)
        m = pat.search(text)
        if prev is not None:
            prev[key] = int(m.group(2)) if m else None
        if m:
            if int(m.group(2)) != value:
                text = text[:m.start(2)] + str(value) + text[m.end(2):]
                changed.append(f"{key} {m.group(2)}->{value}")
        else:
            text = text.replace("            <children>\n", "            <children>\n" + ENTRY.format(key=key, value=value), 1)
            changed.append(f"{key} added={value}")
    return text


def main():
    if not PATH.exists():
        sys.exit(f"{PATH} not found")
    args = sys.argv[1:]
    text = PATH.read_text(encoding="utf-8")
    changed = []
    want = dict(WANT)
    prev, prev_path = None, None
    if args[:1] == ["--restore"]:
        saved = json.loads(Path(args[1]).read_text())
        want = {k: v for k, v in saved.items() if v is not None}
    elif "--set" in args:
        i = args.index("--set") + 1
        while i < len(args) and not args[i].startswith("--"):
            k, v = args[i].split("=")
            want[k] = int(v)
            i += 1
        if "--prev" in args:
            prev_path = Path(args[args.index("--prev") + 1])
            prev = {}
    text = apply(text, want, changed, prev)
    if prev_path:
        prev_path.write_text(json.dumps({k: v for k, v in prev.items() if k not in WANT}))
    if changed:
        shutil.copyfile(PATH, PATH.with_suffix(".lsx.bg3rtx-bak"))
        PATH.write_text(text, encoding="utf-8")
        print("graphicSettings.lsx: " + ", ".join(changed))
    else:
        print("graphicSettings.lsx: ok")


if __name__ == "__main__":
    main()
