# BG3RTX progress - 2026-11-11 (main 9b83a578)

Live tests are PAUSED by the user (2026-11-11, flag `.scratch/coord/PAUSE`). No live without a fresh explicit go.
State and next steps: [HANDOFF_SESSION](HANDOFF_SESSION.md). RTX Hair design and history: [RTX_HAIR_PRIMARY](RTX_HAIR_PRIMARY.md) 5v-6f.
Full text of every entry below (about 2900 lines): [archive](archive/PROGRESS_CODEX_2026-11-11.md).

## Newest: RTX Hair, 2026-11-10/11

### Motion stability ON in High and Ultra (user decision, branch claude/hair-mv-preset)
- Control "RTX Hair motion stability" = `rt_hair_mv_card=2`. On in High and Ultra, Off in Low and Medium; release.ini pin removed.
- Gates: hm1 210705 camera-only share 11% (off 98%), hair/ring ratio 0.77; hf2 000619 pan 3.2%, ratio 1.00. Strand G-buffer 0.22-0.31 ms.
- Turning it on needs a game restart (specialization 1049). User check: Ultra, S8, walking characters or turning camera: no hair smear.

### Detail at distance: pixel LOD (branches claude/hair-pixel-lod*, claude/hair-detail-knob)
- Control "RTX Hair detail at distance": Off / 1/4 / 1/8 (keys rt_hair_pixel_lod, rt_hair_pixel_lod_small_keep). Presets Off, applies live.
- Heads 35-60 px get keep 1/4 (1/8 optional, about +2% outline). Under 35 px unchanged. Above 60 px the k rule over-covers about +15% (open).
- pl1 211046: strand G-buffer -27% (1/4), -34% (1/8). Camera BLAS about 16 MB per groom at 1/4, never compacted (possible saving).
- Image gain not resolved by the shots (poses change between shots); no hole, no card fallback, frame energy unchanged.

### Holes under a fast camera dolly (branch claude/hair-holes-root, READY_FOR_LIVE)
- Cause 1 found and fixed behind `rt_hair_header_cam_gpu=1`: header camera was a CPU read, now copied on the GPU. hl1 000345: coverage 2-7% -> 50-70%.
- Remaining 30-50% loss is open. Disproved: another build's TLAS, row order, full BLAS rebuilds, mesh pool reuse, VP eye, stale publish
  (k=1 is normal; `rt_hair_stale_publish` cannot act, retire it), newer build (build stamp hl1 003637), BLAS refit, game LOD switch, VP mismatch as stated.
- Next: hl1 with `rt_hair_dense_sample=1` (spec 1052), staged in `tools/live_batches_staged/hair_late/`, predicted.json lists the signatures.
  The hair G-buffer shader is not byte-identical with the key off (spec-constant branch); merge-green shader gates passed.
- Other findings (6c/6d): misleading counters ("own build frame's publish", "header/BLAS mismatch" always 0 with `rt_hair_groom_primary_current=1`);
  rejected-after-hit is frame-wide; the CPU camera also feeds the cascade caster rebase and groom LOD bounds; `lint_snapshot` shrinks the snapshot (needs a merge mode);
  possible full build on compacted BLAS storage at the `layer/bvhsplit.cpp` refit fallback.
- hf2 final qualification job `tools/live_batches_staged/hair_final/` (hole gate report-only) does not yet set `rt_hair_header_cam_gpu`.

### Other 2026-11-10 hair work (one line each)
- hair-v3-settle: nearest-strand trace for a groom joining while others swap. hair-swap-transitions: switch counter, retry latch.
- hair-stale-publish / stale-camera: stale check built, found inert (see above). hair-slot-identity, hair-shared-key, hair-row-stable: stable slots, rows, one-key watchdog.
- hair-release-policy: RTX Hair On was inert in release, policy reset fixed. hair-mv / mv-burst: strand motion vectors (rt_hair_mv_card).
- hair-keys-default, hair-nearest-default, hair-bundle-upgrade, hair-audit: RTX Hair On bundle keys and upgrade path.
- hair-coverage-cause, hair-groom-coverage, hair-frame-miss, hair-cam-step, hair-arrival-cause, hair-late-arrival, hair-pool-reuse, hair-fg-job: hole investigation steps and diagnostics.

## Current user decisions

