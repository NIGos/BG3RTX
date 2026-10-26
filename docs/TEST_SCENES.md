# Test scenes

The player's saves cover every act, so they are the multi-scene test suite for
the review checkpoints in `QUALITY.md`. `scripts/save_index.py` lists all saves
with their level (from `SaveInfo.json` inside each `.lsv`); the save thumbnails
confirm the view and the time of day.

Load one with `scripts/auto-run.ps1 -Save "<folder substring>"`.

`bg3.exe` has an undocumented `-loadSaveGame <save folder name>` argument
(found next to `-continueGame` in the executable's strings; "GameStateInit:
These commands should be mutually exclusive"). `auto-run.ps1 -Save` passes the
matching folder name. Neither the full `.lsv` path nor file timestamps work:
`-continueGame` picks the newest save by the save time stored inside each
package.

`layer/savefilter.cpp` (env `BG3RTX_SAVE_ONLY`) can also hide every other save
from the game's listing of `Savegames\Story`. It works, but `-continueGame`
then stays in the main menu, so it is kept only as a tool. Nothing on disk is
ever moved for a test. That matters because Steam Cloud syncs that folder
(`remotecache.vdf` lists every save), and moving saves out, even temporarily,
could look like a deletion to it.

Runs with `-Save` set `auto_after_load=1`: the trace waits for a loading screen
after the 3D main menu, since other scenes draw about as much as the menu.
`scripts/scene-sweep.ps1` runs every scene, two at a time (see below), and writes
`captures/scene-sweep.json`. `-Exclusive` runs them one at a time.

Saves this run's game writes (the layer records the folders it creates,
`BG3RTX_SAVE_LOG`, `layer/savefilter.cpp`) are parked in `Savegames\_bg3rtx_parked`
instead of joining the player's list. Saves of any other game, a concurrent test
run's or the player's, are never moved; new folders not written by this run's game
are only listed in its output. Save content is never modified. A full copy of the player's profile and saves, taken
2026-10-26 before the first `-Save` run, is in
`D:\BG3_Save_Backup\20261026_PlayerProfiles`. It has 227 files and 2.12 GB, and
the newest save's hash matches the original. To restore, copy it back over
`%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles` with the game
closed.

| Id | Save (substring for `-Save`) | Level | Lighting case | What it stresses |
|---|---|---|---|---|
| S1 | `QuickSave_3784` (Steel Watch Foundry) | CTY_Main_A | interior, many point/spot lights | local-light shadows (D-LS), the default `-continueGame` scene |
| S2 | `Rivington - 74h` | BGO_Main_A | outdoor day, hard sun | sun shadows and penumbra, cobblestone GI, crowds (skinned characters) |
| S3 | `Campsite - 27h` | WLD_Main_A | outdoor day, trees and grass | alpha-tested foliage, back-lit translucency, alpha-buffer capacity |
| S4 | `Goblin Camp - 18h` | WLD_Main_A | outdoor overcast, dense vegetation, fog | foliage shadows, fog over GI |
| S5 | `Grymforge - 32h 59m` | WLD_Main_A | underground, lava and torches | emissive lava (D-EM), local lights, GI colour bleed |
| S6 | `Adamantine Forge` | WLD_Main_A | huge lava pool, metal machinery | emissive bounce, metal reflections (D-RF) |
| S7 | `The Blushing Mermaid - 96h` | CTY_Main_A | tavern interior, candles, cut-away roof | local shadows, casters hidden by the roof cut-away |
| S8 | `Water Queens House` | CTY_Main_A | marble temple, water, columns | reflections on water and polished stone (D-RF) |
| S9 | `Crèche Yllek` | CRE_Main_A | gilded interior | glossy metal reflections, warm GI |
| S10 | `Shadow-Cursed Lands - 52h` | SCL_Main_A | darkness, magic light shafts, fog | low light, local and magic lights, noise in the dark |
| S11 | `Gauntlet of Shar - 57h` | SCL_Main_A | dark, purple lights, polished stone | reflections in the dark, local lights |
| S12 | `Moonrise Towers Main Floor - 65h` | SCL_Main_A | dark interior, torches | local shadows, GI at low light |
| S13 | `Mind Flayer Colony` | SCL_Main_A | organic, emissive pods | emissive surfaces, wet-looking materials |
| S14 | `Lower City - 91h` | CTY_Main_A | city day, stairs and walls | sun shadows on architecture, large view distance |

Checkpoint routine: for each scene, a feature-off and a feature-on capture of
the same frame (`scripts/live_phases.py` toggles live inside one run), the A/B
sheet from `scripts/ab_compare.py`, GPU costs, and the non-finite and
validation checks. `python -m scripts.qa run --scenes ...` automates the
captures and the objective checks ([QA.md](QA.md)). The art-intent review looks at each A/B sheet. It asks
whether the change keeps the look the artists lit for (key lights, character
readability, colour grade) as well as being physically better.
