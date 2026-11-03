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
far = next(s for s in state["settings"] if "+3.2-3.7 ms" in s["cost"])
assert "RTX 5090" in far["provenance"] and "6 m" in far["provenance"]
print("Version, unchecked status and A1 cost provenance retained; unknown cost pills omitted.")

assert state["restartCount"] == 0 and state["restartBanner"] == ""
for setting in state["settings"]:
    assert setting["apply"] in {"Live", "Applies after a short hitch", "Restart to apply"}, setting["id"]
    for choice in setting["choices"]:
        assert choice["apply"] in {"Live", "Applies after a short hitch", "Restart to apply"}
print("Every card and choice has an explicit apply badge; no pending restart at baseline.")
