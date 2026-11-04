"""Independently parse the C++ bridge's JSON with the standard library."""
import json
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

assert state["version"] == "0.9.0.0"
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
 "Lighting": ["DLSS Ray Reconstruction", "Ray-tracing detail", "Sun shadows", "Indirect lighting", "Local light shadows", "Mod lighting strength", "Off-screen bounce light", "Interior light shadows", "Hair shadows"],
 "Surfaces": ["Reflections", "Water lighting", "Depth of field and bloom smoothing"],
 "Performance": ["Frame Generation", "Rendered frame limit", "NVIDIA Reflex"],
 "Support": ["Detailed logging"],
}
assert len(state["settings"]) == 16
for page, labels in expected.items():
    assert {s["title"] for s in state["settings"] if s["category"] == page} == set(labels)
assert not state["developer"] and not state["experimental"]
assert all(not s["developer"] and not s["experimental"] for s in state["settings"])
print("Exact release pages and 16 controls verified.")

choices = {
 "detailed_logging": ["Off", "On"], "rr": ["Off", "On"], "rt_geometry": ["Off", "Solid surfaces", "Include foliage"],
 "sun_shadows": ["Visible surfaces", "Ray-traced", "Include hidden roofs"],
 "gi": ["Off", "Visible surfaces", "Ray-traced", "Ray-traced high"],
 "local_shadows": ["Off", "Fast", "Full"], "exp_hidden_surface": ["Off", "Near", "Far (6 m)"],
 "box_lights": ["Off", "On"], "rr_post_temporal": ["Off", "On"],
 "exp_native_coverage": ["Off", "On"],
 "reflections": ["Off", "Glossy", "Standard", "Rough"],
 "exp_water": ["Off", "Reflections", "Reflections and shadows", "Include refraction"],
 "frame_generation": ["Off", "2x", "3x", "4x"], "reflex": ["Off", "On", "On + Boost"],
}
for setting in state["settings"]:
    if setting["id"] in choices:
        assert [c["label"] for c in setting["choices"]] == choices[setting["id"]]
    else:
        assert setting["id"] in {"rt_intensity", "fps_cap"}
        bounds = (50, 100, 5, False, "%") if setting["id"] == "rt_intensity" else (30, 240, 1, True, "fps")
        assert tuple(setting[k] for k in ("min", "max", "step", "hasOff", "unit")) == bounds
