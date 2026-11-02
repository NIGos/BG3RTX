# VG2 live analysis - 2026-11-02

## vg2 iteration 3 (analysis only, NOT QUALIFIED)

**Keep rt_vram_guard, rt_vram_compact and rt_capture_elastic off by default.** Recovery and static energy pass. Both guarded turns fail the committed budget gate, and quality during motion is not shown to be unchanged. This bundle test cannot qualify the keys independently.

Sources: `captures/queue/logs/vg2-s8-low-long-view-fg2.1.215241.{log,runner.txt,status.json}`; main checkout `tools/live_batches/vram_guard/{predicted.json,plans/vg2_s8.json,jobs/vg2-s8-low-long-view-fg2.json,README.md}`; trace dumps listed below. Repository guidance read. `results.md` was copied first to `C:\Users\quali\AppData\Local\Temp\vg2-analysis-results-copy.md`; only the copy was read.

Runner/status: done, 14/14 phases, 280 s run / 282 s lock, script-requested close, no exception. Visible unfocused with FG preserved: 516 visibility checks, no foreground or occlusion violations. The copied queue results classify this exact 21:52 run INVALID: turn-on-r and turn-on-l each fail all 5 memory snapshots. No other queue gate failure is listed.

### Memory per plan window

Log MB means MiB. Ranges are periodic `[mem] ours (window)` snapshot minima/maxima, not continuous peaks. B = BLAS reserved; P = mesh pool reserved (used); R = RT capture (alpha region); C = cascade capture; H = recycled hit materials; E/W = emissive/water soups. Consumer ranges need not coincide. Rest includes game, core GI/RR images and untracked memory. Simulated budget is 15640 MB; actual driver budget stays 31419 MB.

| Label | Start-end (21:mm:ss) | Guard/compact/elastic | Process | B | P (used) | R (alpha) | C | H | E/W | Tracked | Rest | Within/over snapshots |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| warmup-on | 53:52.395-54:12.377 | on/on/on | 11825-12142 | 259-323 | 259-512 (193-197) | 416 (171) | 816 | 0 | 128/32 | 1910-2227 | 9913-9915 | 5/0 |
| lower-on | 54:12.377-54:20.408 | on/on/on | 13132-13196 | 704-768 | 517 (289-300) | 544 (217) | 816 | 0 | 256/32 | 2869-2933 | 10262 | 2/0 |
| view-on-a | 54:20.408-54:40.412 | on/on/on | 12704-12960 | 448-704 | 517 (289-298) | 544 (217) | 816 | 0 | 256/32 | 2613-2869 | 10091 | 5/0 |
| turn-on-r | 54:40.412-55:00.420 | on/on/on | 16276-17103 | 2146-3187 | 1018 (792-1014) | 1024 (409) | 816 | 0-160 | 256/32 | 5294-6493 | 10572-10982 | 0/5 |
| turn-on-l | 55:00.420-55:20.417 | on/on/on | 17385-17964 | 3630-4178 | 1018 (854-943) | 768 (307) | 816 | 0-192 | 256/32 | 6520-7260 | 10704-11084 | 0/5 |
| settle-off | 55:20.417-55:30.431 | off/off/off | 18629 | 3172 | 2036 (1439) | 768 (307) | 1281 | 0 | 256/32 | 7545 | 11084 | 0/1 |
| shot-off-a | 55:30.431-55:38.014 | off/off/off | 18409 | 2852 | 2036 (1424) | 768 (307) | 1281 | 0 | 256/32 | 7225 | 11184 | 0/1 |
| shot-on | 55:38.014-55:45.738 | on/on/on | 17564 | 2084 | 2036 (1425) | 768 (307) | 1281 | 0 | 256/32 | 6457 | 11107 | 0/1 |
| shot-off-b | 55:45.738-55:53.007 | off/off/off | 17024 | 1444 | 2036 (1452) | 768 (307) | 1281 | 0 | 256/32 | 5817 | 11207 | 0/1 |
| turn-off-r | 55:53.007-56:13.014 | off/off/off | 17088-18847 | 1585-3520 | 2036-2047 (1886-1945) | 768 (307) | 1281 | 5-328 | 256/32 | 6286-7972 | 10658-10996 | 0/5 |
| turn-off-l | 56:13.014-56:33.030 | off/off/off | 18741-21100 | 3663-5785 | 2047 (1765-1929) | 768 (307) | 1281 | 1-337 | 256/32 | 8303-10172 | 10438-11198 | 0/5 |
| recover-settle-on | 56:33.030-56:58.126 | on/on/on | 15210-21090 | 918-5868 | 1326-2047 (1047) | 768 (307) | 1008-1281 | 0 | 256/32 | 4308-10252 | 10838-10966 | 1/5 |
| recover-on | 56:58.126-57:18.135 | on/on/on | 14762 | 534 | 1326 (1047-1054) | 768 (307) | 1008 | 0 | 256/32 | 3924 | 10838 | 5/0 |
| restore | 57:18.135-57:23.141 | off/off/off | 15122 | 534 | 1326 (1047) | 768 (307) | 1281 | 0 | 256/32 | 4197 | 10924 | 2/0 |

