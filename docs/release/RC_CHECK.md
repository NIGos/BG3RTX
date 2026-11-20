# Release candidate check

Date: 2026-11-20 19:58:57 +01:00. Commit: 08cb1e995bbefcd1328c192602d0b10f3246fa2b.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS with 1 skipped row. Candidate version: rc-20261120-08cb1e99-19cabd47.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 73.72 MiB; SHA256 e2b3f74ac526ba0da19eb395d989e332317597ab352dbe995c1cbdaac0b5955d; symbols kept in separate archive. |
| Manifest | PASS | 230 files; expected 230 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. Runtime source dependencies covered. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 227 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 16 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261120-08cb1e99-19cabd47; commit matches HEAD; layer lean. |
| Install | PASS | 230 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 230 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | SKIPPED | skipped: another BG3RTX install present (C:\Users\quali\AppData\Local\Programs\BG3RTX\, 0.9.6-hairtest18 from VERSION.txt; Apps entry says 0.9.6-hairtest10). The compiled Setup was not run, so it is not qualified here: run verify_rc on a PC without a BG3RTX install, or uninstall it first. |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: E:\Storage\Code Projects\BG3RTX\.scratch\rc\20261120-195709-19cabd47\BG3RTX-rc-20261120-08cb1e99-19cabd47.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.

## Release readiness: 0.9.6-beta (refreshed 2026-11-20 for main 08cb1e99)

Hand-written; `verify_rc.ps1` keeps this section when it rewrites the table above. Not a release approval.
User decision 2026-11-15: finalise for release, RTX Hair Off by default. Nothing is uploaded, tagged or installed by agents.
Notes: [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md), sources in [RELEASE_EVIDENCE_0.9.6.md](RELEASE_EVIDENCE_0.9.6.md).

### Candidate (built offline from main 08cb1e99, lean layer, not installed, not run)

| Artifact (main checkout) | Bytes | SHA-256 |
|---|---|---|
| `dist/BG3RTX-0.9.6-beta.zip` (230 files) | 77681850 | a702c117bcbbfe3152554ba440f2543d992476103aa04a8c09661b5aee8bb9eb |
| `dist/BG3RTX-0.9.6-beta-symbols.zip` (private) | 80394488 | 5fc43e9415d03d607c45e7f27d6780c8a5810f532e9605d9f21568a55881f135 |
| `.scratch/setup-0.9.6-beta/BG3RTX-0.9.6-beta-Setup.exe` (Inno 6.7.3, unsigned) | 57688061 | 5e33db7aa74d6e29d24986b7c880a1891d31c1cef09834a58fc3f3926b6015b3 |

`VERSION.txt`: BG3RTX 0.9.6-beta, commit 08cb1e99, layer lean, built 2026-11-20 19:59.
`.scratch/setup-0.9.6-beta/SHA256SUMS.txt` lists these three hashes. Older candidates: `.scratch/old_rc_745a1fff/`.
Since 976df3df the payload gained the groom decode fix, the tracker map erase and handle-reuse fix, `refresh()` once
per frame, the NaN-safe giRaw clamp and the round 2 skipped-work fixes (all on, no key), and five keys at 0 in the
packaged `bg3rtx.ini`: `rt_hold_pack_base`, `rt_hair_header_early`, `gi_far_roof_emission`, `rt_as_size_class`,
`rt_cascade_keep_refused`.

### Gates and evidence

| Gate | Result | Evidence |
|---|---|---|
| Build and all CTests | PASS | merge-green of 08cb1e99 ("full gates passed"); claude/r2-core 238 of 238 before it |
| verify_rc (table above) | 11 PASS, 1 SKIPPED | rc-20261120-08cb1e99-19cabd47. Setup row skipped: another install present (0.9.6-hairtest18 from its `VERSION.txt`; the Apps entry says hairtest10). See "Not verified" 6 |
| RTX Hair Off in Low, Medium, High, Ultra and without a user file | PASS | `tests/settings_test.cpp` `test_release_rtx_hair`; the packaged `bg3rtx.ini` reads `rt_hair_groom_primary=0`, `rt_hair_capture=0` |
| RTX Hair On from the launcher survives the release policy | PASS | same test: all 26 bundle keys, 4 presets, experimental options 0 and 1; policy idempotent |
| Release controls | PASS | 23 visible controls plus 4 behind "Show experimental options"; 32 developer knobs hidden (`settings_test`, `launcher_web_bridge_json_test`) |
| Developer keys off in the package | PASS | every `data/release.ini` pin is the last value in the packaged `bg3rtx.ini` (`dev=0`, `rt_hair_dense_sample=0`); `rt_hair_card_fill`, `rt_hair_groom_async`, `rt_as_chunk_fit`, `rt_hold_pack_base`, `rt_hair_header_early`, `gi_far_roof_emission`, `rt_as_size_class`, `rt_cascade_keep_refused` are 0 and have no release control |
| New keys of 2026-11-20 | READY_FOR_LIVE, off | `rt_hold_pack_base`, `rt_hair_header_early` (`tools/live_batches_staged/final_layer/`), `gi_far_roof_emission` (`tools/live_batches_staged/r2_tools/`, inn PT-reference A/B), `rt_as_size_class` + `rt_cascade_keep_refused` (`tools/live_batches_staged/r2_layer/`, inn dolly). None has run |
| Preset contents | PASS | Low-latency scene On in High and Ultra with the hit wait pinned; motion stability On in High and Ultra; four VRAM policies 1 in every preset |
| Licences | PASS | `LICENSE`, `LICENSE-EXCEPTION.md`, `THIRD_PARTY.md` and the `licences/` files byte-equal to the repository; verify_rc Licences row |
| Exclusions | PASS | 0 `granny2*`, 0 PDB, 0 Python, no DLSS 5 runtime (only its MIT bridge notice and codec shader); verify_rc Exclusions row |
| Shader identity of the 2026-11-20 core changes | PASS | claude/final-core: 225 / 225 requests byte-identical after the dead-code removal; NaN clamp: only the 28 hit-GI requests change, in the final select; `gi_far_roof_emission` off: "Not verified" 15 |
| Final smoke staged | READY_FOR_LIVE | `tools/live_batches_staged/release_final/`: frozen lint 5 jobs, 0 failing; live preflight 5 jobs, 0 failing |

