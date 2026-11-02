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
