"""Per-scene native ambient table (tools/ambient_table.py) on report lines in frame_replay's wording.

    py -3 tests/ambient_table_test.py
"""
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ambient_table as at  # noqa: E402

FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name} {info}")
    if not ok:
        FAILS.append(name)


# The lines tools/frame_replay.cpp native_ambient_report writes (format strings filled in by hand).
REPORT = """native ambient: PT_SUN_LUT=1 PT_ENV=1 PT_CASTERS=1 (the pack's cascade casters: 759945 triangles) PT iterations 32; 2560x1067 GI pixels, 2731520 compared (live giRawDepth > 0); lighting inputs 37 images, 24 probe cubes, probe records yes, LUT sun yes, sky cube yes
native ambient means (luminance, the giRaw.rgb quantity, game units): E_native 0.53833 (sky irradiance only 0.696094), x game AO 0.44278 (AO 0.8205), x game AO x live a 0.404728 (live a 0.8790); froxels listing a local probe 93.5%; as lit (x diffuse colour x AO) 0.0998928, specular ambient 0.0204111
PT far field (total minus live domain, first hit beyond gi_rt_radius 1 m or faded): 0.00533 PT units (first hits beyond it on screen 0.0465454, off screen 0.0331913); k = 15.200; k x PT far 0.081; PT environment through misses 0.226 (first vertex 0.2; first-ray miss fraction 0.2000)
PT split (compared pixels, % of PT total): far field on screen beyond gi_rt_radius 53.6% (visible 15.2, hidden 38.3), off screen 35.8%, live domain 8.4%; sun 70.3%, local point/spot 29.7%
native vs PT far field: E_native x AO / (k x PT far) 5.466, E_native x AO / (k x PT far + PT env) 1.442, E_native / (k x PT far + PT env) 1.753, E_native x AO x live a / (k x PT far + PT env) 1.318
per tile (16 x 9, 144 with pixels): E_native x AO / (k x PT far + PT env) p10 0.900 median 1.750 p90 3.010, pixel-weighted mean |native - ref| / ref 0.900; / (k x PT far) p10 2.669 median 6.186 p90 21.929
sun transmittance: game LUT 0.8892 0.7837 0.5723 (atmosphere path on 100% of pixels), Kasten-Young estimate 0.7258 0.6287 0.4531; cloud shadow mean 0.4700
"""
r = at.parse(REPORT)
check("ratio", abs(r.get("ratio", 0) - 1.442) < 1e-9, str(r.get("ratio")))
check("E x AO, k, k x PT far, env", r.get("eao") == 0.44278 and r.get("k") == 15.2 and r.get("kfar") == 0.081 and r.get("env") == 0.226)
check("tiles p10 / median / p90", r.get("tiles") == (0.9, 1.75, 3.01), str(r.get("tiles")))
check("split: far on screen, off screen, sun / local", r.get("split") == (53.6, 15.2, 38.3, 35.8, 8.4, 70.3, 29.7), str(r.get("split")))
check("roofs from the parameters", r.get("roofs") == "casters")
row = at.row("camp", r)
check("row", row == "| camp | 0.443 | 0.081 | 0.226 | 1.44 | 0.90 / 1.75 / 3.01 | 53.6% + 35.8% off (89.4%) | 70 / 30 | 15.20 | casters |", row)

old = "\n".join(l for l in REPORT.splitlines() if not l.startswith("PT split")).replace("PT_CASTERS=1 (the pack's cascade casters: 759945 triangles) ", "")
ro = at.parse(old)
check("a report without the split line: n/a, no roofs", "split" not in ro and ro.get("roofs") == "none" and "| n/a | n/a |" in at.row("s8", ro))

with tempfile.TemporaryDirectory() as d:
    pack = os.path.join(d, "replay-20261101-142819-2")
    os.makedirs(os.path.join(pack, "replay"))
    Path(pack, "replay", "native_ambient_report.txt").write_text(REPORT, encoding="utf-8")
    rep = at.find_report(pack)
    check("find_report: <pack>/replay", rep == os.path.join(pack, "replay", "native_ambient_report.txt"))
    check("scene: the folder name without scene.txt", at.scene_of(pack, rep) == "replay-20261101-142819-2")
    Path(pack, "scene.txt").write_text("inn\n", encoding="utf-8")
    check("scene: scene.txt (post_replay.ps1 -Scene)", at.scene_of(pack, rep) == "inn")
    check("main: exit 0 with a report, 1 when none", at.main([pack]) == 0 and at.main([os.path.join(d, "none")]) == 1)

print(f"{'FAIL' if FAILS else 'PASS'}: {len(FAILS)} failure(s)")
sys.exit(1 if FAILS else 0)
