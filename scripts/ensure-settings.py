"""Puts BG3's graphicSettings.lsx back to the configuration the project is
mapped against: DLSS (ResolutionUpscaleType=3) in Quality mode (DLSS2Mode=2).

The game rewrites this file from defaults when it decides an upscaler is
unavailable at startup (for example while an experimental NGX hook is
loaded), which silently changes the render resolution of every later run.
Run before each launch; it only touches the two entries and keeps a backup.
"""
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


def main():
    if not PATH.exists():
        sys.exit(f"{PATH} not found")
    text = PATH.read_text(encoding="utf-8")
    changed = []
    for key, value in WANT.items():
        pat = re.compile(r'(value="%s"/>\s*<attribute id="Type" type="int32" value="0"/>\s*'
                         r'<attribute id="Value" type="int32" value=")(-?\d+)(")' % key)
        m = pat.search(text)
        if m:
            if int(m.group(2)) != value:
                text = text[:m.start(2)] + str(value) + text[m.end(2):]
                changed.append(f"{key} {m.group(2)}->{value}")
        else:
            text = text.replace("            <children>\n", "            <children>\n" + ENTRY.format(key=key, value=value), 1)
            changed.append(f"{key} added={value}")
    if changed:
        shutil.copyfile(PATH, PATH.with_suffix(".lsx.bg3rtx-bak"))
        PATH.write_text(text, encoding="utf-8")
        print("graphicSettings.lsx: " + ", ".join(changed))
    else:
        print("graphicSettings.lsx: ok")


if __name__ == "__main__":
    main()
