# RTX hair roadmap: from the strand prototype to a better hair look

2026-11-03, offline plan. Builds on RTX_HAIR_GROOM_EXPERIMENT.md (converter, bench) and RTX_HAIR_INTEGRATION.md
(prototype, key `rt_hair_groom`, off). User rule: physically correct is fine even if it differs from the game.
Costs are from `tools/hair_lss_bench` (RTX 5090, 3.7M rays per batch all leaving the hair: a full-screen
close-up worst case; scale by the share of rays that reach hair). Per near character: groom 2 mm compacted
0.91/0.74/1.27 ms shadow/AO/GI, refit 0.10 ms, BLAS 60 MB + points 11 MB; LOD 1/2: 0.71/0.62/1.03 ms, 30 + 6 MB;
LOD 1/4: 0.63/0.61/0.92 ms, 15 + 3 MB; static arrays 34 MB per hairstyle (mesh pool). Cards (3-sample test):
1.94/1.71/2.99 ms. Status tags: OFFLINE-NOW (no game), NEEDS-LIVE (user-authorized batch).

## Order (visual impact per effort)
| # | Step | Impact | Effort | Can start |
|---|---|---|---|---|
| 1 | Qualify the secondary-ray prototype | medium (shadows, AO, GI, reflections of hair) | days | NEEDS-LIVE (job ready) |
| 2 | Automated batch conversion of every style (first version done) | high (whole cast) | days | OFFLINE-NOW |
| 3 | Converter quality + artist-free validation | medium (fewer frizz tips, gaps) | 2-3 days | OFFLINE-NOW |
| 4 | Fibre BSDF at secondary hits | high in GI/reflections, backlight | 1 week | OFFLINE-NOW, then live |
| 5 | Animation robustness | medium (no lag/pop under motion) | days | mostly NEEDS-LIVE |
| 6 | Primary replacement of the visible hair | very high (the hair you see) | 3-5 weeks | after 1, 4, 5 |

## 1. Secondary rays (prototype, commit 104092ca)
On screen: hair shadows on face/neck/shoulders become fibre-fine instead of ribbon blocks; AO under the hair,
GI bounce and reflections see strands; the visible hair is unchanged. Method: as implemented (groom TLAS,
card skipped in software). Inputs: Shadowheart's .hgrm (generated), host-mapped hair index buffer (join).
Risks: join fails (index buffer device-local: log `[hair groom] draw with N indices`), self-shadow acne at the
card surface (strand lies on the card: tmin derived from 2 x max radius if seen), double shadow where a
groom and an unmatched card overlap. Cost: predicted rt -0.5..-1.5 ms, gi -0.8..-2.0 ms vs cards (card AABBs
still traversed), scene build +0.05..+0.3 ms per character. Gates: live job `tools/live_batches/hair_groom`
(groom instances > 0, no background build in judged windows, character float energy ON >= OFF - spread,
cost off/groom/off/no-hair). Offline already passed: build, 130 CTests, key-off identity vs main.
Next optimisation if the gate passes: hide replaced cards by instance mask instead of the software skip
(needs a mask bit free in cascade/current-sun passes) for the full card-cost saving. Deps: none.

## 2. Automated batch conversion and coverage (OFFLINE-NOW; first version done)
One command on the user's PC: `python -m tools.hair_groom.batch --report-dir <dir>` (`--match`, `--limit`,
`--jobs`, `--accept-review`). It enumerates the LOD0 CHAR_Hair objects of the mesh_materials catalogue (872 rows:
hair and beards), resolves each mask, reads GR2 and mask from the paks (read-only), decodes the GR2 with LSLib's
reader (`gr2_dump.ps1`, PowerShell 7, granny2.dll next to LSLib.dll; native vertex/index order, so the index-only
join ID holds; LSLib's glTF exporter fails on some meshes, e.g. Lae'zel, Jaheira), detects the atlas grid,
converts (strands of every tile a card touches, dedupe), validates (silhouette IoU, coverage and radiance vs the
cards in front/side/back renders at 2 mm, root channel, UV winding, reversed cards) and writes pass /
needs-review / reject, a front PNG per style, `summary.md`, `state.json`; assets (3 LODs, named by index ID)
only for pass (review with the flag), add-only. Incremental by pak entry (package, offset, size) + mesh +
converter version; resumable; progress every 30 s; BelowNormal priority.
First run (9 companion styles, 81 s, 3 jobs): 7 pass (Shadowheart, Astarion, Karlach, Lae'zel, Gale, Minthara,
Jaheira: IoU 0.92-0.97), 2 needs review (Gale's beard: IoU 0.76, small mesh at 2 mm pixels; Halsin: 44% of the
cards with the root away from the head). Atlas grids seen: 8x2, 8x4, 9x2, 8x16. All 872: ~27 s per style per
job: ~2.2 h with 3 jobs, ~1.1 h with 6. Next: LOD1 meshes, pubes/fur recipes, beards at finer render pixels, a
review page. Crowd budget: LOD by screen size (done) plus a VRAM cap dropping far characters to LOD 1/4 then
cards (10 close characters at LOD 0 ~710 MB + 34 MB per style). Gates: offline as above (CTest: catalogue
parsing, grid detection); live: matched/unmatched counts in a crowd scene (camp, Lower City). Deps: none.

## 2b. Getting the grooms onto a player's PC: storage, needed set, launcher (2026-11-08, offline study)
Prototypes: `tools/hair_groom/storage.py` (size scan, HGRZ codec, error study), `tools/hair_groom/prepare.py`
(needed-first order, launcher status), CTest `hair_storage_test`, read-only Preparation card in the launcher.
Nothing here changes the layer or the .hgrm format yet; the decisions at the end pick what ships.

**Size today (data/assetdb/hair_grooms, 871 files, 20.97 GB).** Per array, all files and LODs: barycentrics
(2 x f32) 8.15 GB 39%, triangle (u32) 4.07 GB 19%, radius (f32) 4.07 GB 19%, segment_first_point (u32) 3.66 GB
17%, root (u8) 1.02 GB 5%, headers 0. There are no positions or tangents: a point is (card triangle,
barycentrics), placed on the skinned cards each frame; the tangent is the segment's direction. Per strand LOD
inside a file: keep 1 57%, keep 1/2 29%, keep 1/4 14%. Per mesh: LOD0 meshes 10.60 GB (393 files), LOD1 meshes
3.26 GB (168), and 7.11 GB (312 files) are not written by the last batch (`.scratch/hair-c/full`): earlier,
wider batches' outputs (299 of them have no mesh in the current GR2 dumps). A player would not have those.
Hair 13.3 GB, beards 0.57 GB (71 rows). Per file: median 19.4 MB, p10 6.4, p90 46.1, max 120.8 MB; 9.8 points
per strand. Largest: HAIR_DWR_M_Straight_Short_J 121 MB (3.3 M points, 255 k strands), HRC MenStraight_Short_J
119, the Githyanki long cuts 110-112, the three Afro_Long_A 103, DWR Braids_Long_B 97. The top 10% of files hold
28% of the bytes. The batch cache is a second footprint: `<report-dir>/cache` 16 GB of groom .npz + 0.5 GB of
GR2 dumps for 737 rows; on a player's PC it must be dropped after each style (keep state.json only).
- Redundant by construction: the thinned LODs are `convert.thin(LOD 0, keep)` (strand hash < keep: nested
  sets; radius min(r / keep, 9 mm) with keep a power of two): bit-exact on every sampled file, 43% of the bytes.
  `segment_first_point` is "every point but each strand's last": a strand length (1 byte) replaces ~9 x 4 bytes.