### Refusals and coverage per window

Refusals sum emitted `guard window` attempt counters (B/C/R); they are retries, not allocated MB. Summary counters use emission-time assignment and can straddle phase boundaries. The two attempts reported in settle-off carry over from ON. Scene timeouts are summed only from regular 120-frame summaries. Opaque drops and alpha fallbacks are per-frame averages from those summaries, not total counts. Every complete scene-wait summary inside a gated phase has zero timeouts.

| Label | Refused attempts B/C/R | SL >100 ms | Scene timeouts (summaries) | Opaque drops/frame min-max | Alpha made opaque/frame max |
|---|---:|---:|---:|---:|---:|
| warmup-on | 0/0/0 | 1 | 0 (5) | 0.0 | 0.0 |
| lower-on | 0/0/0 | 0 | 0 (2) | 0.0 | 0.0 |
| view-on-a | 0/0/0 | 0 | 0 (5) | 0.0 | 0.0 |
| turn-on-r | 116/93/0 | 0 | 0 (5) | 0.45-6.4 | 0.0 |
| turn-on-l | 0/159/334 | 0 | 0 (5) | 14.75-36.28 | 0.0 |
| settle-off | 0/1/1 | 0 | 0 (2) | 0.0-0.37 | 0.0 |
| shot-off-a | 0/0/0 | 2 | 0 (1) | 0.0 | 0.0 |
| shot-on | 0/0/0 | 2 | 0 (1) | 0.0 | 0.0 |
| shot-off-b | 0/0/0 | 1 | 0 (1) | 0.0 | 0.0 |
| turn-off-r | 0/0/0 | 1 | 0 (5) | 0.0-1.59 | 0.0 |
| turn-off-l | 0/0/0 | 0 | 0 (5) | 0.0-13.26 | 0.0 |
| recover-settle-on | 0/0/0 | 0 | 0 (7) | 0.0-2.54 | 0.0 |
| recover-on | 0/0/0 | 0 | 0 (5) | 0.0 | 0.0 |
| restore | 0/0/0 | 0 | 0 (1) | 0.0 | 0.0 |

No Streamline CPU-fence timeout appears anywhere. Of 10 >100 ms warnings, 3 precede the plan, 1 occurs in warmup-on, 2 each in shot-off-a and shot-on, 1 in shot-off-b and 1 in turn-off-r. Zero occur in the four gated ON windows. Tracing itself stalls presentation; the shot warnings do not establish a normal-rendering pacing regression. Indirect and non-list RT capture drops are zero in plan summaries. Load summaries before the plan contain 16 scene-wait timeouts; no plan summary has one.

### Static energy from float scene colour

Main `tools/m4_promotion_gate.py` identifies `SCENE_FS=5c3fdabc`: lit HDR scene before transparents and post. These are slot-0 R11G11B10F dumps at 2560x1067, FS `5c3fdabc477f316a`. Luminance = 0.2126 R + 0.7152 G + 0.0722 B, using float RGB means in dumps.json, not PNGs or bloom. Exposure held at EV 11.5282; all trace-frame lighting probes have s1b13[0] = 0.000282149. Exposure normalisation leaves relative deltas unchanged.

