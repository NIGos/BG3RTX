## Tileset release choice prepared (branch tileset-qual, READY_FOR_LIVE, 2026-11-04)

- Prepared ts1-native-tileset under tools/live_batches/tileset_qual; no game, queue or Git writes.
- Retained inventory: Dialogue3 132 Tileset G-buffer draws, 48 supported material draws and 56 supported depth draws; largest identified save among 101 retained captures.
- rt_native_tileset is pinned on every config reload. Two separate OFF/ON launches are required; a live 0/1/0/1 would be false evidence.
- Release Ultra + Hair shadows; each plan 99.6 s plus 120 s load allowance, 3.66 min/launch, hard cap 4.9 min. Two static PNG traces, estimated 140 MB.
- Gates: ON complete records and GPU family2/native matches positive, unknown/incomplete 0; scene timeouts, RT/alpha overflow and material table overflow 0; coverage and RT/GI GPU ms per state.
- Float pre-post energy/exposure reported; no matched Dialogue3 PT reference found. Cross-launch view/exposure confounds remain; energy and quality await the user's look at both static PNGs.
- Offline: 2/2 job preflights, scene-wait/capacity lint, both fake-queue negative and positive paths, and 3 synthetic tests pass. Receipts: tools/live_batches/tileset_qual/validation.json and lint.json. Historical replay is UNPROVEN.
- Pending live evidence is not a quality pass. Tilesets stays off until the user accepts it. ABI and shader requests unchanged (+0).
