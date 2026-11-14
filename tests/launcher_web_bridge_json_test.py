"""Independently parse the C++ bridge's JSON with the standard library."""
import json
import re
import subprocess
import sys

state = json.loads(subprocess.check_output([sys.argv[1], "--json"], encoding="utf-8"))
assert state["type"] == "state"
assert len(state["settings"]) > 0
assert "\n" in state["status"] and "\t" in state["status"] and '"' in state["status"]
assert isinstance(state["compatibility"]["adapters"], list)
assert isinstance(state["report"]["files"], list)
assert isinstance(state["frameGeneration"], str)
print(f"Parsed {len(state['settings'])} settings; JSON control characters retained.")

# No package beside the test binary: the base version of tools/launcher/version.h, marked as a developer build.
assert re.fullmatch(r"\d+\.\d+\.\d+\.\d+ \(developer build\)", state["version"]), state["version"]
assert state["modStatus"] == "Not checked"
for setting in state["settings"]:
    assert "Unknown" not in setting["cost"]
    assert "Unknown" not in setting["vram"]
far = next(s for s in state["settings"] if s["id"] == "exp_hidden_surface")
assert "RTX 5090" in far["provenance"] and "Estimated cost" in far["provenance"]
print("Version, unchecked status and A1 cost provenance retained; unknown cost pills omitted.")

assert state["restartCount"] == 0 and state["restartBanner"] == ""
for setting in state["settings"]:
    assert setting["apply"] in {"Applies after saving", "Applies after saving; may briefly pause", "Restart the game to apply"}, setting["id"]
    for choice in setting["choices"]:
        assert choice["apply"] in {"Applies after saving", "Applies after saving; may briefly pause", "Restart the game to apply"}
print("Every card and choice has an explicit apply badge; no pending restart at baseline.")

assert far["title"] == "Off-screen bounce light"
assert [c["label"] for c in far["choices"]] == ["Off", "Near", "Far (6 m)"]
sun = next(s for s in state["settings"] if s["id"] == "sun_shadows")
assert sun["choices"][2]["label"] == "Include hidden roofs"
box = next(s for s in state["settings"] if s["id"] == "box_lights")
assert box["maturity"] == "qualified" and not box["experimental"]
assert box["value"] == 0
assert "0.13 ms" in box["description"]
assert [c["label"] for c in box["choices"]] == ["Off", "On"]
assert all(c["enabled"] and c["maturity"] == "qualified" for c in box["choices"])
assert "0.13 ms" in box["cost"]
assert not any(s["id"] in {"roof_shadows", "exp_hit_emission", "exp_gi_far", "exp_hair_physical_origin"} for s in state["settings"])
assert not any(s["id"] in {"exp_omm", "exp_vt_alpha_masks", "rr_post"} for s in state["settings"])
local = next(s for s in state["settings"] if s["id"] == "local_shadows")
assert [c["label"] for c in local["choices"]] == ["Off", "Fast", "Full"]
print("Consolidated cards, qualified box shadows and developer visibility verified.")

expected = {
 "Lighting": ["DLSS Ray Reconstruction", "Ray-tracing detail", "Sun shadows", "Indirect lighting", "Local light shadows", "Mod lighting strength", "Off-screen bounce light", "Interior light shadows", "Hair shadows",
              "RTX Hair",   # exp_hair_groom_primary: a release Lighting control since 0.9.5 (user request)
              "RTX Hair detail at distance",   # hair_distant_detail (user request, 2026-11-10)
              "RTX Hair motion stability"],   # hair_motion_stability (user request, 2026-11-10)
 "Surfaces": ["Reflections", "Water lighting", "Depth of field and bloom smoothing"],
 "Performance": ["Frame Generation", "Rendered frame limit", "NVIDIA Reflex", "Faster bounce light tracing", "Bounce trace reordering", "Low-latency scene"],   # gi_rt_pipeline, qualified 2026-11-08
 "Support": ["Detailed logging"],
 "DLSS 5": ["DLSS 5 Neural Rendering (experimental)"],
}
assert len(state["settings"]) == 23   # + scene_handout (Low-latency scene, 2026-11-14); 22 = 17 + exp_gi_rt_pipeline + reorder (2026-11-08) + RTX Hair (0.9.5) + its distance detail and motion stability
for page, labels in expected.items():
    assert {s["title"] for s in state["settings"] if s["category"] == page} == set(labels)
