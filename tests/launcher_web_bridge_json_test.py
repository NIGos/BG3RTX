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
assert "RTX 5090" in far["provenance"] and "+3.2-3.7 ms" in far["provenance"]
print("Version, unchecked status and A1 cost provenance retained; unknown cost pills omitted.")

assert state["restartCount"] == 0 and state["restartBanner"] == ""
for setting in state["settings"]:
    assert setting["apply"] in {"Live", "Applies after a short hitch", "Restart to apply"}, setting["id"]
    for choice in setting["choices"]:
        assert choice["apply"] in {"Live", "Applies after a short hitch", "Restart to apply"}
print("Every card and choice has an explicit apply badge; no pending restart at baseline.")

assert far["title"] == "Off-screen bounce light"
assert [c["label"] for c in far["choices"]] == ["Off", "Near", "Far (6 m)", "Far + reflections"]
sun = next(s for s in state["settings"] if s["id"] == "sun_shadows")
assert sun["choices"][2]["label"] == "Ray-traced + hidden roofs"
box = next(s for s in state["settings"] if s["id"] == "box_lights")
assert box["maturity"] == "qualified" and not box["experimental"]
assert box["value"] == 0
assert "ba1 S6 Grymforge 2026-11-03 08:02 valid, user approved" in box["provenance"]
assert [c["label"] for c in box["choices"]] == ["Off", "Shadowed", "All"]
assert all(c["enabled"] and c["maturity"] == "qualified" for c in box["choices"])
assert "+0.13 ms S6" in box["cost"]
assert not any(s["id"] in {"roof_shadows", "exp_hit_emission", "exp_gi_far", "exp_alpha_spill", "exp_hair_physical_origin"} for s in state["settings"])
omm = next(s for s in state["settings"] if s["id"] == "exp_omm")
masks = next(s for s in state["settings"] if s["id"] == "exp_vt_alpha_masks")
assert omm["maturity"] == masks["maturity"] == "qualified"
assert not omm["experimental"] and not masks["experimental"]
assert omm["apply"] == "Restart to apply" and "RTX 5090" in omm["provenance"]
local = next(s for s in state["settings"] if s["id"] == "local_shadows")
assert [c["label"] for c in local["choices"]] == ["Off", "Fast", "Full"]
print("Consolidated cards, qualified box shadows and developer visibility verified.")
assert not far["choices"][3]["enabled"] and far["choices"][3]["maturity"] == "experimental"