| Window | Trace frame | Capture ID under test-main/captures | Dump | Float mean RGB | Mean luminance | Nonfinite |
|---|---:|---|---|---|---:|---:|
| shot-off-a | 4503 | 20261102-215534 | d163 | [0.216312, 0.18521, 0.18069] | 0.1914959412 | 0 |
| shot-on | 4631 | 20261102-215542 | d368 | [0.217681, 0.186165, 0.18151] | 0.1925292106 | 0 |
| shot-off-b | 4761 | 20261102-215550 | d565 | [0.215791, 0.185037, 0.180672] | 0.1912601474 | 0 |

OFF average 0.1913780443; **ON vs mean OFF +0.6015%: PASS** against predicted 0 +/-0.92%. ON vs OFF-a +0.5396%; ON vs OFF-b +0.6635%; OFF-b vs OFF-a -0.1231%. This supports static energy preservation, not unchanged geometry or temporal quality during turns.

### Predicted gates and interpretation

| Gate / prediction | Decision and evidence |
|---|---|
| All measured ON windows within 15640 MB | **FAIL.** view-on-a passes; right turn 16276-17103 MB and left turn 17385-17964 MB: 5/5 periodic snapshots over in each. A right-turn refusal snapshot reaches 17259 MB. Recovery passes. |
| Turn refusals >0 | PASS. BLAS/cascade/RT growth refused; per-draw BLAS refusal logs explicitly say the draw is left out that frame. |
| OFF turns over simulated budget | PASS (report). Both exceed 15640 MB. This OFF state follows ON in one run, so it is not an untouched independent baseline. |
| recover-on within budget after 25 s settle | PASS. All five periodic snapshots 14762 MB, 878 MB below budget. First snapshot at 21:56:58.404 precedes 1 s event settling; all four later snapshots pass too. |
| No SL >100 ms or fence timeout in measured ON | PASS. Zero in view-on-a, turn-on-r, turn-on-l, recover-on. |
| Every gated scene-wait window has 0 timeouts | PASS. Includes OFF turns and recovery settling; complete-window summaries show zero. |
| Mesh/BLAS reserve accounting; >=3 recovery repack snapshots without overlap | PASS. 2 cumulative repacks; pool copy/retirement 0 throughout recovery snapshots, including all four after settling. |
| Static energy 0 +/-0.92% | PASS. ON vs mean OFF +0.6015%, zero nonfinite scene pixels. |
| RT opaque drops >0 in turns, ON and OFF | Observed, explicitly report-only. ON peaks 6.40/36.28 drops per frame; OFF peaks 1.59/13.26. Missing capture is real; unequal view/history prevents attributing the entire difference to one key. |
| Pool estimate 1312.5 MB | Actual 1326 MB (1047 MB current used), published 2047 ->1326 MB at 21:56:41.635. Retained peak/headroom target differs from the prior-run estimate; no failed retirement. About 721 MB released. |
| Cascade estimate 1056 MB | Actual recovery 1008 MB, below estimate. Restore disables elastic and returns to 1281 MB, outside ON gates. |
| BLAS <=2102 MB after retirement | PASS in measured recovery: 534 MB reserved, 374 MB live. Motion exceeds it (4178 MB periodic peak); it is not an instantaneous bound on exempt and in-flight allocations. |
| RT <=1024 MB, recycled hit-material <=254 MB after retirement | PASS: recovery RT 768 MB, H 0. Emissive/water 256/32 MB, measured, no fixed target; tracked total 3924 MB. |
| Pool copy/retirement overlap 0 after recovery | PASS. Snapshot zero does not rule out short replacement overlap between samples. |
| Disk estimate 190 MB | Estimate, not a gate with a tolerance. Three traces completed; no quality decision depends on byte count. |

### Real limitations versus simulated pressure

