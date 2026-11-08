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
  upload path, once per groom LOD. Open: decode on a worker or keep the last decoded LOD 0 if a live shows hitches.
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