assert not state["developer"] and not state["experimental"]
assert all(not s["developer"] and (not s["experimental"] or s["id"]=="dlss5_nr") for s in state["settings"])
print("Exact release pages and 16 controls verified.")

choices = {
 "dlss5_nr": ["Off", "On"], "detailed_logging": ["Off", "On"], "rr": ["Off", "On"], "rt_geometry": ["Off", "Solid surfaces", "Include foliage"],
 "sun_shadows": ["Visible surfaces", "Ray-traced", "Include hidden roofs"],
 "gi": ["Off", "Visible surfaces", "Ray-traced", "Ray-traced high"],
 "local_shadows": ["Off", "Fast", "Full"], "exp_hidden_surface": ["Off", "Near", "Far (6 m)"],
 "box_lights": ["Off", "On"], "rr_post_temporal": ["Off", "On"],
 "exp_native_coverage": ["Off", "On"],
 "exp_hair_groom_primary": ["Off", "On"],   # RTX Hair (0.9.5)
 "hair_distant_detail": ["Off", "1/4 (Standard)", "1/8 (Sharper)"],
 "hair_motion_stability": ["Off", "On"],
 "reflections": ["Off", "Glossy", "Standard", "Rough"],
 "exp_water": ["Off", "Reflections", "Reflections and shadows", "Include refraction"],
 "frame_generation": ["Off", "2x", "3x", "4x"], "reflex": ["Off", "On", "On + Boost"],
 "exp_gi_rt_pipeline": ["Off", "On"],
 "scene_handout": ["Off", "On"],
 "exp_gi_rt_pipeline_reorder": ["Off", "By hit kind", "By hit kind and object"],
}
for setting in state["settings"]:
    if setting["id"] in choices:
        assert [c["label"] for c in setting["choices"]] == choices[setting["id"]]
    else:
        assert setting["id"] in {"rt_intensity", "fps_cap"}
        bounds = (50, 100, 5, False, "%") if setting["id"] == "rt_intensity" else (30, 240, 1, True, "fps")
        assert tuple(setting[k] for k in ("min", "max", "step", "hasOff", "unit")) == bounds

# The page's plain-browser sample (tools/launcher/ui/app.js, no WebView host) repeats table copy: it must be the
# table's. It had drifted (old descriptions, RTX Hair still marked "restart") while the real cards moved on.
import pathlib
page = (pathlib.Path(__file__).resolve().parents[1] / "tools/launcher/ui/app.js").read_text(encoding="utf-8")
block = page[page.index(" const settings=["):page.index("].filter(x=>x[2]!=='Experimental'")]
rows = re.findall(r"""\['([a-z0-9_]+)',('[^']*'|"[^"]*"),'([^']*)',\[([^\]]*)\],-?\d+,('[^']*'|"[^"]*")\]""", block)
# The sample also lists the opt-in options (shown with ?experimental); compare with the opt-in state.
state_opt_in = json.loads(subprocess.check_output([sys.argv[1], "--json", "experimental"], encoding="utf-8"))
assert {r[0] for r in rows if r[2] != "Experimental"} == {s["id"] for s in state["settings"]}
by_id = {s["id"]: s for s in state_opt_in["settings"]}
assert len(rows) == len(by_id) and {r[0] for r in rows} == set(by_id), sorted(set(by_id) ^ {r[0] for r in rows})
for ident, title, category, levels, description in rows:
    real = by_id[ident]
    assert title[1:-1] == real["title"] and category == real["category"] and description[1:-1] == real["description"], ident
    assert re.findall(r"""'([^']*)'|"([^"]*)\"""", levels) == [(c["label"], "") if "'" not in c["label"] else ("", c["label"]) for c in real["choices"]], ident
print("Sample data of the page matches the settings table.")