The simulation changes our policy, not the physical GPU or the game's allocation strategy. Driver budget remains 31419 MB. Rest in ON turns stays roughly 10.6-11.1 GB and is 10838 MB in measured recovery; it includes core images/untracked memory as well as the game. The game does not shrink its VRAM simply because our guard is told 15640 MB. Thus the committed simulated-process gate fails, but this is not evidence of actual physical OOM/paging on this card. No device loss or Streamline out-of-VRAM warning is present.

The overshoot cannot all be excused by the simulation. Our tracked total grows from 2613 MB in the last view snapshot to 7260 MB in the left turn; BLAS reserve grows 448 ->4178 MB. At 21:55:12.315 the mesh-cache summary reports reserved/live/pending 4077.7/867.3/588.3 MB and 1342 compactions in that window. That is real physical reserve/retirement pressure. Main layer/bvhsplit.cpp:225-250 explicitly exempts compaction copies and cascade/sun BLAS (guard_what=nullptr) from refusal. Guarded per-draw refusals therefore do not imply every chunk allocation is capped. These logs cannot assign each excess chunk to a particular exempt path. Strict process-budget enforcement during motion remains a failed candidate requirement.

Recovery does free memory: process 21090 ->14762 MB and tracked 10252 ->3924 MB, with rest 10838 MB at both endpoints. BLAS 5868 ->534 MB, pool 2047 ->1326 MB, and cascade 1281 ->1008 MB account for all 6328 MB saved. This run does not demonstrate a persistent pool-retirement leak. The restore increase after turning elastic off is expected.

Both pending-producer-boundary and detached-slot replacement events occur for RT/cascade capture, so this is actual replacement, not only target publication. Full new allocation can still be refused while old allocations survive. Opaque drops and rejected per-draw BLAS omit RT geometry and can affect shadows/reflections/GI even when global mean energy is stable. Quality unchanged is therefore unproven. Keep all three keys experimental/off by default; the bundled run does not isolate an independent compact/elastic promotion. No new live test is requested or queued here.

### GPU ms lines per window

RT and GI lines below average 300 frames (~10 s at 30 rendered fps). Assignment is by emission timestamp: early lines can contain preceding-window work. Short shot-on has no standalone RT/GI line. These are descriptive costs, not a clean overhead estimate for each key. Do not assume GI and rt passes form a non-overlapping sum. Use GPU ms here, not unfocused rendered frame time.

| Window | RT passes (timestamp: ms/frame) | GI (timestamp: ms/frame) |
|---|---|---|
| warmup-on | 21:54:02.203: 4.643; 21:54:12.204: 4.673 | 21:54:01.268: 7.845; 21:54:11.266: 7.989 |
| lower-on | no line | no line |
| view-on-a | 21:54:22.212: 4.751; 21:54:32.211: 4.793 | 21:54:21.275: 8.430; 21:54:31.275: 8.584 |
| turn-on-r | 21:54:42.218: 4.799; 21:54:52.344: 5.253 | 21:54:41.274: 8.571; 21:54:51.359: 8.782 |
| turn-on-l | 21:55:02.455: 5.470; 21:55:12.566: 5.341 | 21:55:01.465: 9.058; 21:55:11.626: 8.938 |
| settle-off | 21:55:22.569: 5.664 | 21:55:21.628: 9.216 |
| shot-off-a | 21:55:32.568: 6.848 | 21:55:31.628: 11.023 |
| shot-on | no line | no line |
| shot-off-b | 21:55:49.296: 6.937 | 21:55:48.359: 11.137 |
| turn-off-r | 21:56:02.595: 5.718; 21:56:12.589: 5.445 | 21:56:01.654: 9.649; 21:56:11.652: 9.048 |
| turn-off-l | 21:56:22.592: 5.559; 21:56:32.589: 5.486 | 21:56:21.653: 9.289; 21:56:31.653: 9.085 |
| recover-settle-on | 21:56:42.589: 6.083; 21:56:52.589: 6.075 | 21:56:41.652: 9.596; 21:56:51.651: 9.612 |
| recover-on | 21:57:02.590: 6.060; 21:57:12.589: 6.077 | 21:57:01.651: 9.594; 21:57:11.652: 9.610 |
| restore | 21:57:22.589: 6.060 | 21:57:21.652: 9.594 |

