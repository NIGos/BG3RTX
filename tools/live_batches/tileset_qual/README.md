# ts1-native-tileset

Prepared offline for the 2026-11-04 user decision. Never queued or launched.
Two startup-state jobs are necessary: `layer/util.cpp:338` calls
`retain_startup_tilesets` on every settings reload. Neither provisioning at startup
nor toggling material shading can isolate the full Tileset choice live. The latter
would disable other material families while leaving Tileset depth coverage enabled.
Do not run a nominal 0/1/0/1 plan: all four states would retain the startup value.

Fallback: run `ts1-native-tileset-off`, then `ts1-native-tileset-on`, separate
processes in the existing serial queue, only after explicit batch authorization.
Both use visible_unfocused_fg, release Ultra and Hair shadows; paks stay off.
Both set dev=1 so release policy preserves the hidden Tilesets choice; each
requires its requested rt_native_tileset value in the logged cfg keys line.
The complete profile comes from existing `release_qual/profile.cpp` compiled
against this tree's settings, bg3rtx.ini and data/release.ini. Only the Tileset
key differs between launch rendering settings. Qualification diagnostics and
capture/exposure controls are explicit. No changes to rendering code or presets.

## Scene evidence

`inventory.py` reuses coverage.py's DB names and retained frame associations;
it disables save-folder inspection. Of 101 readable retained captures, Dialogue3
has the most Tileset G-buffer draws: 132, including 48 `cad41eff979970e4`
material draws. Its depth pass has 56 `781cd8d1d600117d` draws.
Deciding frame: test-main/captures/20261103-181930. S8 maximum is 62,
S6 is 1; unassociated captures are not eligible. This is a retained inventory,
not proof of today's view or of every family being supported. COVERAGE.md's older
S1 association and the 110-depth/105-deferred frame in the construction note
do not establish that the current QuickSave_3784 docks view still contains them.

`TRACK_D_REFLECTIONS.md:497` describes 13 opaque S8 Tileset draws whose pool
COLOR blend weights are unsupported. Do not confuse these with the exact STI
AlphaTest pair above. That native recipe selects Green for COLOR.y > 0, then
Red for COLOR.x > 0, then Base; it does not blend. `docs/NATIVE_TILESET.md`
and `docs/evidence/native-hit-tileset-20261031.md` document the supported paths.

## Evidence and decision

Capture: post-VS clip/UV/COLOR plus basis (80 bytes, or existing Hair 88), native
Green alpha and owned samplers for depth; nine real 2D views and tilings for the
STI hit-material recipe. Runtime proof is `[alpha tileset] records complete > 0,
unknown 0, capture-incomplete 0` and `hit material tileset: qualified family2 > 0;
native primary matches > 0`. Unknown/incomplete are the capture-invalid indicators;
there is no log field literally called "invalid Tileset records".
Both producers run every 120 frames (`alphatest.cpp:1351`, `core/gi.cpp:1717`).
Coverage reports include overall material qualified/attempted and hybrid GI shares.
No threshold or direction for overall coverage/energy is invented.

Warmup, warmed float capture control, diagnostic interval, capture-free RT/GI cost
interval, one static trace and restoration. Shared flow retimes all producer
windows. Two traces total form the PNG pair. Estimated plans: 99.6 seconds each;
with 120 seconds load each: 7.32 minutes total, 3.66 per launch. Fallback replaces
the requested one-launch six-minute budget. Each launch has a hard 4.9-minute
cap including load; slow cold compilation may therefore invalidate it.
Dry-run disk estimate: 140 MB, no bursts/raw attachments.

`predicted.json` is written before jobs. All technical gates must pass.
`analyze.py` fails closed on missing evidence and reports float pre-post scene
mean from dumps.json, pre-exposure, coverage producer lines, and RT/GI GPU ms.
No matched Dialogue3 PT reference was found in the retained reference notes/jobs.
This is a shading-model change: energy equality is not a pass rule. Cross-launch
view, exposure and stochastic differences are confounders, so neither energy nor
PNG difference grants a causal A/B or quality pass. User inspection remains open.
Keep complete captures and both logs until comparison finishes.

Offline commands (no game):

```powershell
# Export current profile using existing release_qual/profile.cpp in an MSVC shell:
cl /nologo /std:c++20 /EHsc tools/live_batches/release_qual/profile.cpp /Fe$env:TEMP/ts1-profile.exe /Fo$env:TEMP/ts1-profile.obj
& $env:TEMP/ts1-profile.exe ultra | Set-Content tools/live_batches/tileset_qual/profile.txt
python tools/live_batches/tileset_qual/inventory.py
python tools/live_batches/tileset_qual/make_jobs.py
python tools/live_batches/live_flow.py tools/live_batches/tileset_qual --preflight --report tools/live_batches/tileset_qual/lint.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/tileset_qual/dryrun.ps1
# After authorized runs (each job automatically extracts its own report):
python tools/live_batches/tileset_qual/analyze.py --compare <off.tileset.json> <on.tileset.json> --out <comparison.json>
```

No pending inbox is written. Historical replay has no ON Tileset evidence;
successful lint/dry-run is preparation only. No release promotion is implied.