- Stock visible hair ships; strand hair hidden until its post-release update.
- Experimental page: only Water surface reflections (Fresnel), off.
- The game-derived asset database ships in the archive (user's own risk on data rights).
- GPL-3.0-or-later plus NVIDIA exception; no NVIDIA notification. "Tested only on RTX 5090".
- Nexus: setup in a ZIP (main file) plus portable ZIP; manual download, no Vortex.
- Lean: only the tests that are really needed; one final live on the user's go.
- 2026-11-11: RTX Hair motion stability is On in High and Ultra. Live tests paused ("non avviare altre live") until a fresh go.

Full folded branch notes: [2026-11-05](archive/PROGRESS_CODEX_2026-11-05.md), [2026-11-04](archive/PROGRESS_CODEX_2026-11-04.md).
- Optimisations that qualify are always on; no player option.

Full notes of the night: [archive](archive/PROGRESS_CODEX_2026-11-05-night.md); release 0.9 report: [archive](archive/PROGRESS_CODEX_2026-11-05.md).

## Older entries (2026-11-04 to 2026-11-09, one line each; full text in the archive)

- RTX Hair strand/card flicker while turning: header camera from the GPU (branch claude/hair-header-cam, READY_FOR_LIVE, 2026-11-09)
- rt_capture_wide_split on by default (branch claude/wide-split-on, 2026-11-09)
- Strand resolve finds its frame's snapshot under a moving present (branch claude/strand-resolve-fallback, offline, READY_FOR_LIVE, 2026-11-09)
- Strand resolve paired with its own frame's snapshot by submission order (branch claude/strand-pair, offline, READY_FOR_LIVE, 2026-11-09)
- Scene handout design (branch claude/scene-handout-design, design only, 2026-11-09)
- Scene handout increment 5: traversal flag (ABI 31) and water on the handout (branch claude/scene-handout-5, 2026-11-09)
- Scene handout increment 4b: native hit materials under the handout (branch claude/scene-handout-4b, READY_FOR_LIVE, 2026-11-09)
- Scene handout increment 4: OMM on reserved slots; hit materials analysed (branch claude/scene-handout-4, offline-verified, 2026-11-09)
- Scene handout increment 3: handout without the scene wait (branch claude/scene-handout-3, READY_FOR_LIVE, 2026-11-09)
- Scene handout increment 2: scene TLAS by stamp address (branch claude/scene-handout-2, offline-verified, 2026-11-09)
- Scene handout increment 1: update-after-bind set 1 layout (branch claude/scene-handout-1, offline-verified, 2026-11-09)
- RTX Hair switchable while playing, hotkey Ctrl+Shift+F7 (branch claude/rtx-hair-live, READY_FOR_LIVE, 2026-11-09)
- Release verification lives 2026-11-09: rise reserve on, capture limits found (branch claude/rise-reserve-on)
- Mesh cache budget charged with its own BLAS (branch claude/mesh-cache-rate, READY_FOR_LIVE, 2026-11-09)
- Mesh cache headroom for the frame's other users; no build fails on hair bounds (branch claude/mesh-cache-rate-2, READY_FOR_LIVE, 2026-11-09)
- Launcher UX: Home dashboard, RTX Hair conversion panel, Hotkeys page (branch claude/launcher-ux, offline, 2026-11-09)
- RTX Hair warm-up after a level load (branch claude/hair-warmup, READY_FOR_LIVE, 2026-11-09)
- rt_hair_warmup on by default (branch claude/hair-warmup-on, 2026-11-09)
- Static-camera strand flicker: diagnosis and staged burst job (branch claude/hair-static-flicker, READY_FOR_LIVE, 2026-11-09)
- Strand edge dithering: strand-aware dither resolve and analytic coverage (branch claude/hair-edge-quality, READY_FOR_LIVE, 2026-11-09)
- Convert all hair styles from the player's own files (branch claude/hair-convert-all, offline, 2026-11-09)
- Diagnostic report: opt-in latest save, Home link (branch claude/diag-save, ready for review, 2026-11-09)
- RT capture stride split: 88 bytes only for strand-frame shaders (branch claude/capture-stride, READY_FOR_LIVE, 2026-11-09)
- RT capture alpha floor yield, BLAS stall report persistence (branch claude/capture-fixes, 2026-11-09)
- Scene slot material records pinned against pruning (branch claude/blas-retire-stall, READY_FOR_LIVE, 2026-11-09)
- BLAS chunk cap by occupancy for oversized BLAS (branch claude/blas-frag, READY_FOR_LIVE, 2026-11-09)
- BLAS chunks capped for unguarded callers; retirement-stall report (branch claude/blas-chunk-growth, READY_FOR_LIVE, 2026-11-09)
- Background runs: the user's mouse no longer reaches the game (branch claude/bg-input, READY_FOR_LIVE, 2026-11-09)
- Release 0.9.5-beta: version bump and changelog (branch rel095, 2026-11-08)
- Packaged 2026-11-08 23:00
- Rebuilt 2026-11-09 (main b3cb21fa): RTX Hair visible in Lighting, launcher Home page
- RTX Hair single option, OMM dyn refusal churn, scene wait cause (branch claude/opt-waits, READY_FOR_LIVE, 2026-11-08)
- VRAM: capture reserve from the observed rise, BLAS chunk reuse (branch claude/opt-vram, READY_FOR_LIVE, 2026-11-08)
- SER / raygen GI trace on game frames (branch claude/opt-ser-gi, offline, 2026-11-08)
- Scene early handout: plan reviewed, not implemented; decision live prepared (branch claude/opt-scene-handout, READY_FOR_LIVE, 2026-11-08)
- Launcher: a selected option turns on what it needs (branch claude/opt-knob-deps, done offline, 2026-11-08)
- GPU cost breakdown, hair fibre timing, fibre compile time (branch claude/opt-gpu, offline, 2026-11-08)
- Runtime capture as the groom converter's source (branch claude/hair-runtime-capture, offline study, 2026-11-08)
- Strand fibre shading for shadow-mapped local lights (branch claude/hair-m4-locals, READY_FOR_LIVE, 2026-11-08)
- Strand hair layer path: review fixes (branch claude/hair-layer-fix, READY_FOR_LIVE, 2026-11-08)
- Strand groom storage, needed set and launcher Preparation card (branch claude/hair-groom-storage, offline study, 2026-11-08)
- Grooms v3, seen list, launcher Preparation (branch claude/hair-groom-storage, implemented offline, 2026-11-08)
- The flash while anything moves: per-frame tags, LOD keep, counter fix (branch claude/hair-flash, offline, off, 2026-11-08)
- Strand hair layer review round 3 (branch claude/hair-fix3-layer, READY_FOR_LIVE, 2026-11-08)
- Strand fibre passes now fire: strand-pixel depth test, sun mask (branch claude/hair-fix3-fibre, offline, READY_FOR_LIVE, 2026-11-08)
- Strand fibre shading: review fixes 7-11 (branch claude/hair-fibre-fix, offline, READY_FOR_LIVE, 2026-11-08)
- One-frame strand dropouts: counter and camera gate (branch claude/hair-dropout, offline, off, 2026-11-08)
- Native strand groom converter bg3rtx_hairconv (branch claude/hair-converter-native, offline, 2026-11-08)
- Layer hair capture writer and groom re-index (branch claude/hair-capture-writer, offline, 2026-11-08)
- Live e2e 2026-11-08 22:40 (hcap3, Test Dialogue1, scratch assetdb without groom 8b32681bb9ecfc30): both VALID
- Ray tracing pipeline GI trace (branches claude/gi-rt-pipeline, claude/gi-rt-pipeline-ultra; on by default, 2026-11-08)
- Hair smoke 2026-11-08 19:10 (Test Dialogue1, Strand hair on screen bundle, after the publish fix 782a6ad4): VALID
- Release 0.9.4-beta: version bump and changelog (branch rel094, offline, 2026-11-07)
- Release candidate 0.9.4-beta PASS (branch rc094, 2026-11-07)
- Packaging allows a checkout on a disk mounted as a folder (branch package-mount-points, verified offline, 2026-11-07)
- Launcher picks the RTX GPU when an integrated GPU is also present (branch launcher-default-gpu, verified offline, 2026-11-07)
- Temporal grain from the hair-ab7 bursts (branch claude/hair-temporal, offline, 2026-11-07)
- Strand swaps never started: prepass/G-buffer card-depth deadlock (branch claude/hair-swap-fix, READY_FOR_LIVE, 2026-11-07)
- Strand flicker and lag: coverage hold, this frame's publish (branch claude/hair-stability, READY_FOR_LIVE, 2026-11-07)
- Strand quality: footprint coverage bound, pair gate bias, item 4 design (branch claude/hair-quality, READY_FOR_LIVE, 2026-11-07)
- Hair M5 participation, M3.4 post depth, M2.2 cascade cards (branch claude/hair-m5-m2, READY_FOR_LIVE, 2026-11-07)
- Hair M3.2 sub-pixel coverage and M3.3 RR hair guides (branch claude/hair-m3-image, READY_FOR_LIVE, 2026-11-07)
- Strand AO transmittance and fibre specular indirect (branch claude/hair-m2-m4-light, READY_FOR_LIVE, 2026-11-07)
- Hair groom: 2-D curve tracer for curly atlases (branch claude/hair-m1-curly, READY_FOR_RERUN, 2026-11-07)
- Hair grooms never joined: BVH split ring failed in the BAR heap (branch claude/hair-join-fix, READY_FOR_LIVE, 2026-11-07)
- Strand grain attribution, card normal and world tangent keys (branch claude/hair-grain, offline, off, 2026-11-07)
- 5g live gates evaluated (branch claude/hair-grain-gates, 2026-11-07)
- build.ps1 prefers VS 2022 (branch build-pin-vs2022, 2026-11-07)
- Suspect review (branch suspects, FIXED_OFFLINE, 2026-11-06)
- S-F05 R11G11B10 boundaries (branch rrpost-r11, fixed offline, 2026-11-06)
- Default Ultra RF replay (branch rf-replay, READY_FOR_LIVE, 2026-11-06)
- Default Ultra RF follow-up (branch rf-opt2, NOT QUALIFIED, 2026-11-06)
- Default Ultra RF mismatch (branch rf-opt, READY_FOR_RECAPTURE, 2026-11-06)
- RF compact probe state (branch rf-on, qualified, 2026-11-06)
- Default Ultra receiver probe deferral (branch rf-default, NOT QUALIFIED, 2026-11-06)
- Launcher defect review (branch review-launcher, PASS offline, 2026-11-06)
- DLSS 5 offline defect review (branch review-dlss5, FIXED_OFFLINE, 2026-11-06)
- Release 0.9.3-beta (branch rel092b, 2026-11-06)
- RC receipt 0.9.3-beta (branch rc093, 2026-11-06)
- Queue cancellation, ReShade isolation and release-policy lint (branch queue-tooling, offline PASS with limits, 2026-11-06)
- L11/L12/L17 submission redo (branch layer-submit2, READY_FOR_LIVE, 2026-11-06)
- FG release audit, Round 3 (branch fgx3, offline verified, 2026-11-06)
- DLSS 5 Release audit (branch dlss5-audit, offline fixes tested, 2026-11-06)
- Core completion contract (branch core-contract2, READY_FOR_LIVE, 2026-11-06)
- Core completion contract audit (branch core-contract, ABI_BLOCKED, 2026-11-06)
- BLAS flags replay (branch blasflags, rejected, 2026-11-06)
- Exact GPU technique scan (branch techscan, analysis only, 2026-11-05)
- Sun wave rejected; far GI attribution (branch sunwave, offline, 2026-11-05)
- SER feasibility study (branch ser-study, FEASIBLE_OFFLINE, 2026-11-05)
- Ultra scene build audit (branch scene-ana, analysis complete, 2026-11-05)
- Early roof rejection restored (branch roofrej, prepared READY_FOR_LIVE, 2026-11-05)
- Exact early roof rejection (branch roof-on, kept, 2026-11-05)
- RF receiver probe live state (branch rf-ls-regs, READY_FOR_LIVE, 2026-11-05)
- Compact RF live batch (READY_FOR_LIVE, 2026-11-05)
- ReShade compatibility: Streamline device init after vkCreateDevice (branch reshade-compat, READY_FOR_LIVE, 2026-11-05)
- Release smoke capture windows (branch relsmoke-fix, kept offline, 2026-11-05)
- GI origin audit (branch origin-opt, rejected, 2026-11-05)
- Raw compressed LS replay (branch ls-pack2, offline verification, 2026-11-05)
- Production Ultra LS replay (branch ls-pack, READY_FOR_LIVE, 2026-11-05)
- Exact local-shadow optimisation (branch ls-opt2, OFFLINE QUALIFIED, 2026-11-05)
- Local-shadow replay feasibility (branch ls-opt, stopped at analysis, 2026-11-05)
- DLSS 5 page and keyboard shortcuts (branch hotkeys, offline verified, 2026-11-05)
- Conservative GI-only TLAS (branch gitlas, rejected, 2026-11-05)
- Compact GI hit state (branch gi-livestate, qualified always on, 2026-11-05)
- Far GI live state (branch farregs, rejected, 2026-11-05)
- Native DLSS 5 phase 2b (branch dlss5, READY_FOR_LIVE candidate, 2026-11-05)
- NR render-size feature retirement (branch dlss5-res, offline verified, 2026-11-05)
- Game upscaler guard: BG3RTX.exe puts the game's DLSS setting back (branch claude/confident-diffie-fb694f, READY_FOR_LIVE, 2026-11-05)
- Ultra GI attribution batch (abandoned after the S8 result, 2026-11-05)
- Hair cascade consistency (branch claude-hair-cascade, NEEDS_IMPLEMENTATION, 2026-11-04)