**HGRZ prototype** (`storage.encode` / `decode`): LOD 0 only, thinned LODs rebuilt at load; strand lengths
(u8 + u16 escape); triangle as zigzag delta along the strand; barycentrics as N-bit fixed point, wrapped delta
along the strand; radius as a 16-bit log2 code (4096 steps per octave: 0.017% relative); root as mod-256
delta; each stream byte-shuffled and zstd-compressed alone.
Measured (`python -m tools.hair_groom.storage codec --sample 24`: 24 files spread over the size distribution,
0.63 GB; 16 of them matched to their card mesh in the batch cache for the error in mm; zstd level 19):
| Encoding | Ratio | Max / p99 position error | Points over 0.38 um | Tangent, segments >= 1 px: max; length-weighted p99 |
|---|---|---|---|---|
| zstd on the .hgrm file | 2.2 | 0 | 0 | 0 |
| HGRZ lossless (f32 barycentrics and radius) | 6.1 (3.4-10.5) | 0 | 0 | 0 |
| HGRZ 14-bit | 12.4 | 12.2 / 4.8 um | up to 90% | 2.55 deg; 0.15 deg |
| HGRZ 16-bit | 11.5 (6.1-32) | 3.1 / 1.2 um | up to 32% | 0.61 deg; 0.039 deg |
| HGRZ 18-bit | 10.4 | 0.78 / 0.30 um | up to 0.3% | 0.20 deg; 0.009 deg |
| **HGRZ per-file bits** (16-20, `bits_for`) | **11.1** | **0.35 / 0.15 um** | **0** | 0.14 deg; 0.019 deg |
| HGRZ 16-bit, LZ4 instead of zstd | 8.2 | as 16-bit | | |
Every variant: thinned LODs rebuilt bit-exact, triangle/root/segments exact, radius within 0.017%, 0 zero-length
segments after quantisation (parse_lod rejects those). Per-file bits: the batch has the card mesh when it writes
the file, so it picks the fewest bits whose worst case (step / 2 x (|e1| + |e2|) of the largest used triangle)
is under the threshold: the bound holds by construction, not by sample. Tangent: segments shorter than one pixel
(at most 1.6% of the segments) reach 3 deg, as any sub-um shift does on a 1 um segment; they cover under a
pixel. Longer segments stay under the game-roughness bound (0.16 deg); the narrowest-lobe bound (0.014 deg) is
met by 98-99% of the drawn length, fully only by the lossless variant. Over the 20.97 GB: per-file bits
~1.9 GB, lossless ~3.4 GB; the 559 files of the last batch (13.87 GB): ~1.25 GB / ~2.3 GB. Encode (numpy, zstd 19)
~1.8 s per M points: ~1 s for a median style, against 27 s of conversion. First traps found and fixed in the
prototype: barycentrics down to -1e-4 (clipping them moved points by 11 um), radii from 17 nm to 2 mm (a 16-octave
log range overflowed).

**"Imperceptible", derived.** Closest dialogue framing: the repo's close-up bound (`primary.subpixel_fixture`,
0.6 m at 20 degrees vertical) at 4K: one pixel = 2 x 0.6 x tan(10 deg) / 2160 = 0.098 mm (a 0.23 m head filling
the 2160 rows gives 0.106 mm). A strand edge moved by d changes a pixel's coverage by at most d / pixel; at full
contrast an 8-bit output level is 1/255. So d <= 0.098 mm / 255 = **0.38 um** changes no output level even
before DLSS, TAA and dithered coverage. Tangent: the fibre BSDF's narrowest longitudinal lobe (beta_m clamp 0.05,
`bsdf.longitudinal_variance`: sigma 2.2 deg) changes by at most 0.607 / sigma of its peak per radian, so 1/255 of
the peak needs **0.014 deg** (game hair roughness 0.41: 0.16 deg). Radius: a relative error e widens a strand by
2 r e; 0.017% of a 0.7 mm p99 radius is 0.12 um, under the position threshold.

**Decode cost at load.** numpy prototype (single thread, one LOD 0 + two thinned LODs): 133 ms per M points
(lossless 87): ~70 ms for a median style (0.53 M points), 440 ms for the largest (3.3 M). A C++ decoder
(zstd at ~1 GB/s, shuffle and prefix sums) should be ~5-10x faster; the layer already reads a whole LOD at upload
(`read_lod`), so it would read the file once, decode LOD 0 and thin in memory. Needs zstd in the layer
(the save reader in layer/assetdb.cpp has only an LZ4 block decoder; it skips zstd SaveInfo.json today).

**What is needed.** Known before the game starts, without the game:
- the catalogue (872 CHAR_Hair rows) and `tools.hair_groom.reach` categories from the paks (player 432, companion
  6, named NPC 211, generic 25, hat variants 337, items 49, unused 157 of the 1217-row run; RTX_HAIR_PRIMARY);
- per level: `reach` already reads every level's `Characters/_merged.lsf`; keeping the level name per template
  gives styles per level (templates placed there), a few lines in reach;
- the current level and party: layer/assetdb.cpp already reads the newest save's SaveInfo.json (read-only) for
  "Current Level"; its party list names the origins and the custom character's race (to check on a player
  save: we never open the user's saves in development), not the hairstyle.
Known only at runtime, exactly: the layer computes the index id of every hair draw (the join key) and logs
CountUnknown / NoFile misses. An index map built from the GR2 decode alone (no conversion) names the style of any
draw: measured on the 545 cached dumps, 1212 meshes -> 1180 distinct index ids (32 identical meshes share a
groom), 731 distinct index counts (189 counts shared by several ids, so the id, not the count, is the key),
1.1 s to compute, 86 KB as JSON (+ 32 UV samples per mesh for draws without host-mapped indices: ~310 KB).
Approaches (27 s per style per job; 872 rows -> ~98 min at 4 jobs; companion rows ~32, 0.88 GB raw):
| | First play | Disk (HGRZ per-file bits) | Misses on first sight |
|---|---|---|---|
| (a) companions, then the rest in the background | ~4 min for companions at 4 jobs | all reachable, ~1.1 GB (about 87% of the styles are reachable) | custom player hair, hirelings, disguises until reached in the queue |
| (b) on demand, cards meanwhile | 0 | only seen styles | every style once (cards for that session); wildshape/disguise/NPC crowds pop |
| (c) everything | ~98 min before strands appear | ~1.25 GB (today 13.9 GB) | none (rejects keep cards) |
Recommended: (a) + the runtime "seen" list. The layer appends the index id of each unconverted hair draw to
`%LOCALAPPDATA%\BG3RTX\hair_seen.txt` (no new game key: the existing miss path); the launcher converts in
`prepare.order`: seen first, then companion, player (character creation), named NPC, hat, other; LOD 0 before
LOD 1 in each tier; "unused" (157, no game file references them) only with Convert all. Edge cases:
- Custom player hair: in the player tier (432 styles), so first play may show cards for one session; the seen
  list puts it first next time. Narrowing by the save's race is possible but unverified.