### Not verified

1. **Launcher in the real WebView2 window.** Seen by the user in the installed app (hairtest 12 to 18), as the user
   states on 2026-11-15; no agent-side run (headless Edge only); no defects reported so far. Not a blocker.
2. **No qualification live in the release profile since 2026-11-14/15** (scene handout default, VRAM policies, hair
   fixes, launcher, and the 2026-11-20 layer and core changes). The lives of that night ran Ultra with RTX Hair On and
   FG 3x, not the shipped default. Smallest final smoke, staged and not queued: `final-s8-ultra-high` (Water Queens
   House, Ultra then High, about 3.8 minutes) and `final-stock-hair-dialogue1` (Test Dialogue1, about 3.4 minutes);
   `predicted.json` is committed. It needs a user go, the branch in test-main and a warning right before the runner
   starts. **Blocker** by the project's own rule (a feature that ships is tested in the real preset), unless the user
   accepts their own play sessions with hairtest18 as the acceptance (the user played it on 2026-11-20 and found
   everything fine; hairtest18 is 1c09e039: it lacks the 2026-11-20 changes). Lives are paused by the user (2026-11-20). Limits of the staged jobs: few valid logs of one save are left as lint corpus, Dialogue1 has no recent
   valid log, and three expected lines (FG 2x presented, the master toggle, the OMM check) match only the frozen snapshot.
3. **Hitch frames with RTX Hair** are fixed offline only, never seen live. Not a blocker: RTX Hair ships Off and Beta.
4. **EXPLAIN: GI trace read 0.2 to 0.4 ms lower with the handout in one rerun.** Not a blocker: energy equal and 0
   fallbacks in the qualification; claude/final-core read it as benign (another specialization, constant 1051).
5. **BLAS chunk reserve after a dolly** (2.3 to 2.7 GB for 0.65 to 0.8 GB live at the inn, back to about 1 GB after
   15 s still). Not a blocker: known issue in the notes. The `[mem] ours` line now splits the reserve: read it in
   the next user log.
6. **Compiled Setup route on this PC.** verify_rc skips the row because the user's install (0.9.6-hairtest18) owns
   the Apps entry; the setup was compiled and hashed but never executed. Last pass of that row: 2026-11-07
   (rc-20261107-efc9f6d6). The ZIP install, update and uninstall rows pass. Not a blocker for the ZIP;
   **blocker for publishing the Setup.exe** until the user installs it once over hairtest18 or the row runs on a PC
   without an install. The installer is unsigned (SmartScreen will warn).
7. **VRAM guard recovery** is unconfirmed in a long play session; the user's log of 2026-11-20 peaked at 27.1 GB
   used (ours 8.7 to 15.3 GB) with one short LOW period. Not a blocker: the notes say so.
8. **RTX Hair cost.** One player session with RTX Hair On reported a large frame-rate loss; `perf_ab` is staged and
   never ran. Two entering-head card runs of 6 and 9 frames in hx1 202905 are unexplained. Not a blocker (Off, Beta).
9. **ReShade.** The player README says ReShade 6 works (tested 2026-11-05); the rs1 run of 2026-11-14 was INVALID.
   Not a blocker; the claim rests on the older test.
10. **"Softer look at distance"** in the notes' RTX Hair limits is backed offline by the pixel-LOD study
    (docs/RTX_HAIR_PRIMARY.md 5v; cited in RELEASE_EVIDENCE_0.9.6.md). One groom, no motion, no live comparison.
11. **Scope never tested:** any card other than the RTX 5090, any driver other than 621.01, Act 2 and most of Act 3,
    sessions of several hours, a fresh PC without the SDKs, and the native fallback window's Beta label for RTX Hair
    (the badge is in the WebView page only). Not blockers for a beta that says "Tested only on RTX 5090".
12. **Player without a user file** (game started before the launcher saved once): the packaged defaults resolve to
    High without OMM and offline masks (the policy enables them only with `preset=high` or `ultra`). The launcher
    writes the suggested preset on first use. Not a blocker; never run live.
13. **Strand-variant pipeline creation at start: 15 to 50 s** in 18 of 25 queue launches and 17.4 / 19.6 s in two
    user logs, also with RTX Hair off (`layer/tracker.cpp` makes the variants for every hair pipeline). A known issue
    in the notes; the fix `rt_hair_header_early` is off and READY_FOR_LIVE. Not a blocker; the largest start cost left.
14. **Groom decode fix** (a style decoded up to 9 times under a refused pool growth): offline only. Check in the next
    user log: no LOD id with several "decoded" lines before its "uploaded" line. Not a blocker (RTX Hair Off).
15. **`gi_far_roof_emission` off is the old program.** claude/final-core showed the 6 `hit_sun_em` requests equal to
    their baseline with constant 1054 frozen to 0; the tool run of that proof crashed in its exhaustive cross. The
    tool is fixed on claude/r2-tools (it names the request and pass and continues; `--current REV` compares one
    commit with its parent). Not a blocker: the key is 0, and when it is off core/gi.cpp does not add constant 1054.
