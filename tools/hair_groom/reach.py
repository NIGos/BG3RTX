"""Where every converted hairstyle is used in the game data (offline, read-only paks; never the game).

python -m tools.hair_groom.reach [--state captures/hair_groom_batch/state.json] [--out <report.json>]

For each style of the batch (its VisualBank resource ID) the references found in the installed paks:
- cc: CharacterCreationAppearanceVisuals / CharacterCreationSharedVisuals (player and character-creation presets;
  slot Hair, Beard, Horns, ...).
- cv: slots of CharacterVisualResources ([PAK]_CharacterVisuals), and which character templates use each
  resource (CharacterVisualResourceID in RootTemplates and every level's Characters/_merged.lsf). A template named
  S_Player_* or an origin preset is a companion; another S_* template a named NPC; others generic NPCs.
- hat: an item's Equipment hair-type map (Equipment > <HairType> > Object MapValue): the hair drawn under that
  hat or helmet.
- item: an item or scenery template (corpse, statue, hat) whose VisualTemplate is the visual itself.
Category of a style: the first of player, companion, named NPC, generic NPC, hat variant, item, CV only (a
character visual no template references), unused (checked: no pak file outside its own visual bank names it). Read-only; prints progress per file group.
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

from ..assets import lsf, lsx
from ..assets.locate import game_data
from ..assets.lspk import PackageSet

ROOT = Path(__file__).resolve().parents[2]
CATEGORIES = ("player", "companion", "named NPC", "generic NPC", "hat variant", "item", "CV only", "unused")


def load(ps, name):
    raw = ps.read(name)
    return lsx.parse(raw) if name.endswith(".lsx") else lsf.parse(raw)


def references(ps, visuals, log=print):
    """{visual: {"cc": [(file, slot)], "cv": [(cv_id, cv_name, slot)], "hat": [(item, hair_type)], "item": [name]}},
    {cv_id: [template names]}."""
    refs = {v: collections.defaultdict(list) for v in visuals}
    users = collections.defaultdict(set)
    names = list(ps.index)
    cc = [n for n in names if "/CharacterCreation/CharacterCreation" in n and n.endswith("Visuals.lsx")]
    cvf = [n for n in names if "[PAK]_CharacterVisuals/_merged.lsf" in n]
    tpl = [n for n in names if n.endswith("RootTemplates/_merged.lsf") or n.endswith("/Characters/_merged.lsf")]
    log(f"[hair reach] {len(cc)} character-creation, {len(cvf)} character-visual, {len(tpl)} template files")
    for n in cc:
        for reg in load(ps, n).regions:
            for node in reg.walk():
                v = str(node.get("VisualResource") or "").lower()
                if v in refs:
                    refs[v]["cc"].append((n, node.get("SlotName")))
    for n in cvf:
        for reg in load(ps, n).regions:
            for res in reg.find_all("Resource"):
                for s in res.children_named("Slots"):
                    v = str(s.get("VisualResource") or "").lower()
                    if v in refs:
                        refs[v]["cv"].append((str(res.get("ID")).lower(), res.get("Name"), s.get("Slot")))
    log("[hair reach] character visuals read")
    for i, n in enumerate(tpl):
        for reg in load(ps, n).regions:
            for go in reg.find_all("GameObjects"):
                name = go.get("Name") or ""
                cv = str(go.get("CharacterVisualResourceID") or "").lower()
                if cv:
                    users[cv].add(name)
                vt = str(go.get("VisualTemplate") or "").lower()
                if vt in refs:
                    refs[vt]["item"].append(name)
                eq = go.child("Equipment")
                if eq is None:
                    continue
                for hair_type in eq.children:
                    for obj in hair_type.walk():
                        v = str(obj.get("MapValue") or obj.get("Object") or "").lower()
                        if v in refs:
                            refs[v]["hat"].append((name, hair_type.name))
        if (i + 1) % 10 == 0:
            log(f"[hair reach] templates {i + 1}/{len(tpl)}")
    return refs, users


def category(r, users):
    if r["cc"]:
        return "player"
    tnames = {t for cv, _, _ in r["cv"] for t in users.get(cv, ())}
    if any(t.startswith("S_Player_") or "Origin" in t for t in tnames):
        return "companion"
    if any(t.startswith("S_") for t in tnames):
        return "named NPC"
    if tnames:
        return "generic NPC"
    if r["hat"]:
        return "hat variant"
    if r["item"]:
        return "item"
    if r["cv"]:
        return "CV only"
    return "unused"


def slot(r, name):
    slots = collections.Counter(s for _, s in r["cc"]) + collections.Counter(s for _, _, s in r["cv"])
    if slots:
        return slots.most_common(1)[0][0]
    if r["hat"]:
        return "Hair"
    return "Beard" if name.upper().startswith("BEARD") else "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--state", type=Path, default=ROOT.parents[2] / "captures" / "hair_groom_batch" / "state.json")
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    state = json.loads(a.state.read_text(encoding="utf-8"))
    ps = PackageSet(game_data())
    refs, users = references(ps, {str(r["visual"]).lower() for r in state.values()})
    passed0 = {r["gr2"] for r in state.values() if r["verdict"] == "pass" and r["lod"] == 0}
    rows = []
    for r in state.values():
        ref = refs[str(r["visual"]).lower()]
        rows.append({"name": r["name"], "verdict": r["verdict"], "lod": r["lod"], "category": category(ref, users),
                     "slot": slot(ref, r["name"]), "lod1_of_passed": r["lod"] == 1 and r["gr2"] in passed0,
                     "templates": sorted({t for cv, _, _ in ref["cv"] for t in users.get(cv, ())})[:8],
                     "hats": sorted({h for h, _ in ref["hat"]})[:4], "reasons": r.get("reasons", [])})
    table = collections.Counter((x["category"], x["verdict"]) for x in rows)
    out = {"styles": len(rows), "table": {f"{c}|{v}": n for (c, v), n in sorted(table.items())}, "rows": rows}
    if a.out:
        a.out.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    for c in CATEGORIES:
        print(f"{c:12s}", {v: table[(c, v)] for v in ("pass", "needs-review", "reject")})


if __name__ == "__main__":
    main()