- Hirelings: character-creation looks, player tier. Disguises (Disguise Self, Alter Self) use CC visuals:
  player tier, seen list. Wildshape: no animal row (wolf, bear, cat, panther, owlbear, spider, badger, raven) in
  the 872-row catalogue, so nothing to convert; creature hair that is CHAR_Hair (bugbear, goblin, cambion) is
  ordinary NPC tiers.
- Hats: hair under a hat is a separate mesh (hat tier, 337): converted after NPCs; cards until then.
- NPC crowds: named NPC tier; generic NPCs (25) late; the crowd budget falls back to cards anyway.
- Beards: rows of their own (71), same tiers. LOD1: after its LOD0 in each tier; LOD1 missing = cards at distance.
- Mods: a mod's hair has no catalogue row; the seen list gets its index id but no map entry, so it cannot be
  converted until the catalogue is rebuilt from the installed paks (the batch already re-enumerates: run it
  with the mod's pak in the PackageSet). Rejects and needs-review keep cards in every approach.
- Convert all: the opt-in for players who want no card ever; also covers unused and CV-only styles.

**Launcher: one Preparation card** (Help & support page, first card; status, not a knob, so it fits neither the
settings table nor CONTROL_CONTRACT's pages and adds no game key). One line per item, from a JSON block the
batch writes (`prepare status --json`), polled by the bridge while the window is open:
- `Strand hair: 412 of 559 styles ready, 2.1 GB, converting, about 35 min left.` progress bar; detail line:
  the current tier ("Companions and character creation done. Converting named NPCs."). Buttons (next step, not
  prototyped): Pause / Resume, and a "Convert all styles (+157, about N min)" switch.
- `Shaders: ready.` / `Shaders: preparing, 40 of 100.` What exists: a SPIR-V disk cache
  (%LOCALAPPDATA%\BG3RTX\shader_cache, keyed like tools/shader_identity.py), a request manifest (`requests.txt`,
  prefetched on a background thread at game start), async pipeline builds (common/pipeline_async.h; the first
  raygen build is ~60 s on a cold driver cache, compute meanwhile), no VkPipelineCache (the NVIDIA driver cache
  holds compiled pipelines). A launcher preload makes sense only for the SPIR-V (BUILD_AND_PACKAGE_EFFICIENCY
  section 4, option B 40-60 s once, or C: ship the preset's SPIR-V as a seed, 0 s); the driver's pipeline cache
  is per application and GPU, so the launcher cannot warm it: the card says the first minute of play finishes it.
- Where it also shows: one header line "Preparing: strand hair 74%" while work is left; Play stays enabled
  (cards are drawn until a groom exists).
Prototype (this branch): `preparationCard` in tools/launcher/ui/app.js renders `state.preparation` items
read-only (title, state badge, line, progress, detail); the sample preview (`?sample`) shows it; the
renderer test checks it renders only when sent, one row per item, no buttons. The bridge does not send it yet.

**Decisions for the user.** (1) Encoding: HGRZ with per-file bits (11.1x, every point under 0.38 um; recommended)
or lossless (6.1x); both need zstd in the layer (decoder ~100 KB of code) and a format version 3 the layer reads
beside v2. (2) Needed set: (a) + seen list (recommended), or (c) only.
(3) Convert all as an opt-in switch, default off. (4) The launcher converts only while it is open and the
game is not running (BelowNormal; the game's level load starves otherwise), or also in the background.
(5) Shader preload: ship the SPIR-V seed (C) or prewarm in the launcher (B). (6) Delete the batch cache per style.

**Implemented (user decisions 2026-11-08: per-file bits, needed first with Convert all off, the player picks when to
convert, compile before Play, cache deleted per style).** Branch claude/hair-groom-storage.
- Format v3 (`common/hair_groom_v3.h`, writer `tools/hair_groom/storage.py encode_v3`, `asset.write`): the v2 header
  and UV samples, the LOD table (v2's 16-byte LOD headers, so the crowd plan's segment counts stay index-time), then
  LOD 0 as 7 streams; the thinned LODs are rebuilt by `decode_v3_lod` exactly as convert.thin. Barycentric bits per
  file from the card mesh (`pick_bits`: bits_for, raised until no segment collapses; 16-20 seen). The converter writes
  v3 only and replaces an older file of the same name; the layer and launcher read v1-v3.
- Codec: LZMS through the Windows Compression API (`common/hair_groom_lzms.h`, cabinet.dll), not zstd: there is no
  zstd source in external/ or anywhere on this PC, and vendoring it means a download that needs the user's approval.
  LZMS is part of Windows: nothing to vendor or license. The codec word allows zstd later (one id + the vendored
  decoder). Ratio: the 5-style dry run below 10.4x (zstd 19 in the study: 11.1x).
- Dry run (`python -m tools.hair_groom.storage dryrun --cache <batch cache>/groom --out .scratch/assetdb_v3/hair_grooms
  --limit 5 --v2 data/assetdb/hair_grooms`; the batch's own `asset.write` on cached grooms: the paks on D:\ were not
  read): 4 styles with a v2 file 176.7 MB -> 16.9 MB (10.4x; 3.3 M-point style 121 -> 9.6 MB, 12.6x), max position
  error 0.358 um (threshold 0.38). Encode 0.7-6.8 s per style (largest), against ~27 s of conversion.
- C++ decode (`hair_groom_v3_test <fixture> --dir <dir>`, RelWithDebInfo, all 3 LODs, each decoding the file):
  equal to the Python reference on all 5 (triangle, root, segments exact; barycentrics and radius within 1e-7 /
  1e-6). 98-110 ms for 0.35-0.40 M-point styles, 194 ms for 0.8 M, 757 ms for 3.3 M; LZMS is 80% of it. The layer
  decodes one LOD per upload (`read_lod`, logged `LOD n decoded (v3): points in ms`): ~1/3 of those numbers, on the
  upload path, once per groom LOD. The live shows them: queue log r2-inn.1.020820 (inn dollies), every frame of
  90 ms to 1 s follows a synchronous decode within 33 ms; `rt_hair_groom_async` moves it to a worker (staged live
  `tools/live_batches_staged/r4_hitch`, branch claude/r4-hitch).
- Seen list: the layer's miss paths (`index id has no groom file`, unmatched CountUnknown / UvMismatch hair draws,
  already deduplicated per index count) call `hair_groom::note_seen`; `flush_seen` merges them into
  `data/assetdb/hair_seen.txt` ("<index count> <index id>", id 0 = only the count known) at a level load and at device
  destroy. The batch (`--seen`) names them through `index_map.json` (the GR2 decode only, for GR2s no recorded row
  explains) and converts them first (`prepare.order`).
- Batch: needed-first order (seen, companion, player, named, hat, other; unused only with `--all`), categories from
  `<report-dir>/reach.json` (computed once with tools.hair_groom.reach from the paks, else name order), every row
  records its index id and count, the groom cache is deleted after each style and a GR2 dump after the GR2's last row
  (`--keep-cache` for developers), `<report-dir>/preparation.json` (prepare.status: ready / needed, bytes, ETA, tier,
  detail line) every 30 s.
- Launcher (`tools/launcher/preparation.{h,cpp}`): the Preparation card, first on Help & support. Hair: "412 of 559
  styles ready, 1.1 GB on disk." + "Converting named characters, about 35 min left." + progress; Pause/Resume, "Convert
  all styles, also those the game does not use", "When to convert: Game closed / Also while playing" (one core at
  low priority). Settings in bg3rtx_launcher.ini (`hair_convert_all`, `hair_while_playing`, `prep_paused`; optional
  `hair_python`), never game keys; save_game_dir keeps them. The converter runs as `python -m tools.hair_groom.batch`
  in a kill-on-close job object at BelowNormal (closing the launcher, Pause or a policy change stops the whole tree;
  the batch resumes by state.json), 1..4 jobs with the game closed, 1 while playing; it stops when the game starts
  unless "Also while playing". Shaders: once per launcher session with the game closed, `BG3RTX.exe
  --precompile-shaders` (child, BelowNormal) loads the layer and calls its `bg3rtx_precompile` export: the requests
  of `shaders/precompile.txt` (an Ultra session's 71) and the player's requests.txt, with the game's DXC, arguments,
  key and cache folder; cached requests are not compiled (test: 2 compiled in 1.2 s, then 0 in 0.07 s). The card says
  the driver still builds the first ray tracing pipeline in game. The layer's log goes to bg3rtx.precompile.log.
- Before a release build: the release ships no Python, no converter scripts, no LSLib/granny2 and no mesh_materials
  catalogue, so the hair line says "Not available: ..." on a player's PC (needs a frozen converter or an embedded
  Python, plus LSLib and PowerShell 7, and the catalogue); the full conversion run on the user's PC (scheduled by the
  coordinator); a live check of the v3 upload decode cost and of hair_seen.txt. The batch replaces only the files it
  writes: the 312 v2 files of older batches in data/assetdb/hair_grooms stay (and load) until someone deletes them.

## 2c. Runtime capture as the converter's source (2026-11-08, offline study; branch claude/hair-runtime-capture)
Why: granny2.dll cannot be redistributed (RAD licence; BG3 ships none), so the release converter cannot read GR2.
Verdict: **feasible**. A groom is (card triangle in native order, barycentrics from UV, radius from the UV -> world
Jacobian, root from the mask); every one of those inputs is drawn by the game and reachable by the layer.

**What the layer sees for a CHAR_Hair draw (read-only code study).**
- Soup (layer/geocapture.cpp): every captured draw's transform feedback, per corner in index order; position at 0
  recovered as camera-relative world W (post-skin, post-wind: posed, not bind pose), UV0 at +16 at every alpha stride
  (24 by default; 88 with rt_native_hair adds world N/T/B, post-wind camera-relative position and fade). No root or
  strand channel exists on the vertices: root, alpha and strand ID are the mask's channels.
- Indices: the draw's index range; the layer reads it when host-mapped (groom_join, index id) and can copy it by
  GPU otherwise (buffer and offset are tracked). With the range, vertex numbering is native, so cards and the index
  id are exact. Without it, welding identical corners breaks both (measured: 258 -> 230 vertices on one style).
- Mask: the CHAR_Hair material binds it as a plain 2D view (NATIVE_HAIR.md: static set 1 binding 4, instanced
  0/3; layer/alphatest.cpp kind 4 queries it with the noise view), not a VT page. Readback: a transfer copy of mip 0
  (raw BC blocks) or the replay pack's shader read (layer/replay.cpp, shaders/replay_image.hlsl, "BCn decoded").
- Join: the index id (when the range is read) or count + 32 UV samples (the UV join): both already name a groom file.

**GR2 fields the converter uses, and the runtime replacement.**
| GR2 field | Used by | Runtime | Loss |
|---|---|---|---|
| UV0 per vertex | grid, tracing, embedding | soup +16 | none (bit-identical) |
| triangles, native order and vertex numbering | cards, triangle ids, index id, UV samples | index range (+ soup order) | none with the range |
| bind-pose positions | radius (Jacobian), dedupe (0.5 mm), validation renders, content id | posed soup positions | radius scales with the pose's local stretch; dedupe borderline flips; position id differs (unused by the join) |
| bone joints / weights | old .bgrm bench only | none | none for v3 |
| skeleton Head_M / HeadEnd | head proxy (orientation, root from geometry) | sphere fit to strand roots (`capture.head_from_roots`) | a proxy, tested on synthetic roots |
| mesh name, LOD row, catalogue category, visual id | names, LOD1 grid from LOD0, tiers, reach | index id; LOD1 linked to LOD0 by the same mask image | tiers become "seen" order; Convert all can only convert captured styles |
| mask DDS (pak) | tracing, root, validation | bound mask mip 0 | identical if mip 0 is resident at full size (live) |

**Offline equivalence** (`python -m tools.hair_groom.capture equivalence`, 3 cached styles of 330, 1178 and 25710
triangles; the same synthetic mask on both sides, since no game texture is on this PC; CTest hair_capture_test on
synthetic cards): with the index range, a rigid pose gives bit-identical strands, triangles and barycentrics and the
same index id and UV samples, radii within 7e-5; a bent pose (2% of the mesh size) keeps the embedding exact and
moves radii by 1.5-6.6% (the stretch). Before dedupe the largest style is identical too; after it 3 of 125,678
strands differ (pairs at the 0.5 mm threshold). Without the range (welding) the embedding and the id change.

**Capture design.** In bvhsplit's existing miss path (a hair draw with no groom: CountUnknown / NoFile / UvMismatch,
already logged once per index count): once per style, record GPU copies of (1) the index range (count x 2-4 B), (2)
the draw's soup range (count x stride: 1178-triangle style 85 KB at stride 24; 25710 triangles 1.9 MB), (3) mask mip
0 (raw BC blocks, 2048^2 BC7 4 MB) into a host-visible staging buffer in the next capture submit; read back with the
existing readback_state machinery and written by a worker thread to `<assetdb>/hair_capture/<index id>.hcap`
(header: index count, stride, mask format and size, the draw's material hash; then the three blobs, LZMS). Cost: a
few MB of copies once per style, no per-frame work (the miss set is already deduplicated); at most one capture per
frame. The converter reads .hcap instead of GR2 + pak (`capture.from_capture` + BCn decode: Pillow reads the DDS
formats; or the layer stores the shader-decoded RGBA8). Probe in this branch: `[hair capture] fs .. mask image N:
format, size, mips (view base, levels), layout, usage` once per mask image (layer/alphatest.cpp, rt_native_hair only).
Legal: the capture copies only what the game itself uploaded on the player's PC, kept locally for the player's own
use; nothing is redistributed.

**Live job** (READY_FOR_LIVE, not queued: tools/live_batches/hair_capture, hcap1-dialogue, Test Dialogue1, live_flow
lint 0 failing): mask format / size / mips per style (resident mip 0 vs the pak DDS), stride 88 with UV, and how
many hair draws have no host-mapped index buffer (groom stats "not mapped"). Only a live run answers those and the
real pose stretch (card edge lengths over a few frames).

**Live result** (hcap1-dialogue, 2026-11-08 20:32, VALID, 142 s, nv153 0). Folded into the capture spec:
- Masks are BC3_UNORM (format 137). 8 hair mask images were bound as 128x128 with 1 mip (streaming placeholders),
  2 at 2048x2048 with 12 mips; noise textures 64x64 vs 1024x1024 likewise. So the capture waits for a full mask:
  mip count > 1 and at least the size seen before for that join id (stored per id once seen); a placeholder never
  feeds the converter, and a style captured from a placeholder (should one slip through) is re-captured when the
  bound image is upgraded (new image id with more mips) and its groom rebuilt.
- Indices: "not indexed 0" over the run and "not mapped 0": the index-range path covers every hair draw.
- Unmatched hair draws: 424-600 per 300 frames, all "count unknown" (12 "UV pending"): in bvhsplit the count filter
  `count_known(d.args.count)` runs before any join (CountUnknown = no groom file has that index count), so these
  are exactly the styles without a groom, the set the capture feeds.
- Card stretch in real poses: not measured by this job. The capture records it: per style the ratio of each card
  edge's posed length to its length at the first capture (min / median / max over edges), logged with the capture
  line and stored in the .hcap header, so the first capture run reports it.

**On-the-fly variant (convert while the game runs, cards meanwhile).** Stage times today (`capture stages`, 12
cached styles spread by size, one thread, synthetic mask with each style's strand counts; GR2 decode needs the paks
and is not included: the 2026-11-07 batch seconds exceed these totals by a median 1.1 s with pak read and GR2 dump):
| Stage | Median s | p90 s | Share |
|---|---|---|---|
| grid detection + root check | 0.04 | 0.07 | 0.3% |
| strand tracing (convert) | 4.6 | 6.3 | 46% |
| dedupe | 0.7 | 2.2 | 14% |
| validation renders (3 views x cards, fibres, strands) | 1.8 | 4.1 | 21% |
| LODs + v3 write (bits, LZMS) | 1.6 | 4.0 | 18% |
| total | 8.6 | 15.3 (max 45, 4.95 M points) | |
Minimal runtime path: tracing + dedupe + LODs/write, with validation replaced by one cheap check (strand count and
radius range; IoU/coverage/radiance deferred to the launcher, which can delete a failing groom): ~6.9 s typical,
~38 s for the largest, one core. Two threads halve a party's wall time (styles in parallel), not one style's. A party
(4 characters, hair + beards, LOD0 + LOD1: ~8-12 captures) needs ~1-1.5 min of one core. A C++ port (needed anyway:
the release has no Python) should cut this 5-10x (typical ~1 s). Also needed for in-game swaps: the layer indexes
groom files once; it must re-scan at a level load (~40 lines). Recommendation: **capture in game, convert in the
launcher** (the existing child, which already offers "Also while playing" at one BelowNormal job), not an in-process
converter: no Python or GB-scale numpy memory inside the game's process, no CPU competition in CPU-bound scenes
(Lower City's scene thread is at its limit), and the full validation stays before a groom replaces cards. The cost
is latency: a style seen for the first time gets strands at the next level load after its conversion.

**Implementation size.** Layer capture + .hcap writer ~300-400 lines; groom re-index at a level load ~40; Python
.hcap reader and batch source switch ~200 (tiers from the seen list); C++ converter port for the release
(convert/strands2d/curves2d/analysis/render, ~2,000 Python lines) ~3,000-4,000 lines, the bulk of the work.

## 2d. Native converter: bg3rtx_hairconv (C++, 2026-11-08; branch claude/hair-converter-native)
C++ rather than Rust (coordinator decision): the speed comes from the algorithm, layout and threads, and the repo
already builds, tests and packages C++ (no second toolchain in the build, merge-green or the installer).
- `bin\bg3rtx_hairconv.exe` (tools/hairconv: core.cpp tracing / locate / runs / dedupe / analysis, curves2d.cpp the 2-D
  tracer, validate.cpp the validation renderer, io.cpp .hcap reader and v3 writer, main.cpp CLI; static CRT).
  Input: `.hcap` (tools/hair_groom/hcap.py layout: native indices, positions, UV, decoded RGBA8 mask, optional head
  proxy and grid). Output: .hgrm v3 (per-file bits, LZMS), read by common/hair_groom_v3.h. The batch cache is a dev
  input only through `python -m tools.hair_groom.hcap export` (cached meshes + a synthetic mask).
- Same pipeline as batch.process (`hcap.reference` is the Python dev reference): grid, root check, trace (rows or 2-D),
  dedupe, head (the file's or fitted to the strand roots), orientation / reorient, check, write. `--check full`
  (default) adds the validation renders and batch.py's verdict rules; `--check cheap` keeps the structural rules.
- Threads: cards in parallel for locate / runs, tiles in parallel for tracing, a spatial hash (cells of the 0.5 mm
  threshold, 27 neighbours) for dedupe, views and fragment chunks in parallel for validation, the 7 LZMS streams in
  parallel; `--jobs` runs styles in parallel. No per-point allocation in the inner loops.
- State: `--report-dir` keeps state.json (input name, size, mtime, converter version) and writes preparation.json
  (the Preparation card's fields) every 30 s; progress lines every 30 s; BelowNormal priority.
- Speed (24 cached styles, 330-37,932 triangles, 1024 x 1024 synthetic masks; Python on a quiet machine; seconds per
  style, median / p90; this PC):
  | Stage | Python | C++ 4 threads |
  |---|---|---|
  | grid + root check | 0.015 / 0.028 | 0.001 / 0.002 |
  | strand tracing | 12.1 / 17.4 | 0.195 / 0.292 |
  | dedupe | 0.144 / 0.510 | 0.068 / 0.309 |
  | validation renders | 1.00 / 2.34 | 0.065 / 0.165 |
  | LODs + v3 write (LZMS) | 0.41 / 1.49 | 0.119 / 0.423 |
  | total | 14.6 / 19.6, max 21.4 | **0.51 / 1.11, max 1.78** |
  Largest style (162,380 strands): Python 17.9 s, C++ 1.27 s. One thread: 0.75 s median, 3.26 s max; 4 styles in
  parallel (1 thread each): all 24 in 8.9 s. Targets (typical < 1 s, largest < 5 s on 4 threads) are met. What is
  left of the time: LZMS (single-threaded per stream; the largest stream bounds the write), dedupe's pair counting on
  the largest styles, and tracing (sequential per tile row by construction).
- Equivalence gate (`python -m tools.hair_groom.hairconv_compare`, 24 cached styles of 330-37,932 triangles, 2 with a
  curly atlas; the same synthetic mask on both sides): every verdict, IoU / coverage / radiance (3 decimals), grid,
  card, strand and segment count identical; the 19 written grooms decode to identical LODs (triangles, roots,
  segments, and barycentrics and radii bit-identical: 0 difference), same index id. The 2-D tracer matches to 2e-12 px
  (numpy's own float32 sin/cos reproduced), the renderer to 9e-9. CTest `hairconv_test` runs a synthetic fixture
  (tests/fixtures/hairconv, `hcap fixture`) against the reference's record and v3 file.
- Launcher: the Preparation flow runs `bg3rtx_hairconv <assetdb>\hair_capture --out <assetdb>\hair_grooms
  --report-dir %LOCALAPPDATA%\BG3RTX\hair_batch --threads N --jobs 1` (N: 1..4 with the game closed, 1 while playing),
  oldest capture first, same pause, job objects and "When to convert" choice; new captures make it run again. The
  hair line says "No hair styles captured yet." until the layer has captured one. "Convert all" is gone: with the
  runtime capture only styles the game drew exist. Python (tools/hair_groom) stays the dev reference.
- Release: scripts/package.ps1 ships `bin\bg3rtx_hairconv.exe`; verify_rc requires it and allows it among the
  payload's executables; the launcher target builds it. The setup needs no Python, PowerShell 7, LSLib or granny2.
- Left: the layer capture itself (2c: the .hcap writer with the full-mask rule and the stretch stat), the groom
  re-index at a level load, and a live run of capture -> conversion -> strands. (Done in 2e.)
- Convert all, for the user: with grooms built from what the game draws, a style can only be converted after the
  game has shown it once, so there is nothing left to "convert all" ahead of time and the switch is gone.
  (Back in 2f as an opt-in that reads the paks with the player's own granny2.dll.)

## 2e. Layer capture writer and groom re-index (2026-11-08; branch claude/hair-capture-writer, offline)
- Key `rt_hair_capture` (startup; bg3rtx.ini 0; written 1 by "Strand hair on screen" On and 0 by Off).
- layer/hair_capture.cpp: from bvhsplit's no-groom path (CountUnknown, NoFile, UvMismatch hair draws captured this
  frame), once per (index count, mask image): GPU copies of the index range, the draw's soup range and mask mip 0
  (raw BC3 blocks; the mask is the kind 4 alpha record's CHAR_Hair binding) into one host-cached staging buffer;
  readback through the existing readback state; a worker thread rebuilds the vertices, computes the index id and the
  stretch, encodes .hcap v2 (common/hair_capture.h) and writes `data/assetdb/hair_capture/<index id>.hcap` (tmp +
  rename). Full-mask rule: mips > 1, at least 256 and at least the size seen for the style (placeholders counted, never
  converted); an upgraded mask re-captures and rewrites; an existing file is kept unless its mask was smaller.
  With the key on, the tracker adds TRANSFER_SRC usage to the game's sampled BC3 images at creation (as the replay
  wrapper does): game textures are otherwise not copyable, hence the startup key.
- Cost: once per style, never per frame. GPU copy about 4.3 MB for a 1,178-triangle style (4 MB of it the 2048^2 BC3
  mask) and about 11 MB for 25,710 triangles at stride 88; render thread 0.1-1 ms (tracker lookup, staging allocation);
  worker 10-30 ms. Bounds: at most one capture recorded per frame, 2 staging buffers in flight (64 MB cap each),
  8,192 dedupe keys, 128 MB of first-seen corners for the stretch, a 512 MB folder cap (logged once). Logs: one line
  per capture and a summary every 300 frames when anything happened.
- Converter: tools/hairconv reads .hcap v1 (dev) and v2 (layer: BC3 mask decoded, stretch kept in the record).
- Re-index (layer/hair_groom.cpp rescan, about 30 lines): at a level load (the same boundary that drops the join
  caches) new groom files are indexed; never mid-level, never replacing or removing an entry (a groom rewritten in
  place is refused by read_lod's size check until the next start).
- Tests: CTest hair_capture_cpu_test (full-mask rule, stretch, a capture in the layer's layout -> .hcap v2 ->
  bg3rtx_hairconv reads the fixture's mesh and mask back and converts to the identical groom; a BC3 mask decodes to
  the same groom as its RGBA8 twin; truncated files refused).
- Live (READY_FOR_LIVE, not queued, lint 0 failing): tools/live_batches/hair_capture/jobs/hcap2-capture.json then
  hcap2-strands.json (make_e2e_jobs.py). Two launches of Test Dialogue1: the capture key is a startup key and the plan
  runner has no in-run save reload, so the capture (launch 1), the conversion (its post step, stage.py convert: moves
  the captures out of the real assetdb into a scratch assetdb copy in the job's tree and runs bg3rtx_hairconv there)
  and strands from a cold start (launch 2, whose post removes the copy) cannot share one launch.
- hcap2 live (2026-11-08 22:13, both INVALID, nv153 0): nothing captured. The capture decided once per (index count,
  mask image): a style whose first look found a 128x128 placeholder was never looked at again unless its image
  changed, and the ~600 unmatched draws per 300 frames were two background styles drawn every frame (2 keys, 3 looks).
  Their masks never left 128x128; the two 2048x2048 masks (images 14449, 14777, 17 s after the load, as new images)
  belonged to styles that already had grooms. Fix: a per-style retry state (common/hair_capture.h Retry): look again at
  once when the bound mask image changes, else every 300 frames; after 20 placeholder looks the style is "never
  full-size" (counted in the summary, not retried until its image changes); a captured style re-captures only on an
  upgrade. CPU test in hair_capture_cpu_test.
- hcap3 (READY_FOR_LIVE, not queued, lint 0 failing; replaces hcap2): before queuing, `python
  tools/live_batches/hair_capture/stage.py setup 8b32681bb9ecfc30` in the test tree builds <tree>/data/assetdb as hard
  links of the real assetdb minus that close-up groom (the largest one hcap2 matched), so the layer loads the copy
  ("from this checkout", required by both jobs) and the companion becomes the capture target with its full mask.
  hcap3-capture expects `[hair capture] 8b32681bb9ecfc30: ... mask 2048x2048 fmt 137 mips 12 ... written`; its post
  (stage.py convert) runs bg3rtx_hairconv into the copy and compares the regenerated groom with the removed original
  (read-only): index id, strand and point counts, shared strand starts, radius median ratio, the converter's verdict
  and IoU, in captures/hcap3_e2e.json: the first real-mask check. hcap3-strands expects the v3 file indexed and
  decoded and groom instances; its post removes the copy (links only). The real assetdb is never written.
- Resolved (coordinator, 2026-11-08): "Strand hair on screen" and "Strand hair in ray-traced lighting" need hardware only
  (kCapHairGrooms dropped); with no grooms cards stay, captures accumulate and strands follow conversion. Was:
  "Strand hair on screen" required local groom files (kCapHairGrooms), so on a fresh install with
  no grooms it cannot be turned on and nothing is ever captured. With the capture model it should require only the
  hardware (the capture then fills the folder); left unchanged pending that decision.

## 2f. Convert all styles from the player's own files (2026-11-09; branch claude/hair-convert-all, offline)
Opt-in alternative to the capture model (2c-2e): every hair and beard row of the catalogue, before the game has shown
it. Nothing of the game or of Granny is shipped: the paks are read in place (read-only) and GR2 sections are
decompressed by a granny2.dll the player supplies. Without that DLL the mode is unavailable and the capture model stays.
- **CLI (launcher hook).** `bg3rtx_hairconv --all --game "<BG3 folder or its Data>" --granny "<granny2.dll>" --out
  <assetdb>\hair_grooms --report-dir %LOCALAPPDATA%\BG3RTX\hair_batch [--jobs J] [--threads N] [--check full|cheap]
  [--accept-review] [--match <text>]... [--names <file>] [--limit N] [--json <file>]`. Dev only: `--export-hcap <dir>`
  (each style's input as .hcap v1), `--dump-gr2 --game .. --granny .. <pak path> <out.bin>` (gr2_dump.ps1 layout).
  Exit 0 done, 2 bad arguments, 3 granny2.dll missing or not Granny, 4 game packages or catalogue unreadable.
  BelowNormal priority, a progress line every 30 s, resumable (state saved after every style).
- **Pak reading** (tools/hairconv/pak.cpp, port of tools/assets/lspk.py / lsf.py / lsx.py, layouts from LSLib, MIT,
  credited in docs/release/THIRD_PARTY.md): LSPK v18 list and entries, part files, override order (priority, patches,
  name), LZ4 block and frame (own decoder). zstd and zlib entries are refused with a clear error: Patch 8 has 0 zstd
  entries among .lsf, .GR2 and .DDS (all LZ4 or stored), and no zstd source is in external/ (not downloaded).
- **Granny** (tools/hairconv/granny.cpp): `LoadLibraryEx` of the exact path given. Stock Granny 2.11.8 does not open
  BG3's GR2 itself ("magic value doesn't match any known file format"), so, as LSLib does, the DLL only decompresses
  the sections (BitKnit: GrannyBeginFileDecompression / DecompressIncremental / EndFileDecompression; Oodle:
  GrannyDecompressData); header, section table, BitKnit-compressed relocation tables and the data-tree walk (by the
  file's own member definitions) are ours. Read: mesh Name, Position and TextureCoordinates0 (Real32 or Real16) in
  native vertex order, PrimaryTopology Indices / Indices16, BoneBindings, skeleton InverseWorldTransform (head proxy
  as batch.py). Access faults in a malformed tree are caught per style.
- **Catalogue** (tools/hairconv/catalogue.cpp): every */Content/*.lsf (3,295 files) -> VisualBank / MaterialBank /
  TextureBank as mesh_materials.collect, rows as batch.styles (LOD 0/1, CHAR_Hair, ID_Depth_Root_Alpha_MSKA mask),
  categories as reach.py (character creation, character visuals, 55 template files), tiers as prepare.py (companion,
  player, named, hat, other, unused; LOD 0 before LOD 1). Cached in `<report-dir>\all_catalogue.tsv`, keyed by the
  packages' names, sizes and write times.
- **Masks**: DDS mip 0 decoded like Pillow (dds.cpp: 5:6:5 by bit replication; BC3 alpha truncated); all BG3 masks are
  DXT5. Note: the layer capture's BC3 decode (common/hair_capture.h) widens by x 255 / 31 instead (1 level apart on
  some endpoints).
- **Incremental key**: converter version, check mode, mesh name, (package, offset, part, size) of the GR2 and the mask.
  `<report-dir>\all_state.json`: one "name \t key \t record" line per style (record: the capture mode's fields plus
  gr2, mesh, lod, mask, visual, category, tier, index_id, indices).
- **Status** (`<report-dir>\preparation.json`, the file the Preparation card reads; same fields as the capture mode:
  kind, ready, finished, needed, bytes, eta_s, state running/waiting/done, line, detail "Converting <tier>, about N min
  left.", pid) plus: `source` "game" (the capture mode has none), `all` (catalogue rows), `convert_all` true, `review`,
  `rejects`, `next_tier`, `tiers` [{tier, total, finished}], `run_done`, `run_total`, `granny` (version), `updated`.
  Unavailable: `{"source": "game", "state": "unavailable", "error", "line": "Convert all styles is not available.",
  "detail": <reason>}` with exit 3.
- **Player-facing line for the UI**: "Convert all styles needs granny2.dll, which BG3RTX cannot include. If you have
  LSLib's ExportTool (github.com/Norbyte/lslib, Releases), point to the granny2.dll in its folder."
- **Equivalence** (`python -m tools.hair_groom.hairconv_all_compare`, 28 styles of the 2026-11-07 batch spread over the
  strand counts, both LODs, all verdicts; outputs in .scratch): catalogue 1,217 rows identical to batch.styles on
  mesh_materials; 28/28 GR2 dumps byte-identical to the cached LSLib dumps; 28/28 masks byte-identical to Pillow; 28/28
  same index id and verdict (19 pass, 5 review, 4 reject). Strand counts differ from that batch by median 0.4%, max
  1.2% (867 of 72,972): strands2d.trace links runs in `np.argsort(dist, axis=None)` order, numpy's non-stable SIMD
  quicksort, so equal distances (common on real masks, rare on the synthetic masks of the 2d gate) are linked in an
  implementation-defined order; the native tracer uses the stable (flat-index) order. With argsort made stable, the
  Python reference equals the native records exactly on 28/28 (strands, segments, dedupe drops, grid, IoU / coverage /
  radiance, verdict). The hcap3 live's -0.7% strands (2e) is likely the same effect. Fix for the reference: `kind=
  "stable"` in strands2d.trace (a converter-version bump of the dev batch; not done here).
- **Timing** (this PC, Ryzen 7 9800X3D, 16 threads, `--jobs 4 --threads 4`, full check, machine quiet): the whole
  catalogue, 1,217 rows, in **532 s (8.9 min)**: index of 27 packages 0.8 s, catalogue 0.6 s, classification 0.2 s
  (both cached for the next run), then about 137 styles per minute. Verdicts: 898 pass, 282 needs-review, 37 reject;
  884 groom files, 2.72 GB (v3). On the 737 rows the Python batch also ran: 736 same verdicts and every written id
  matches; the one flip (GoblinFemCut_Short_C_Spring: pass -> needs-review, "82 cards with the root away from the
  head") is the argsort tie order at a review threshold. Strands vs that batch: median 0.38%, p95 1.1%, max 3.8%.
  (Python batch: about 27 s per style per job, about 2.2 h with 3 jobs.) Scratch output deleted after the count.
- CTest `hairconv_all_test`: synthetic LSPK (stored, LZ4 block, LSF with LZ4-frame sections, a zstd entry refused, a
  patch overriding an entry, a part file skipped), catalogue and category, catalogue cache, Pillow-exact DXT5, a
  synthetic uncompressed GR2 (relocations, Real16 UVs, Indices16, bones) read without granny2.dll, a BitKnit section
  refused without it, and --all without / with a missing / with a non-Granny DLL: exit 3 and "unavailable".
- Launcher (2026-11-09, after the launcher UX rework): Home's RTX Hair panel runs this CLI with `hair_granny` when
  "All styles" is on; tools/launcher/preparation.cpp maps `source` "game" to "all", `all` to the total, `tiers` to a
  per-tier line (`prep-hair-tiers`), `state` "unavailable" to a stopped run with its reason (the picker stays), and
  exit 2 / 3 / 4 to one-line errors (hair_exit_error); the granny2.dll help is the line above (`grannyHelp`).
  LSLib's MIT text ships as licences/LSLib-MIT.txt (required notice: package.ps1 and verify_rc check it).
- Left: the seen list (hair_seen.txt) is not used in this mode (it orders by tier only).

## 3. Converter quality and validation (OFFLINE-NOW)
On screen: fewer fake tips (strands are 14 mm median pieces: crossings in the painted mask), no doubled
strands, cleaner partings. Method: (a) stitch across card seams (end of a strand on card A to the start on
the adjacent card B when positions and directions agree within the strand radius); (b) longer 2-D tracking
with the mask's R (strand ID) channel to keep identity through crossings; (c) taper radius to 0 over the
last mm at real tips only (B channel near 1), not at fragment ends; (d) keep dedupe (0.6% true copies).
Validation without an artist: silhouette IoU vs cards >= 0.92 (now 0.92-0.94), coverage within the cards'
+-4 points, mean radiance within the off spread, strand length p50 (now 14 mm) and tip count per strand as
tracked metrics; PNG sets front/side/back/backlit/crown (tools/hair_groom/render.py) per style.
Inputs: masks and meshes (paks, read-only). Risks: over-stitching across layered sheets (guard: same tile
and < 2 mm). Cost: none at runtime (same segment count +-5%). Gates: offline metrics above in a CTest on a
fixture style + the PNG review. Deps: none.

## 4. Fibre BSDF (OFFLINE-NOW, then live)
On screen: hair hit by GI/reflection rays gets a coloured specular R lobe and TT/TRT (rim light in backlight,
warm transmission) instead of today's unknown/approximate hair material at secondary hits; with step 6 the
visible hair too. Method: near-field Chiang 2016 / far-field Marschner-d'Eon written from the papers
(longitudinal Gaussian lobes, azimuthal logistic, absorption from colour). Dual scattering (Zinke 2008)
for the many-strand multiple scattering in shadow rays: forward-scattering transmittance and spread from
the strand count along the shadow ray (two scalars per ray), no extra rays. Mapping from CHAR_Hair: Hair_Color
-> absorption sigma_a (Chiang 2016 eq. 9: the colour is the multiple-scattering albedo of a hair volume), Roughness -> beta_m/beta_n,
Scatter / HairBacklit -> TT weight only as a sanity check (physically derived lobes win), cuticle tilt 2-3 deg.
Licence: pbrt-v3 hair is BSD-2 (port with notice) or own code from papers; RTXCR material library only after
reading its NVIDIA RTX SDK licence and recording notices (docs/THIRD_PARTY). Inputs: material parameters
(already in Materials.pak, read-only). Risks: energy: white furnace test must return 1 (no absorption) and
the expected albedo with absorption; brighter hair than the game's (allowed if physical). Cost: +0.1..0.3 ms
per hair hit batch (shading only; traversal unchanged). Gates: offline furnace + lobe tests (CTest) and a
CPU/PT reference render vs the shader on a fixture; live: character energy within spread of the analytic
expectation, backlit shot shows rim (PNG pair). Deps: step 1 (the groom hit carries the fibre tangent).
Prototype 2026-11-03 (RTX_HAIR_INTEGRATION.md 4): energy OK; one-bounce Chiang is pale, needs dual scattering.

## 5. Animation (mostly NEEDS-LIVE)
On screen: strands follow head turns, spring physics and wind exactly as the cards (points are embedded in the
captured deformed card triangles), no lag or pops at LOD switches. Method: already per-frame refit; add a
rebuild when the bounds grow > 20% over the build-time bounds or after a teleport/cut (refit after 2 cm sway
costs 0-5% trace, measured); LOD switch with a one-frame overlap (old BLAS kept until the new one is built);
previous points kept (one extra buffer, 11 MB at LOD 0) for step 6's motion vectors. Inputs: captured cards
(done). Risks: game culls card triangles (NaN-parked: points keep their last position) -> detached strands;
rebuild cost 1.3 ms spikes. Cost: rebuild 1.1-1.3 ms when triggered, +11 MB with previous points. Gates:
offline synthetic sway test in the bench (trace cost vs rebuild); live: dialogue with head turns, `[hair groom]`
build/refit counts, no visible lag in PNG pairs at motion, scene build GPU ms. Deps: step 1.

## 6. Primary replacement of the visible hair (after 1, 4, 5)
On screen: the hair you see is strands: no alpha-tested card edges, no dither/TAA shimmer (the edge flicker
the user reported), correct parallax, proper backlight. Method options: (a) RT primary: one ray per hair
pixel against the groom TLAS inside the game's hair mask (pixels the card raster covers, dilated by the strand
radius), shade with the fibre BSDF, composite into the game's lighting buffer before post; (b) raster strands
(camera-facing ribbons from the LSS points in a draw we inject): cheaper per pixel, needs a draw hook and
depth writes. Recommendation (a): no new raster hook, same geometry as the RT effects. Must provide: depth
(write the groom hit depth into the game's depth or our own for post), normals (fibre tangent-based shading
normal), motion vectors (current - previous point, step 5), DLSS RR guides (diffuse/specular albedo, roughness,
hit distance, specular motion) and DLSS-G (depth/MV consistent with the composite), alpha for DoF (the game's
DoF reads depth: strands must not punch through it; keep DoF on the game's depth if replacing breaks it).
Scalp and accessories keep their game draws. Fallback: card raster for unmatched styles, missing LSS, VRAM
guard. Inputs: the game's hair G-buffer pass and depth (already captured for native hair), its post chain
order (docs/ARCHITECTURE). Risks: high: G-buffer integration, RR noise on sub-pixel strands (strands ~0.25 mm
are sub-pixel beyond ~1 m: coverage AA needs several rays or a footprint-based coverage estimate), DoF/TAA
interplay, look change. Cost: hair pixels x 1 ray: full-screen close-up at DLSS 0.67 render scale (1.6M px)
~0.6 ms trace + 0.2-0.4 ms shading, 2-4 rays per pixel for coverage AA ~1.5-2.5 ms; typical (hair 10-25% of
the screen) 0.2-0.6 ms. Gates: offline: PT reference vs shader on a fixture (coverage, colour); live: same-run
A/B cards/strands, edge-flicker metric (temporal variance on hair pixels) lower, character energy within the
BSDF expectation, RR/FG artefact review (PNG + short float burst), cost < budget the user sets. Deps: 1, 4, 5.

Step 6 design and increments: docs/RTX_HAIR_PRIMARY.md (2026-11-03, started on the user's request).

## Top 3 next steps
1. Live run of `hair_groom` (step 1): proves the join, the energy and the real cost.
2. Batch over all 872 styles on the user's PC (step 2), review the needs-review list; then converter
   stitch/taper (step 3) and an incremental rerun.
3. Fibre BSDF from papers with furnace tests (step 4): needed for both secondary hits and step 6.
