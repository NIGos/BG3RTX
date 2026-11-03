# wf9: S8 compose camera shift

Prepared offline. Never queued. No game launch or live authorization. `predicted.json` precedes the job.
One Water Queens House launch, visible_unfocused_fg, 24 phases / 302 s plus load.
The static energy calibration toggles rt_water_compose_shift 0/1/0/1 at one unmoved camera;
then the moving windows repeat 0/1/0/1 in the same run. Compose stays enabled throughout.
Other water image terms stay off at startup and in every compared window. The outer queue restores INI.
The final phase clears the experimental shift and capture keys and releases exposure.

Turns hold E through each forward motion interval and Q on reverse returns.
The runner's `rotation-input mode=keys` gates require an actual key press in each
cost window. Check adjacent presented views/minimap before scoring; camera jumps
or water leaving view invalidate the moving comparison. The 10:08 camera probe
showed Q/E movement in 120/120 samples while posted mouse turns jumped.
Return windows are approximate: compare static energy only at the unmoved camera.
Both static ON windows require build-camera delta average < 0.01 m, with nonzero
samples. This rejects the 13:10 buffer-layout regression (294.80 m at rest).

Four capture-free cost-turn windows collect compose stability, compose tiers, water reflection stats
and compose GPU ms with expect_every. Read compose dispatch ms separately from the lighting copy.
All ON windows must satisfy the prediction, not just one. Existing scene-wait and capacity gates apply.
Tier 1 near 25% refers to debug 25 covered-water pixels; the compose stats screen-hit percentage
uses traced rays as its denominator. Never substitute water_reflect tiers for compose tiers.
Predicted ON hidden share: about 7-10%; static energy unchanged within the static OFF-repeat band.
No quality pass or GPU-cost claim is made before that authorized live check.

Raw 99100: static debug 25 calibration. Raw 99101/99102/99103: turning shift 1/0/1, 8 frames each.
Beauty: static OFF then turning 0/1/0/1, 16 presents each. Energy: one trace per static window;
read pre-post float means from dumps.json with energy_fs=water-compose (scene plus in-frame delta),
never PNG brightness. Use OFF windows 0 and 2 to derive noise before comparing ON windows 1 and 3.
Use the existing scripts.qa.reflection_toggle metric with 99100 as calibration on each moving raw burst.
The exact tier population and hidden-share predictions remain deciding observables; presented flicker
is supplementary. The short raw windows are dedicated diagnostics, outside GPU-cost windows.

Presented frame numbers are global across folders: p00-p79, not p00-p15 in every folder.
After the run, supply the five emitted beauty folders in capture order and verify all files:

```powershell
$beauty = @('<static>', '<turn-0>', '<turn-1>', '<turn-2>', '<turn-3>')
for ($i=0; $i -lt $beauty.Count; $i++) {
  $expected = @((16*$i)..(16*$i+15) | ForEach-Object { 'p{0:00}.png' -f $_ })
  $actual = @(Get-ChildItem -LiteralPath $beauty[$i] -Filter 'p*.png' | ForEach-Object Name | Sort-Object)
  if (Compare-Object $expected $actual) { throw 'INVALID: missing or extra presented PNGs' }
}
```

Budget: four 8-frame selective debug bursts (288 B/render pixel), eighty half-display RGB PNGs,
four PNG trace sets and logs: <=1,914,323,840 B at render <=2560x1440 pixels and display <=3840x2160.
The wrapper checks counts and byte bounds before the common fake queue check.
Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/water_flicker_wf9/dryrun.ps1`.
Synthetic authorization receipts exist only in temporary .queue-fake-root directories and permit no real run.
Keep this folder outside every watched captures/queue/pending inbox.

Camera probe preparation: energy-static-0..3 now last 14 s (motion_start/end 14).
Turns now hold E (Q on reverse returns) for the motion interval using `turn_mode=keys`.
The 10:08 probe showed Q/E moving in 120/120 samples; posted mouse modes jumped tens of degrees.

Prewarm shift 1 for 5 s before the first measured window (shift 0 warms at startup).
Lint assumptions: 30 rendered FPS, 1 s settings latency, 1 s per trace stall.
Load-time roof overflow is rejected with zero tolerance in measured windows.
Held Q/E turns and selective capture counts are unchanged. No generator exists.

18:56 run: all four raw bursts report transport INVALID and lack water_coverage
in their manifests (16 rather than 24 samples). The producer also logs coverage
REFUSED (requires 32 B records covering the target). This is a capture defect;
transport/coverage gates stay intact and reflection residuals are not usable.