Other GPU lines summarised below as the range of logged medians/means in ms (not the distribution across individual frames). Missing means no line in that phase. Post lists render/output separately; water reflection/compose lists their separate ranges. Shared water-shadow/refraction diagnostics repeat the compose timer and must not be added to it. No dedicated guard/compact/elastic GPU timer is logged.

| Window | GI trace | Local shadows | Reflections | RR | FG input | Post render/output | Scene build | Water reflection/compose | Emission check |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| warmup-on | 4.094-4.096 | 0.210 | 2.835-2.838 | 2.362-2.375 | 0.026-0.029 | 0.140-0.142/0.059 | 0.879-1.142 | 0.122-0.127/0.411-0.416 | 0.011-0.012 |
| lower-on | none | none | none | 2.379-2.381 | 0.028-0.029 | 0.144-0.160/0.059-0.061 | 1.278-2.041 | 0.107-0.112/0.355-0.356 | 0.012-0.013 |
| view-on-a | 4.143-4.154 | 0.372-0.374 | 3.144-3.148 | 2.375-2.382 | 0.029 | 0.160-0.162/0.059 | 1.282-1.450 | 0.106-0.107/0.355 | 0.013 |
| turn-on-r | 4.152-4.551 | 0.373-0.395 | 2.980-3.137 | 2.387-2.401 | 0.029 | 0.162-0.164/0.060-0.064 | 1.295-3.106 | 0.106-0.114/0.338-0.373 | 0.012-0.013 |
| turn-on-l | 4.572-4.699 | 0.393-0.395 | 3.021-3.068 | 2.394-2.421 | 0.029 | 0.162-0.164/0.062-0.065 | 1.645-3.264 | 0.108-0.112/0.338-0.380 | 0.012-0.013 |
| settle-off | 4.891 | 0.352 | 3.108 | 2.397-2.415 | 0.029 | 0.161-0.162/0.059-0.061 | 1.765-1.871 | 0.108/0.379-0.380 | 0.012 |
| shot-off-a | 6.088 | 0.345 | 3.553 | 2.401 | 0.029 | 0.162/0.059 | 1.867 | 0.108/0.381 | 0.012 |
| shot-on | none | none | none | 2.437-2.439 | 0.030 | 0.161-0.162/0.063 | 1.888 | 0.108/0.381 | 0.012 |
| shot-off-b | 6.131 | 0.349 | 3.567 | 2.416 | 0.029 | 0.161/0.060 | 1.880 | 0.108/0.380 | 0.012 |
| turn-off-r | 4.794-5.010 | 0.385-0.390 | 2.957-3.130 | 2.381-2.416 | 0.029 | 0.162-0.164/0.060-0.062 | 1.864-3.314 | 0.108-0.113/0.338-0.380 | 0.012 |
| turn-off-l | 4.830-4.952 | 0.395-0.401 | 2.887-3.156 | 2.382-2.390 | 0.028-0.029 | 0.161-0.162/0.060-0.061 | 1.429-3.585 | 0.102-0.115/0.339-0.357 | 0.012 |
| recover-settle-on | 5.265-5.269 | 0.424-0.425 | 2.824-2.827 | 2.398-2.406 | 0.029 | 0.160-0.161/0.060 | 1.558-3.064 | 0.124-0.126/0.434-0.437 | 0.012 |
| recover-on | 5.255-5.265 | 0.422-0.424 | 2.824-2.825 | 2.402-2.410 | 0.029 | 0.160-0.161/0.060 | 1.562-1.575 | 0.126/0.436-0.437 | 0.012 |
| restore | 5.263 | 0.424 | 2.823 | 2.396 | 0.029 | 0.159/0.059 | 1.529-1.564 | 0.124-0.126/0.435-0.437 | 0.012 |

Analysis only. Only this report was added in the worktree. No source/settings change, build, test, live run, queue mutation or commit.
