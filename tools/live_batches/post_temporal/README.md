# Temporal post candidate

Prepared only. No staging or launch script. Never place these jobs in a watched
inbox without new explicit user steering. No game launch is authorized by this task.

`rr_post_temporal=0/1/0/1` runs once per save on Ultra: Test Dialogue3 (ef1)
and Water Queens House (static S8). Both ON windows must pass. S8 uses the same
named static save and no camera input as rr_post_decision on current main.
Its close-companion framing remains proposed; verify the shot before accepting evidence.

Each window settles 5 s, captures rendered K8, separately captures presented K8,
records one float energy trace, then measures 10 s of GPU timing. No diagnostics
or readbacks in timing windows. Restore disables the candidate and captures.
The 165 s plan plus 120 s load allowance is 4.75 minutes per scene.

K8 contains depth/final/rr_post_ac/rr_post_eb on all eight frames and
material/albedo at both ends (36 samples). AC is C.rgb/A.a; EB is bloom.rgb and
fresh exposure.a. OFF uses RGBA16F (97), ON RGBA32F (109). The shared decoder
supports both. This extends the existing edge transport; a missing component
is INVALID. The history remains device-local fp32 to avoid fp16 EMA stagnation.
At 2560 x 1067, six 16-byte ping-pong images occupy 262,225,920 bytes (~250 MiB).

Reuse the existing edge_flicker registration and residual functions; mask and
energy rules and the exact predicted observables are in predicted.json. Historical
rr_post=0 numbers are targets only. No pass is claimed from lower energy.
Selective rrpost float traces contain effective AC, Z and EB for linear pre-grade
reconstruction; image slots 1/2/6 are retained in the trace metadata. Float sidecars
use preview-sized nearest texels at width 384; no full raw trace dump is enabled.
Use both OFF repeats to bound sampling/animation noise. Preserve frame registration
and exclude cuts. Read the separate `gpu temporal` lines for the new pass cost.

Offline check (fake queue only):

```powershell
python -m tools.live_batches.post_temporal.make_job
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/post_temporal/dryrun.ps1
```

Peak batch disk bound: 2,070,977,920 bytes (<2 GiB), including 70 MB for small
float previews, logs and summaries. Analyze each scene before the next and delete
analysed raw captures, retaining reports. The dryrun enforces the extent limits
used by the runtime edge capture. No test changes saves or game configuration.
