# Nexus beta checklist

Developer checklist. Player instructions: [README.md](README.md).
This is not release approval. [Offline status](RELEASE_OFFLINE_STATUS.md) records receipts and owner questions.
Final controls: [contract](CONTROL_CONTRACT.md). Later qualification receipts supersede its old wf9 hold.

## Release configuration and evidence

- [x] Player README/settings guide describe the final 15 controls, stock visible hair and Ctrl+Shift+F9
  master RT toggle. Ultra is a preset name, not an RTX-model or frame-rate guarantee.
- [x] Document OMM/masks as automatic only with Ultra + Full geometry and available data/hardware.
  Stock-hair pool trim follows capture; all trace resolutions remain Full. No promotion to High is implied.
- [x] Record user-verdict Ultra promotion of temporal depth of field/bloom, 0.152-0.153 ms isolated,
  about 250 MiB reference memory. Preserve paired-test failures; this is not a statistical quality pass.
- [x] Record wf9 08:59 qualification: VALID, 17 every-window gates, static ON equals OFF,
  turning screen hits about 6.7% to 17%, compose median 0.154 ms absolute. Correction follows water.
- [x] Keep Fresnel unpromoted. wsf1 06:44 gates pass, but rim +43% and ON variation 13% versus
  OFF about 2%; 0.10-0.15 ms surface FS is absolute. Owner appearance decision remains open.
- [ ] Independently verify the final merged defaults/schema/generated tables, stale-INI filtering,
  dependency writes and removed water keys against the contract. Stream A owns implementation.
- [ ] Run the final integrated stock-hair Ultra smoke after the configuration/build is frozen.
  Read every-window participation, overflow, OMM, water, driver-budget VRAM, GPU ms and master-toggle gates.
- [ ] Accept stock-hair dialogue/startup behaviour, including first-enable shader compilation.
- [ ] Complete VRAM/transition, Act 2/3, RTX 30/40 and multi-hour acceptance. Redesign the old
  43-minute single-launch protocol to respect the 15-minute launch cap before authorizing it.
- [ ] Verify game/driver versions in the final logs. Recorded 621.01 is a reference, not a minimum;
  game 4.1.1.7631656 is the existing test reference, not proof of compatibility with later patches.
- [ ] Check overlays, GOG Browse, HDR and other graphics mods before claiming compatibility.

Evidence locations: `tools/live_batches/post_temporal/RESULT_pairs_032335.md`,
`water_surface_fresnel/RESULT_wsf1_064436.md` and `water_flicker_wf9/RESULT_wf9_085908.md`
(all under `tools/live_batches/`; wf9 currently read from the main checkout, build `6a00b295`).
Older combined `RESULT_WATER_185612.md` is INVALID and provides exploratory costs only.
Prior OMM/box/bounce/stock-card qualification is recorded in RELEASE_DEFAULTS_REVIEW.md and the
TRACK/native-hair documents; no RESULT file for those historical costs is present in this tree.

## Package and installer

- [x] Review the explicit payload and notice manifest. Runtime DXC, production Streamline/DLSS,
  fonts/OFL, branding/provenance, player docs, Diagnostics.cmd and diagnostics.ps1 are wired.
- [x] Full payload dry-run in worktree scratch compares planned destinations against files.txt.
  It copies no player payload, builds nothing and reports missing build inputs. See offline status.
- [x] Exclude symbols, Python, developer tools, SDK payloads, Aftermath DLLs and development
  Streamline DLLs. Retain applicable compiled-header notices. SHaRC headers remain opt-in/unapproved.
- [ ] Supply the missing lean layer; verify final binary provenance and current build freshness.
  A source audit or placeholder fixture is not a final archive inspection.
- [ ] Inspect the actual final archive and its files.txt, default INI, hashes, notices and private
  symbols separation after owner decisions. Approve upload separately; no upload was performed.
- [x] Run disposable package, legacy release_install, diagnostics and lifecycle fixtures.
  They use synthetic files, child-only process mocks and test scratch; no game or payload EXE runs.
- [x] Wire Inno to the same 229-file ZIP payload; external SHA256 metadata, combined licence
  page, original icon, per-user install, shortcuts and optional finish launch. Pin Inno 6.7.3
  and its distribution hash in `scripts/release/SETUP.md`.
- [x] Add an actual silent setup/update/uninstall RC stage on a throwaway destination:
  229 hashes, settings keep/remove, shader-cache deletion and HKCU Apps-entry cleanup.
  Qualification outcome: see RC_CHECK.md and `docs/progress/inno-setup.md`; adding the stage
  does not itself qualify the compiled installer.
- [ ] Pass compiled Setup RC gate; sign Inno and complete standard-user disposable VM
  wizard/shortcuts/finish-launch, abort, locked-file, low-space and interruption acceptance.
- [ ] Complete fresh-PC/no-VULKAN_SDK and Steam launch acceptance separately. Runtime DXC ships
  in bin; no compiler SDK, Python or browser runtime should be required on the player's PC.

Legacy scripts preserve user settings on update and back up original collisions once to
`.bg3rtx_backup`. Uninstall restores originals; missing/unsafe manifests fail before deletion.
The legacy uninstaller removes runtime logs/captures and shader cache by default. Copy needed
capture evidence elsewhere first. Inno also removes shader cache by default, but keeps settings unless explicitly selected;
reports and previous-version lifecycle backups remain. Direct lifecycle calls keep cache by default.

## Diagnostics and launcher acceptance

- [x] Fixture tests verify a local ZIP, path/contents, no upload, no save by default, redaction,
  effective config/device information, existing dumps and read-only selected synthetic save copying.
- [x] README includes manual reporting fallback and warns that binary dumps/saves cannot be redacted.
- [ ] Manually accept IncludeSave selection/No-default confirmation using synthetic data only.
- [ ] Complete actual keyboard/focus, Apply/restart, WebView 100/150/200% zoom, multi-monitor DPI,
  high contrast and Narrator acceptance. Offline checks do not close these.

## Owner sign-off

- [x] Answer the licence, NVIDIA notification, game-data rights and Microsoft runtime questions
  in [offline status](RELEASE_OFFLINE_STATUS.md). Notices do not grant game-data redistribution.
- [ ] Approve explicit deferral of the unqualified backlog and define supported hardware/content.
- [ ] Review the final archive, README, known issues and deciding evidence before distribution.

No live batch, installation outside scratch or publication is authorized by this checklist.
