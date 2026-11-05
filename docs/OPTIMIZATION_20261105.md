# Optimization profiling, 2026-11-05

## Outcomes

| Candidate | Verdict | Measured result / reason |
| --- | --- | --- |
| `rt_scene_compact_holds` | Qualified, default 1 | High inn scene build: -0.87 ms GPU; OFF spread 0.075 ms; float energy within OFF spread. |
| `cpu_opt1` | Qualified, default 1 | Ultra inn hook bodies: -0.13 ms/frame CPU; OFF spread 0.016 ms. |
| `cpu_opt2` | Qualified, default 1 | Ultra inn hook bodies: -0.30 ms/frame CPU; OFF spread 0.088 ms. |
| `cpu_opt3` | Qualified, default 1 | Ultra inn capture total: -0.24 ms/frame CPU; OFF spread 0.054 ms. |
| `cpu_opt4` | Rejected, removed | Ultra inn capture hook bodies: +0.47 / +0.54 ms/frame CPU; OFF spread 0.104 ms. |
| `gi_roof_reject` | Rejected, removed | Inn GI total: 21.75 / 21.58 ms OFF vs 21.87 / 21.95 ms ON. |
| `rt_origin_lazy_normal` | Rejected, removed | Origin pass no saving: 1.514 / 1.495 ms OFF vs 1.634 / 1.520 ms ON. |
| GI temporal current-sample shortcut | Dropped offline | No exact proof of stored-output equivalence. |
| Reflection / local-shadow early-outs | Dropped offline | No exact proof / estimated saving <0.05 ms. |
| Ray flags audit | Closed, already implemented | Binary visibility rays already use first-hit termination. |
| GI attribution job | Abandoned | Readiness cannot observe cached pipelines. |

Totals: about -0.67 ms/frame CPU at the inn on Ultra (hook bodies 12.4 ->
about 11.7 ms/frame), summing the separately qualified CPU receipts; -0.87 ms GPU
on High scene build at the inn. Other scenes were not A/B measured per candidate;
these totals are not a multi-scene or combined-preset measurement.

Tooling fixed tonight: quiet-only readiness, layer-only keys excluded from pipeline
waits, shared A/B analysis, and candidate prewarm. Rejected GPU candidate prewarm
registrations were subsequently removed; the historical notes below retain context.

CPU4 evidence: `captures/queue/logs/opt-cpu4-inn.*.log` and its `.cpu.md` report;
the rejection evidence is retained in `TRACK_D_SCENE_BUILD.md`.

Status: final outcomes applied; historical profiling and candidate notes follow.

Batch: `tools/live_batches/opt_profile/jobs/opt-profile-*.json` (four launches).
Expected total 17.1 minutes including 120 s load per scene; 20 minutes total hard caps.

Each log's post-analysis writes `<log>.profile.md` with rows for this table. Paste only
reviewed rows after the authorized batch, with the log and runner paths alongside them.
Keep Ultra/High and static/pan separate. Those views are not matched-camera A/B evidence.
Report median, min/max and number of complete period summaries for every pass.
Do not add GPU medians or subtract GI trace from GI total to invent a denoiser timer.
Release GI denoising belongs to shared RR; CPU submit/driver are watch interval totals.
FG copy's first straddling line is discarded. Missing or disabled work is never zero.

| Scene | Preset | Window | Pass | Median | Range | Periods | Unit | Status |
| --- | --- | --- | --- | ---: | --- | ---: | --- | --- |

Pending measurements: scene build; GI total/trace/shared RR denoise; local shadows;
reflections; water build/reflect/compose; RR; post chain/grade/temporal; FG copy;
emissive build; CPU submit; watch driver time. See the batch README for units and gates.

## Round 1 outcomes

| Candidate | Decision | Same-launch inn evidence | Applied outcome |
| --- | --- | --- | --- |
| rt_scene_compact_holds | Qualified | High: saving 0.87 ms vs OFF spread 0.075 ms; float energy within OFF spread; identical BVH geometry fixtures | Layer-only, default and every preset 1; release policy forces 1; developer A/B key only |
| gi_roof_reject | Rejected | GI total OFF 21.75 / 21.58 ms vs ON 21.87 / 21.95 ms; no saving | Code, key, prewarm, candidate tests/jobs removed; ID 1028 reserved |
| rt_origin_lazy_normal | Rejected | Origin OFF 1.514 / 1.495 ms vs ON 1.634 / 1.520 ms; no saving | Code, key, prewarm, candidate tests/jobs removed; ID 1027 reserved |
| GI attribution (opt_r2/gi_attr) | Abandoned | Readiness cannot see cached pipelines | Job removed; no attribution claim |

Logs: `opt-scene-compact-holds-inn.1.014837.log`, `opt-gi-roof-inn.1.021432.log`,
`opt-origin-inn.1.012845.log` under `captures/queue/logs`. Full historical design
and evidence: TRACK_D_SCENE_BUILD, TRACK_C_GI and TRACK_D_ALPHA_GI respectively.
The retained opt-tools2 scene report marks rendered flicker comparisons FAIL;
qualification follows the coordinator's decision and geometry/energy evidence,
not a claimed temporal-detector pass. Shader request delta 0, ABI unchanged.
Validation: `scripts/build.ps1 -Test` passes 176/176 (150.24 s). Release equivalence
against main f3d3c41a: 203 identical, 11 equivalent, 0 different; 1,041 assignments,
11 universal certificates and negative control PASS (278.4 s). Receipt:
`.scratch/release-equivalence.json`. No commit, game launch or queue staging.


# Offline cost map and candidates


## Cost map

Scope: offline inspection only, worktree HEAD `09d82f7d`. Main was read only. No game, GPU replay, build, compiler, queue submission or commit was run. Only this document was written. The prohibited installed `bg3rtx.prev.log` was not read. Shader request delta: 0.

Sources below are relative to `C:/Users/quali/Documents/Code Projects/BG3RTX/`. Code references are relative to this worktree. GPU: RTX 5090; RR render 2560 x 1067, output 3840 x 1600. Dialogue1 uses Ultra with stock visible hair.

| ID | Log in captures/queue/logs (runner has same stem + .runner.txt) | Interpretation |
|---|---|---|
| F | final-s8-ultra-high.1.233544.log | Final Ultra S8 measured section 23:37:17.754-23:37:35.783; whole job INVALID because subsequent High readiness failed. Keep Ultra numbers, do not call the entire job qualified. |
| D | final-stock-hair-dialogue1.1.232756.log | VALID; measured static/cost windows at 23:29, stock hair, Ultra. |
| U | rel1-smoke-ultra-s8.1.205853.log | VALID Ultra S8, measured windows at 21:01. Different run/build: context, never an A/B regression claim. |
| H | rel2-smoke-high-s8.1.121305.log | VALID older High S8 found in results.jsonl. Most phases lack motion-ready markers; pan-plan boundaries below are explicitly proxies. Diagnostic phases excluded from ranking. |
| HX | rel2-smoke-high-s8.1.114141.log | INVALID, 32 s; failed before the plan. No measured window to cost. |

Method: assign each timestamped cost receipt to the half-open interval between runner `[motion-ready]` markers. Exclude warmup/producer phases. The final F ab-on interval ends at the High settings request at 23:37:40.260, not the end of the log: otherwise High's 0.342 ms trace would be mislabeled Ultra. Last windows end at the runner completion/exit. H has no readiness marker for static/pan/rotation; those rows use `[pan-plan]` start/end and cannot establish readiness-qualified per-window medians. Its diagnostic settings are flagged separately.

The tables report the median of the *logged summaries* in each interval, not a reconstructed median of individual frames. `n` counts receipts. Most short windows contain one receipt. GI total, RT region, RR and post logs report means; their cells are medians of those means. Scene, trace, water and FG logs already report medians: their cells are medians of medians, unweighted by sample count. No raw frame timings are available. A report can lag its samples; adjacent intervals and settings transitions can contaminate its rolling history. Frame-period statistics sometimes cover 60-600 frames, so share is approximate attribution, not critical-path GPU utilization.

Share = 100 x reported pass ms / median logged `[pace]` rendered-frame median in that interval. This is rendered cadence (usually capped near 33.3 ms), not FG displayed-frame time or an uncapped GPU-frame measurement. NR means unavailable, not 0 ms. CPU submit/tracker figures are accumulated per watchdog reporting interval, not per frame, and are disabled (`cpu_timing=0`, plus the release diagnostics build gate): logged 0.000 ms does not mean free CPU work.

Do not add overlapping rows: enclosing GI total includes injected work; RT region ends after GI trace/temporal/AO/sun and contains GI trace. LS includes its join. Water compose includes its lighting copy; its sun/local/refraction labels repeat that same timer. Scene total is measured independently: component medians do not sum to the total median. Cascade capture contains raster draws + XFB + conversion, not only our incremental overhead. Emissive build and capture are separate receipts. No isolated sun/origin/AO/GI material/AO-at-hit timing exists; do not derive it by subtracting unlike statistics.

### F: static-a, 23:37:17.754-23:37:21.037

Rendered-frame median: 33.250 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 11.435 ms | 34.39% | 1; 5815 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.329 ms | 28.06% | 1; 5816 |
| GI trace | S8 Queens House | Ultra | 7.461 ms | 22.44% | 1; 5817 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.648 ms | 1.95% | 1; 5818 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 5818 |
| Reflections | S8 Queens House | Ultra | 1.137 ms | 3.42% | 1; 5819 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.426 ms | 7.30% | 1; 5793 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 5793 |
| RR combined [mean] | S8 Queens House | Ultra | 2.427 ms | 7.30% | 1; 5793 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.106 ms | 0.32% | 1; 5791 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.057 ms | 0.17% | 1; 5791 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.085 ms | 0.26% | 1; 5792 |
| FG input copy | S8 Queens House | Ultra | 0.026 ms | 0.08% | 1; 5807 |
| Water AS build | S8 Queens House | Ultra | 0.277 ms | 0.83% | 1; 5765 |
| Water reflections + primary | S8 Queens House | Ultra | 0.135 ms | 0.41% | 1; 5787 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.432 ms | 1.30% | 1; 5781 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 5781 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.409 ms | 1.23% | 1; 5781 |
| Cascade captured pass | S8 Queens House | Ultra | 0.653 ms | 1.96% | 1; 5795 |
| Emissive AS build | S8 Queens House | Ultra | 0.109 ms | 0.33% | 1; 5755 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 1; 5755 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 5755 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 5808 |
| Scene total static | S8 Queens House | Ultra | 1.183 ms | 3.56% | 1; 5802 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.601 ms | 1.81% | 1; 5802 |
| Scene static BLAS | S8 Queens House | Ultra | 0.379 ms | 1.14% | 1; 5802 |
| Scene static TLAS | S8 Queens House | Ultra | 0.201 ms | 0.60% | 1; 5802 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 5804 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 5804 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 5804 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: pan-q, 23:37:21.037-23:37:24.492

Rendered-frame median: 33.390 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 11.817 ms | 35.39% | 1; 6573 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.686 ms | 29.01% | 1; 6579 |
| GI trace | S8 Queens House | Ultra | 7.726 ms | 23.14% | 1; 6580 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.685 ms | 2.05% | 1; 6581 |
| LS join [nested] | S8 Queens House | Ultra | 0.012 ms | 0.04% | 1; 6581 |
| Reflections | S8 Queens House | Ultra | 1.123 ms | 3.36% | 1; 6582 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.425 ms | 7.26% | 1; 6409 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 6409 |
| RR combined [mean] | S8 Queens House | Ultra | 2.426 ms | 7.27% | 1; 6409 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.101 ms | 0.30% | 1; 6407 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.054 ms | 0.16% | 1; 6407 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.085 ms | 0.25% | 1; 6408 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 6635 |
| Water AS build | S8 Queens House | Ultra | 0.277 ms | 0.83% | 1; 6360 |
| Water reflections + primary | S8 Queens House | Ultra | 0.135 ms | 0.40% | 1; 6403 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.433 ms | 1.30% | 1; 6397 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 6397 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.410 ms | 1.23% | 1; 6397 |
| Cascade captured pass | S8 Queens House | Ultra | 0.653 ms | 1.96% | 1; 6411 |
| Emissive AS build | S8 Queens House | Ultra | 0.114 ms | 0.34% | 1; 6350 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 1; 6350 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 6350 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.012 ms | 0.04% | 1; 6446 |
| Scene total static | S8 Queens House | Ultra | 1.172 ms | 3.51% | 1; 6419 |
| Scene total moving | S8 Queens House | Ultra | 3.079 ms | 9.22% | 1; 6420 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.598 ms | 1.79% | 1; 6419 |
| Scene static BLAS | S8 Queens House | Ultra | 0.372 ms | 1.11% | 1; 6419 |
| Scene static TLAS | S8 Queens House | Ultra | 0.200 ms | 0.60% | 1; 6419 |
| Scene camera moving convert/extract | S8 Queens House | Ultra | 0.764 ms | 2.29% | 1; 6420 |
| Scene camera moving BLAS | S8 Queens House | Ultra | 1.926 ms | 5.77% | 1; 6420 |
| Scene camera moving TLAS | S8 Queens House | Ultra | 0.324 ms | 0.97% | 1; 6420 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6422 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6422 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6422 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: pan-e, 23:37:24.492-23:37:28.587

Rendered-frame median: 33.420 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 11.403 ms | 34.12% | 1; 6834 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.117 ms | 27.28% | 1; 6836 |
| GI trace | S8 Queens House | Ultra | 7.356 ms | 22.01% | 1; 6837 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.679 ms | 2.03% | 1; 6838 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 6838 |
| Reflections | S8 Queens House | Ultra | 1.181 ms | 3.53% | 1; 6839 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.421 ms | 7.24% | 1; 6806 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 6806 |
| RR combined [mean] | S8 Queens House | Ultra | 2.422 ms | 7.25% | 1; 6806 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.101 ms | 0.30% | 1; 6804 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.054 ms | 0.16% | 1; 6804 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.082 ms | 0.25% | 1; 6805 |
| FG input copy | S8 Queens House | Ultra | 0.028 ms | 0.08% | 1; 6885 |
| Water AS build | S8 Queens House | Ultra | 0.277 ms | 0.83% | 1; 6777 |
| Water reflections + primary | S8 Queens House | Ultra | 0.155 ms | 0.46% | 1; 6800 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.536 ms | 1.60% | 1; 6794 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 6794 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.514 ms | 1.54% | 1; 6794 |
| Cascade captured pass | S8 Queens House | Ultra | 0.627 ms | 1.88% | 1; 6808 |
| Emissive AS build | S8 Queens House | Ultra | 0.120 ms | 0.36% | 1; 6768 |
| Emissive capture | S8 Queens House | Ultra | 0.080 ms | 0.24% | 1; 6768 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 6768 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 6822 |
| Scene total static | S8 Queens House | Ultra | 1.502 ms | 4.49% | 1; 6815 |
| Scene total moving | S8 Queens House | Ultra | 2.472 ms | 7.40% | 1; 6816 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.663 ms | 1.98% | 1; 6815 |
| Scene static BLAS | S8 Queens House | Ultra | 0.530 ms | 1.59% | 1; 6815 |
| Scene static TLAS | S8 Queens House | Ultra | 0.196 ms | 0.59% | 1; 6815 |
| Scene camera moving convert/extract | S8 Queens House | Ultra | 0.454 ms | 1.36% | 1; 6816 |
| Scene camera moving BLAS | S8 Queens House | Ultra | 1.667 ms | 4.99% | 1; 6816 |
| Scene camera moving TLAS | S8 Queens House | Ultra | 0.327 ms | 0.98% | 1; 6816 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6818 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6818 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 6818 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: rotate-r, 23:37:28.587-23:37:32.990

Rendered-frame median: 33.325 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 12.178 ms | 36.54% | 1; 7020 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.932 ms | 29.80% | 1; 7021 |
| GI trace | S8 Queens House | Ultra | 8.076 ms | 24.23% | 1; 7022 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.696 ms | 2.09% | 1; 7023 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 7023 |
| Reflections | S8 Queens House | Ultra | 1.142 ms | 3.43% | 1; 7024 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.422 ms | 7.27% | 2; 6999,7129 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 2; 6999,7129 |
| RR combined [mean] | S8 Queens House | Ultra | 2.423 ms | 7.27% | 2; 6999,7129 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.122 ms | 0.37% | 2; 6997,7127 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.065 ms | 0.20% | 2; 6997,7127 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.098 ms | 0.29% | 2; 6998,7128 |
| FG input copy | S8 Queens House | Ultra | 0.026 ms | 0.08% | 1; 7086 |
| Water AS build | S8 Queens House | Ultra | 0.278 ms | 0.83% | 2; 6971,7101 |
| Water reflections + primary | S8 Queens House | Ultra | 0.144 ms | 0.43% | 2; 6993,7123 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.443 ms | 1.33% | 2; 6987,7117 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 2; 6987,7117 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.419 ms | 1.26% | 2; 6987,7117 |
| Cascade captured pass | S8 Queens House | Ultra | 0.655 ms | 1.97% | 2; 7001,7131 |
| Emissive AS build | S8 Queens House | Ultra | 0.110 ms | 0.33% | 2; 6961,7091 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 2; 6961,7091 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 2; 6961,7091 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 2; 7013,7143 |
| Scene total static | S8 Queens House | Ultra | 1.157 ms | 3.47% | 2; 7008,7138 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.592 ms | 1.78% | 2; 7008,7138 |
| Scene static BLAS | S8 Queens House | Ultra | 0.368 ms | 1.10% | 2; 7008,7138 |
| Scene static TLAS | S8 Queens House | Ultra | 0.193 ms | 0.58% | 2; 7008,7138 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7010,7140 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7010,7140 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7010,7140 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: rotate-l, 23:37:32.990-23:37:35.783

Rendered-frame median: 33.380 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 12.050 ms | 36.10% | 1; 7280 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.845 ms | 29.49% | 1; 7281 |
| GI trace | S8 Queens House | Ultra | 8.048 ms | 24.11% | 1; 7282 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.636 ms | 1.91% | 1; 7283 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 7283 |
| Reflections | S8 Queens House | Ultra | 1.112 ms | 3.33% | 1; 7284 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.460 ms | 7.37% | 1; 7258 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 7258 |
| RR combined [mean] | S8 Queens House | Ultra | 2.461 ms | 7.37% | 1; 7258 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.113 ms | 0.34% | 1; 7256 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.055 ms | 0.16% | 1; 7256 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.082 ms | 0.25% | 1; 7257 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 7366 |
| Water AS build | S8 Queens House | Ultra | 0.277 ms | 0.83% | 1; 7230 |
| Water reflections + primary | S8 Queens House | Ultra | 0.144 ms | 0.43% | 1; 7252 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.442 ms | 1.32% | 1; 7246 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 7246 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.420 ms | 1.26% | 1; 7246 |
| Cascade captured pass | S8 Queens House | Ultra | 0.654 ms | 1.96% | 1; 7260 |
| Emissive AS build | S8 Queens House | Ultra | 0.110 ms | 0.33% | 1; 7220 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 1; 7220 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 7220 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 7272 |
| Scene total static | S8 Queens House | Ultra | 1.156 ms | 3.46% | 1; 7267 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.590 ms | 1.77% | 1; 7267 |
| Scene static BLAS | S8 Queens House | Ultra | 0.369 ms | 1.11% | 1; 7267 |
| Scene static TLAS | S8 Queens House | Ultra | 0.192 ms | 0.58% | 1; 7267 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7269 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7269 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7269 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: ab-off, 23:37:35.783-23:37:38.697

Rendered-frame median: 33.350 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| RR evaluate [mean] | S8 Queens House | Ultra | 2.421 ms | 7.26% | 2; 7407,7546 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 2; 7407,7546 |
| RR combined [mean] | S8 Queens House | Ultra | 2.421 ms | 7.26% | 2; 7407,7546 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.139 ms | 0.42% | 2; 7405,7544 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.076 ms | 0.23% | 2; 7405,7544 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.118 ms | 0.35% | 2; 7406,7545 |
| Water AS build | S8 Queens House | Ultra | 0.278 ms | 0.83% | 2; 7391,7531 |
| Cascade captured pass | S8 Queens House | Ultra | 0.655 ms | 1.96% | 2; 7409,7548 |
| Emissive AS build | S8 Queens House | Ultra | 0.112 ms | 0.33% | 2; 7381,7521 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 2; 7381,7521 |
| Emissive no capture | S8 Queens House | Ultra | 0.019 ms | 0.06% | 2; 7381,7521 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 7493 |
| Scene total static | S8 Queens House | Ultra | 1.468 ms | 4.40% | 2; 7416,7555 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.589 ms | 1.77% | 2; 7416,7555 |
| Scene static BLAS | S8 Queens House | Ultra | 0.504 ms | 1.51% | 2; 7416,7555 |
| Scene static TLAS | S8 Queens House | Ultra | 0.312 ms | 0.94% | 2; 7416,7555 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7418,7557 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7418,7557 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 7418,7557 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### F: ab-on, 23:37:38.697-23:37:40.260

Rendered-frame median: 33.350 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 12.064 ms | 36.17% | 1; 7616 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.825 ms | 29.46% | 1; 7617 |
| GI trace | S8 Queens House | Ultra | 8.061 ms | 24.17% | 1; 7618 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.684 ms | 2.05% | 1; 7619 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 7619 |
| Reflections | S8 Queens House | Ultra | 1.127 ms | 3.38% | 1; 7620 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.439 ms | 7.31% | 1; 7678 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 7678 |
| RR combined [mean] | S8 Queens House | Ultra | 2.440 ms | 7.32% | 1; 7678 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.147 ms | 0.44% | 1; 7676 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.075 ms | 0.22% | 1; 7676 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.114 ms | 0.34% | 1; 7677 |
| FG input copy | S8 Queens House | Ultra | 0.026 ms | 0.08% | 1; 7642 |
| Water AS build | S8 Queens House | Ultra | 0.276 ms | 0.83% | 1; 7663 |
| Water reflections + primary | S8 Queens House | Ultra | 0.144 ms | 0.43% | 1; 7595 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.442 ms | 1.33% | 1; 7602 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 7602 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.420 ms | 1.26% | 1; 7602 |
| Cascade captured pass | S8 Queens House | Ultra | 0.656 ms | 1.97% | 1; 7680 |
| Emissive AS build | S8 Queens House | Ultra | 0.109 ms | 0.33% | 1; 7653 |
| Emissive capture | S8 Queens House | Ultra | 0.099 ms | 0.30% | 1; 7653 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 7653 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 7626 |
| Scene total static | S8 Queens House | Ultra | 1.156 ms | 3.47% | 1; 7687 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.592 ms | 1.78% | 1; 7687 |
| Scene static BLAS | S8 Queens House | Ultra | 0.366 ms | 1.10% | 1; 7687 |
| Scene static TLAS | S8 Queens House | Ultra | 0.191 ms | 0.57% | 1; 7687 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7689 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7689 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 7689 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### D: static-a, 23:29:44.722-23:29:48.099

Rendered-frame median: 33.330 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | Dialogue1 stock hair | Ultra | 14.396 ms | 43.19% | 1; 7732 |
| RT region [mean, nested] | Dialogue1 stock hair | Ultra | 11.659 ms | 34.98% | 1; 7733 |
| GI trace | Dialogue1 stock hair | Ultra | 9.837 ms | 29.51% | 1; 7734 |
| Local shadows incl. join | Dialogue1 stock hair | Ultra | 1.174 ms | 3.52% | 1; 7735 |
| LS join [nested] | Dialogue1 stock hair | Ultra | 0.009 ms | 0.03% | 1; 7735 |
| Reflections | Dialogue1 stock hair | Ultra | 1.176 ms | 3.53% | 1; 7736 |
| RR evaluate [mean] | Dialogue1 stock hair | Ultra | 2.513 ms | 7.54% | 1; 7710 |
| RR guides [mean] | Dialogue1 stock hair | Ultra | 0.001 ms | 0.00% | 1; 7710 |
| RR combined [mean] | Dialogue1 stock hair | Ultra | 2.514 ms | 7.54% | 1; 7710 |
| Post render chain [mean] | Dialogue1 stock hair | Ultra | 0.135 ms | 0.41% | 1; 7708 |
| Post output grade [mean] | Dialogue1 stock hair | Ultra | 0.080 ms | 0.24% | 1; 7708 |
| Post temporal [mean] | Dialogue1 stock hair | Ultra | 0.152 ms | 0.46% | 1; 7709 |
| FG input copy | Dialogue1 stock hair | Ultra | 0.026 ms | 0.08% | 1; 7791 |
| Water AS build | Dialogue1 stock hair | Ultra | 0.136 ms | 0.41% | 1; 7682 |
| Water reflections + primary | Dialogue1 stock hair | Ultra | 0.077 ms | 0.23% | 1; 7704 |
| Water compose incl. copy | Dialogue1 stock hair | Ultra | 0.322 ms | 0.97% | 1; 7698 |
| Water lighting copy [nested] | Dialogue1 stock hair | Ultra | 0.022 ms | 0.07% | 1; 7698 |
| Water compose dispatch [nested] | Dialogue1 stock hair | Ultra | 0.300 ms | 0.90% | 1; 7698 |
| Cascade captured pass | Dialogue1 stock hair | Ultra | 0.209 ms | 0.63% | 1; 7712 |
| Emissive AS build | Dialogue1 stock hair | Ultra | 0.067 ms | 0.20% | 1; 7674 |
| Emissive capture | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 1; 7674 |
| Emissive no capture | Dialogue1 stock hair | Ultra | 0.000 ms | 0.00% | 1; 7674 |
| Emissive calibration [mean] | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 1; 7725 |
| Scene total static | Dialogue1 stock hair | Ultra | 1.780 ms | 5.34% | 1; 7719 |
| Scene static convert/extract | Dialogue1 stock hair | Ultra | 1.030 ms | 3.09% | 1; 7719 |
| Scene static BLAS | Dialogue1 stock hair | Ultra | 0.585 ms | 1.76% | 1; 7719 |
| Scene static TLAS | Dialogue1 stock hair | Ultra | 0.163 ms | 0.49% | 1; 7719 |
| CPU submit [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7721 |
| CPU descriptor tracker [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7721 |
| CPU alpha descriptor [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7721 |
| CPU life bookkeeping [per frame; 60 s] | Dialogue1 stock hair | Ultra | NR | NR | 0 |

### D: static-b, 23:29:48.099-23:29:50.263

Rendered-frame median: 33.280 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| RR evaluate [mean] | Dialogue1 stock hair | Ultra | 2.592 ms | 7.79% | 1; 7888 |
| RR guides [mean] | Dialogue1 stock hair | Ultra | 0.001 ms | 0.00% | 1; 7888 |
| RR combined [mean] | Dialogue1 stock hair | Ultra | 2.593 ms | 7.79% | 1; 7888 |
| Post render chain [mean] | Dialogue1 stock hair | Ultra | 0.128 ms | 0.38% | 1; 7886 |
| Post output grade [mean] | Dialogue1 stock hair | Ultra | 0.081 ms | 0.24% | 1; 7886 |
| Post temporal [mean] | Dialogue1 stock hair | Ultra | 0.127 ms | 0.38% | 1; 7887 |
| Water AS build | Dialogue1 stock hair | Ultra | 0.135 ms | 0.41% | 1; 7860 |
| Water reflections + primary | Dialogue1 stock hair | Ultra | 0.077 ms | 0.23% | 1; 7882 |
| Water compose incl. copy | Dialogue1 stock hair | Ultra | 0.322 ms | 0.97% | 1; 7876 |
| Water lighting copy [nested] | Dialogue1 stock hair | Ultra | 0.022 ms | 0.07% | 1; 7876 |
| Water compose dispatch [nested] | Dialogue1 stock hair | Ultra | 0.300 ms | 0.90% | 1; 7876 |
| Cascade captured pass | Dialogue1 stock hair | Ultra | 0.209 ms | 0.63% | 1; 7890 |
| Emissive AS build | Dialogue1 stock hair | Ultra | 0.068 ms | 0.20% | 1; 7852 |
| Emissive capture | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 1; 7852 |
| Emissive no capture | Dialogue1 stock hair | Ultra | 0.000 ms | 0.00% | 1; 7852 |
| Emissive calibration [mean] | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 1; 7902 |
| Scene total static | Dialogue1 stock hair | Ultra | 1.808 ms | 5.43% | 1; 7897 |
| Scene static convert/extract | Dialogue1 stock hair | Ultra | 1.027 ms | 3.09% | 1; 7897 |
| Scene static BLAS | Dialogue1 stock hair | Ultra | 0.607 ms | 1.82% | 1; 7897 |
| Scene static TLAS | Dialogue1 stock hair | Ultra | 0.164 ms | 0.49% | 1; 7897 |
| CPU submit [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7899 |
| CPU descriptor tracker [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7899 |
| CPU alpha descriptor [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 1; 7899 |
| CPU life bookkeeping [per frame; 60 s] | Dialogue1 stock hair | Ultra | NR | NR | 0 |

### D: settle, 23:29:50.263-23:29:56.020

Rendered-frame median: 33.335 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | Dialogue1 stock hair | Ultra | 14.558 ms | 43.67% | 2; 8037,8163 |
| RT region [mean, nested] | Dialogue1 stock hair | Ultra | 11.890 ms | 35.67% | 2; 8038,8164 |
| GI trace | Dialogue1 stock hair | Ultra | 9.851 ms | 29.55% | 2; 8039,8165 |
| Local shadows incl. join | Dialogue1 stock hair | Ultra | 1.184 ms | 3.55% | 2; 8040,8166 |
| LS join [nested] | Dialogue1 stock hair | Ultra | 0.009 ms | 0.03% | 2; 8040,8166 |
| Reflections | Dialogue1 stock hair | Ultra | 1.134 ms | 3.40% | 2; 8041,8167 |
| RR evaluate [mean] | Dialogue1 stock hair | Ultra | 2.587 ms | 7.76% | 2; 8015,8142 |
| RR guides [mean] | Dialogue1 stock hair | Ultra | 0.001 ms | 0.00% | 2; 8015,8142 |
| RR combined [mean] | Dialogue1 stock hair | Ultra | 2.588 ms | 7.76% | 2; 8015,8142 |
| Post render chain [mean] | Dialogue1 stock hair | Ultra | 0.151 ms | 0.45% | 2; 8013,8140 |
| Post output grade [mean] | Dialogue1 stock hair | Ultra | 0.092 ms | 0.28% | 2; 8013,8140 |
| Post temporal [mean] | Dialogue1 stock hair | Ultra | 0.156 ms | 0.47% | 2; 8014,8141 |
| FG input copy | Dialogue1 stock hair | Ultra | 0.026 ms | 0.08% | 1; 8029 |
| Water AS build | Dialogue1 stock hair | Ultra | 0.136 ms | 0.41% | 2; 7987,8114 |
| Water reflections + primary | Dialogue1 stock hair | Ultra | 0.077 ms | 0.23% | 2; 8009,8136 |
| Water compose incl. copy | Dialogue1 stock hair | Ultra | 0.322 ms | 0.97% | 2; 8003,8130 |
| Water lighting copy [nested] | Dialogue1 stock hair | Ultra | 0.022 ms | 0.07% | 2; 8003,8130 |
| Water compose dispatch [nested] | Dialogue1 stock hair | Ultra | 0.300 ms | 0.90% | 2; 8003,8130 |
| Cascade captured pass | Dialogue1 stock hair | Ultra | 0.209 ms | 0.63% | 2; 8017,8144 |
| Emissive AS build | Dialogue1 stock hair | Ultra | 0.069 ms | 0.21% | 2; 7979,8106 |
| Emissive capture | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 2; 7979,8106 |
| Emissive no capture | Dialogue1 stock hair | Ultra | 0.000 ms | 0.00% | 2; 7979,8106 |
| Emissive calibration [mean] | Dialogue1 stock hair | Ultra | 0.010 ms | 0.03% | 2; 8030,8156 |
| Scene total static | Dialogue1 stock hair | Ultra | 1.778 ms | 5.34% | 2; 8024,8151 |
| Scene static convert/extract | Dialogue1 stock hair | Ultra | 1.026 ms | 3.08% | 2; 8024,8151 |
| Scene static BLAS | Dialogue1 stock hair | Ultra | 0.584 ms | 1.75% | 2; 8024,8151 |
| Scene static TLAS | Dialogue1 stock hair | Ultra | 0.164 ms | 0.49% | 2; 8024,8151 |
| CPU submit [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 2; 8026,8153 |
| CPU descriptor tracker [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 2; 8026,8153 |
| CPU alpha descriptor [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 2; 8026,8153 |
| CPU life bookkeeping [per frame; 60 s] | Dialogue1 stock hair | Ultra | NR | NR | 0 |

### D: cost-hair, 23:29:56.020-23:30:02.615

Rendered-frame median: 33.360 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | Dialogue1 stock hair | Ultra | 14.785 ms | 44.32% | 3; 8316,8444,8576 |
| RT region [mean, nested] | Dialogue1 stock hair | Ultra | 12.054 ms | 36.13% | 3; 8317,8445,8577 |
| GI trace | Dialogue1 stock hair | Ultra | 9.843 ms | 29.51% | 3; 8318,8446,8578 |
| Local shadows incl. join | Dialogue1 stock hair | Ultra | 1.212 ms | 3.63% | 3; 8319,8447,8579 |
| LS join [nested] | Dialogue1 stock hair | Ultra | 0.009 ms | 0.03% | 3; 8319,8447,8579 |
| Reflections | Dialogue1 stock hair | Ultra | 1.218 ms | 3.65% | 3; 8320,8448,8580 |
| RR evaluate [mean] | Dialogue1 stock hair | Ultra | 2.580 ms | 7.73% | 3; 8295,8423,8554 |
| RR guides [mean] | Dialogue1 stock hair | Ultra | 0.001 ms | 0.00% | 3; 8295,8423,8554 |
| RR combined [mean] | Dialogue1 stock hair | Ultra | 2.581 ms | 7.74% | 3; 8295,8423,8554 |
| Post render chain [mean] | Dialogue1 stock hair | Ultra | 0.171 ms | 0.51% | 3; 8293,8421,8552 |
| Post output grade [mean] | Dialogue1 stock hair | Ultra | 0.100 ms | 0.30% | 3; 8293,8421,8552 |
| Post temporal [mean] | Dialogue1 stock hair | Ultra | 0.175 ms | 0.52% | 3; 8294,8422,8553 |
| FG input copy | Dialogue1 stock hair | Ultra | 0.026 ms | 0.08% | 2; 8231,8491 |
| Water AS build | Dialogue1 stock hair | Ultra | 0.136 ms | 0.41% | 3; 8267,8395,8526 |
| Water reflections + primary | Dialogue1 stock hair | Ultra | 0.077 ms | 0.23% | 3; 8289,8417,8548 |
| Water compose incl. copy | Dialogue1 stock hair | Ultra | 0.322 ms | 0.97% | 3; 8283,8411,8542 |
| Water lighting copy [nested] | Dialogue1 stock hair | Ultra | 0.022 ms | 0.07% | 3; 8283,8411,8542 |
| Water compose dispatch [nested] | Dialogue1 stock hair | Ultra | 0.300 ms | 0.90% | 3; 8283,8411,8542 |
| Cascade captured pass | Dialogue1 stock hair | Ultra | 0.210 ms | 0.63% | 3; 8297,8425,8556 |
| Emissive AS build | Dialogue1 stock hair | Ultra | 0.068 ms | 0.20% | 3; 8259,8387,8518 |
| Emissive capture | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 3; 8259,8387,8518 |
| Emissive no capture | Dialogue1 stock hair | Ultra | 0.000 ms | 0.00% | 3; 8259,8387,8518 |
| Emissive calibration [mean] | Dialogue1 stock hair | Ultra | 0.011 ms | 0.03% | 3; 8309,8437,8568 |
| Scene total static | Dialogue1 stock hair | Ultra | 1.801 ms | 5.40% | 3; 8304,8432,8563 |
| Scene static convert/extract | Dialogue1 stock hair | Ultra | 1.029 ms | 3.08% | 3; 8304,8432,8563 |
| Scene static BLAS | Dialogue1 stock hair | Ultra | 0.589 ms | 1.77% | 3; 8304,8432,8563 |
| Scene static TLAS | Dialogue1 stock hair | Ultra | 0.163 ms | 0.49% | 3; 8304,8432,8563 |
| CPU submit [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 3; 8306,8434,8565 |
| CPU descriptor tracker [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 3; 8306,8434,8565 |
| CPU alpha descriptor [watch sum; disabled] | Dialogue1 stock hair | Ultra | 0.000 ms | NR | 3; 8306,8434,8565 |
| CPU life bookkeeping [per frame; 60 s] | Dialogue1 stock hair | Ultra | NR | NR | 0 |

### U: static-a, 21:01:17.726-21:01:21.109

Rendered-frame median: 33.560 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 13.341 ms | 39.75% | 1; 8206 |
| RT region [mean, nested] | S8 Queens House | Ultra | 11.041 ms | 32.90% | 1; 8207 |
| GI trace | S8 Queens House | Ultra | 9.044 ms | 26.95% | 1; 8208 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.668 ms | 1.99% | 1; 8209 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 8209 |
| Reflections | S8 Queens House | Ultra | 1.185 ms | 3.53% | 1; 8210 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.676 ms | 7.97% | 1; 8182 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 8182 |
| RR combined [mean] | S8 Queens House | Ultra | 2.677 ms | 7.98% | 1; 8182 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.117 ms | 0.35% | 1; 8180 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.059 ms | 0.18% | 1; 8180 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.091 ms | 0.27% | 1; 8181 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.82% | 1; 8152 |
| Water reflections + primary | S8 Queens House | Ultra | 0.135 ms | 0.40% | 1; 8176 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.434 ms | 1.29% | 1; 8170 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 8170 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.412 ms | 1.23% | 1; 8170 |
| Cascade captured pass | S8 Queens House | Ultra | 0.653 ms | 1.95% | 1; 8184 |
| Emissive AS build | S8 Queens House | Ultra | 0.110 ms | 0.33% | 1; 8143 |
| Emissive capture | S8 Queens House | Ultra | 0.097 ms | 0.29% | 1; 8143 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 8143 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 8196 |
| Scene total static | S8 Queens House | Ultra | 1.186 ms | 3.53% | 1; 8191 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.596 ms | 1.78% | 1; 8191 |
| Scene static BLAS | S8 Queens House | Ultra | 0.377 ms | 1.12% | 1; 8191 |
| Scene static TLAS | S8 Queens House | Ultra | 0.205 ms | 0.61% | 1; 8191 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8193 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8193 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8193 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: pan-q, 21:01:21.109-21:01:24.434

Rendered-frame median: 33.850 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 12.751 ms | 37.67% | 1; 8884 |
| RT region [mean, nested] | S8 Queens House | Ultra | 10.387 ms | 30.69% | 1; 8885 |
| GI trace | S8 Queens House | Ultra | 8.526 ms | 25.19% | 1; 8886 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.651 ms | 1.92% | 1; 8887 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 8887 |
| Reflections | S8 Queens House | Ultra | 1.261 ms | 3.73% | 1; 8888 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.543 ms | 7.51% | 1; 8793 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 8793 |
| RR combined [mean] | S8 Queens House | Ultra | 2.545 ms | 7.52% | 1; 8793 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.110 ms | 0.32% | 1; 8791 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.060 ms | 0.18% | 1; 8791 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.095 ms | 0.28% | 1; 8792 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 8267 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.81% | 1; 8756 |
| Water reflections + primary | S8 Queens House | Ultra | 0.136 ms | 0.40% | 1; 8787 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.435 ms | 1.29% | 1; 8781 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 8781 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.412 ms | 1.22% | 1; 8781 |
| Cascade captured pass | S8 Queens House | Ultra | 0.649 ms | 1.92% | 1; 8795 |
| Emissive AS build | S8 Queens House | Ultra | 0.110 ms | 0.32% | 1; 8746 |
| Emissive capture | S8 Queens House | Ultra | 0.098 ms | 0.29% | 1; 8746 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 8746 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.012 ms | 0.04% | 1; 8830 |
| Scene total static | S8 Queens House | Ultra | 1.339 ms | 3.96% | 1; 8803 |
| Scene total moving | S8 Queens House | Ultra | 3.329 ms | 9.83% | 1; 8804 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.594 ms | 1.75% | 1; 8803 |
| Scene static BLAS | S8 Queens House | Ultra | 0.374 ms | 1.10% | 1; 8803 |
| Scene static TLAS | S8 Queens House | Ultra | 0.209 ms | 0.62% | 1; 8803 |
| Scene camera moving convert/extract | S8 Queens House | Ultra | 0.915 ms | 2.70% | 1; 8804 |
| Scene camera moving BLAS | S8 Queens House | Ultra | 1.919 ms | 5.67% | 1; 8804 |
| Scene camera moving TLAS | S8 Queens House | Ultra | 0.323 ms | 0.95% | 1; 8804 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8806 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8806 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 8806 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: pan-e, 21:01:24.434-21:01:27.636

Rendered-frame median: 33.330 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 12.399 ms | 37.20% | 1; 9140 |
| RT region [mean, nested] | S8 Queens House | Ultra | 9.964 ms | 29.89% | 1; 9146 |
| GI trace | S8 Queens House | Ultra | 7.914 ms | 23.74% | 1; 9147 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.668 ms | 2.00% | 1; 9148 |
| LS join [nested] | S8 Queens House | Ultra | 0.013 ms | 0.04% | 1; 9148 |
| Reflections | S8 Queens House | Ultra | 1.268 ms | 3.80% | 1; 9149 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.546 ms | 7.64% | 1; 9100 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 9100 |
| RR combined [mean] | S8 Queens House | Ultra | 2.548 ms | 7.64% | 1; 9100 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.106 ms | 0.32% | 1; 9098 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.058 ms | 0.17% | 1; 9098 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.090 ms | 0.27% | 1; 9099 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 8964 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.83% | 1; 9071 |
| Water reflections + primary | S8 Queens House | Ultra | 0.145 ms | 0.44% | 1; 9094 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.498 ms | 1.49% | 1; 9088 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 9088 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.475 ms | 1.43% | 1; 9088 |
| Cascade captured pass | S8 Queens House | Ultra | 0.652 ms | 1.96% | 1; 9102 |
| Emissive AS build | S8 Queens House | Ultra | 0.116 ms | 0.35% | 1; 9061 |
| Emissive capture | S8 Queens House | Ultra | 0.110 ms | 0.33% | 1; 9061 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 9061 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 9117 |
| Scene total static | S8 Queens House | Ultra | 1.277 ms | 3.83% | 1; 9110 |
| Scene total moving | S8 Queens House | Ultra | 2.482 ms | 7.45% | 1; 9111 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.651 ms | 1.95% | 1; 9110 |
| Scene static BLAS | S8 Queens House | Ultra | 0.414 ms | 1.24% | 1; 9110 |
| Scene static TLAS | S8 Queens House | Ultra | 0.201 ms | 0.60% | 1; 9110 |
| Scene camera moving convert/extract | S8 Queens House | Ultra | 0.448 ms | 1.34% | 1; 9111 |
| Scene camera moving BLAS | S8 Queens House | Ultra | 1.660 ms | 4.98% | 1; 9111 |
| Scene camera moving TLAS | S8 Queens House | Ultra | 0.333 ms | 1.00% | 1; 9111 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9113 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9113 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9113 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: rotate-r, 21:01:27.636-21:01:32.034

Rendered-frame median: 33.050 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 13.968 ms | 42.26% | 1; 9317 |
| RT region [mean, nested] | S8 Queens House | Ultra | 11.791 ms | 35.68% | 1; 9318 |
| GI trace | S8 Queens House | Ultra | 8.421 ms | 25.48% | 1; 9319 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.600 ms | 1.82% | 1; 9320 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 9320 |
| Reflections | S8 Queens House | Ultra | 1.197 ms | 3.62% | 1; 9321 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.480 ms | 7.50% | 1; 9293 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 9293 |
| RR combined [mean] | S8 Queens House | Ultra | 2.481 ms | 7.51% | 1; 9293 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.105 ms | 0.32% | 1; 9291 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.055 ms | 0.17% | 1; 9291 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.087 ms | 0.26% | 1; 9292 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 9310 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.83% | 1; 9265 |
| Water reflections + primary | S8 Queens House | Ultra | 0.167 ms | 0.51% | 1; 9287 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.515 ms | 1.56% | 1; 9281 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 9281 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.492 ms | 1.49% | 1; 9281 |
| Cascade captured pass | S8 Queens House | Ultra | 0.611 ms | 1.85% | 1; 9295 |
| Emissive AS build | S8 Queens House | Ultra | 0.109 ms | 0.33% | 1; 9256 |
| Emissive capture | S8 Queens House | Ultra | 0.097 ms | 0.29% | 1; 9256 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 1; 9256 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 9307 |
| Scene total static | S8 Queens House | Ultra | 1.207 ms | 3.65% | 1; 9302 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.617 ms | 1.87% | 1; 9302 |
| Scene static BLAS | S8 Queens House | Ultra | 0.374 ms | 1.13% | 1; 9302 |
| Scene static TLAS | S8 Queens House | Ultra | 0.201 ms | 0.61% | 1; 9302 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9304 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9304 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9304 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: rotate-l, 21:01:32.034-21:01:35.049

Rendered-frame median: 33.310 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 13.619 ms | 40.89% | 1; 9499 |
| RT region [mean, nested] | S8 Queens House | Ultra | 11.362 ms | 34.11% | 1; 9500 |
| GI trace | S8 Queens House | Ultra | 8.300 ms | 24.92% | 1; 9501 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.587 ms | 1.76% | 1; 9502 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 1; 9502 |
| Reflections | S8 Queens House | Ultra | 1.230 ms | 3.69% | 1; 9503 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.449 ms | 7.35% | 2; 9478,9624 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 2; 9478,9624 |
| RR combined [mean] | S8 Queens House | Ultra | 2.450 ms | 7.35% | 2; 9478,9624 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.123 ms | 0.37% | 2; 9476,9622 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.068 ms | 0.20% | 2; 9476,9622 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.104 ms | 0.31% | 2; 9477,9623 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 9525 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.82% | 2; 9450,9608 |
| Water reflections + primary | S8 Queens House | Ultra | 0.168 ms | 0.50% | 1; 9472 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.514 ms | 1.54% | 1; 9466 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 9466 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.491 ms | 1.47% | 1; 9466 |
| Cascade captured pass | S8 Queens House | Ultra | 0.616 ms | 1.85% | 2; 9480,9626 |
| Emissive AS build | S8 Queens House | Ultra | 0.110 ms | 0.33% | 2; 9441,9599 |
| Emissive capture | S8 Queens House | Ultra | 0.097 ms | 0.29% | 2; 9441,9599 |
| Emissive no capture | S8 Queens House | Ultra | 0.009 ms | 0.03% | 2; 9441,9599 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.012 ms | 0.04% | 1; 9492 |
| Scene total static | S8 Queens House | Ultra | 1.515 ms | 4.55% | 2; 9487,9633 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.621 ms | 1.86% | 2; 9487,9633 |
| Scene static BLAS | S8 Queens House | Ultra | 0.427 ms | 1.28% | 2; 9487,9633 |
| Scene static TLAS | S8 Queens House | Ultra | 0.357 ms | 1.07% | 2; 9487,9633 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9489,9635 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9489,9635 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9489,9635 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: ab-off, 21:01:35.049-21:01:38.222

Rendered-frame median: 33.290 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| RR evaluate [mean] | S8 Queens House | Ultra | 2.481 ms | 7.45% | 1; 9756 |
| RR guides [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 9756 |
| RR combined [mean] | S8 Queens House | Ultra | 2.482 ms | 7.46% | 1; 9756 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.139 ms | 0.42% | 1; 9754 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.081 ms | 0.24% | 1; 9754 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.128 ms | 0.38% | 1; 9755 |
| FG input copy | S8 Queens House | Ultra | 0.029 ms | 0.09% | 1; 9787 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.83% | 1; 9741 |
| Water reflections + primary | S8 Queens House | Ultra | 0.167 ms | 0.50% | 1; 9810 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.516 ms | 1.55% | 1; 9817 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 1; 9817 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.493 ms | 1.48% | 1; 9817 |
| Cascade captured pass | S8 Queens House | Ultra | 0.619 ms | 1.86% | 1; 9758 |
| Emissive AS build | S8 Queens House | Ultra | 0.112 ms | 0.34% | 1; 9732 |
| Emissive capture | S8 Queens House | Ultra | 0.098 ms | 0.29% | 1; 9732 |
| Emissive no capture | S8 Queens House | Ultra | 0.018 ms | 0.05% | 1; 9732 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.001 ms | 0.00% | 1; 9713 |
| Scene total static | S8 Queens House | Ultra | 1.496 ms | 4.49% | 1; 9765 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.617 ms | 1.85% | 1; 9765 |
| Scene static BLAS | S8 Queens House | Ultra | 0.437 ms | 1.31% | 1; 9765 |
| Scene static TLAS | S8 Queens House | Ultra | 0.326 ms | 0.98% | 1; 9765 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9767 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9767 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 1; 9767 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### U: ab-on, 21:01:38.222-21:01:43.177

Rendered-frame median: 33.800 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | Ultra | 16.564 ms | 49.01% | 3; 9831,9959,10085 |
| RT region [mean, nested] | S8 Queens House | Ultra | 14.059 ms | 41.59% | 3; 9832,9960,10086 |
| GI trace | S8 Queens House | Ultra | 10.354 ms | 30.63% | 3; 9833,9961,10087 |
| Local shadows incl. join | S8 Queens House | Ultra | 0.615 ms | 1.82% | 3; 9834,9962,10088 |
| LS join [nested] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 3; 9834,9962,10088 |
| Reflections | S8 Queens House | Ultra | 1.410 ms | 4.17% | 3; 9835,9963,10089 |
| RR evaluate [mean] | S8 Queens House | Ultra | 2.750 ms | 8.13% | 2; 9885,10011 |
| RR guides [mean] | S8 Queens House | Ultra | 0.002 ms | 0.01% | 2; 9885,10011 |
| RR combined [mean] | S8 Queens House | Ultra | 2.752 ms | 8.14% | 2; 9885,10011 |
| Post render chain [mean] | S8 Queens House | Ultra | 0.155 ms | 0.46% | 2; 9883,10009 |
| Post output grade [mean] | S8 Queens House | Ultra | 0.077 ms | 0.23% | 2; 9883,10009 |
| Post temporal [mean] | S8 Queens House | Ultra | 0.122 ms | 0.36% | 2; 9884,10010 |
| FG input copy | S8 Queens House | Ultra | 0.027 ms | 0.08% | 1; 10043 |
| Water AS build | S8 Queens House | Ultra | 0.275 ms | 0.81% | 2; 9870,9996 |
| Water reflections + primary | S8 Queens House | Ultra | 0.167 ms | 0.49% | 2; 9941,10067 |
| Water compose incl. copy | S8 Queens House | Ultra | 0.516 ms | 1.53% | 2; 9948,10074 |
| Water lighting copy [nested] | S8 Queens House | Ultra | 0.023 ms | 0.07% | 2; 9948,10074 |
| Water compose dispatch [nested] | S8 Queens House | Ultra | 0.492 ms | 1.46% | 2; 9948,10074 |
| Cascade captured pass | S8 Queens House | Ultra | 0.615 ms | 1.82% | 2; 9887,10013 |
| Emissive AS build | S8 Queens House | Ultra | 0.112 ms | 0.33% | 2; 9861,9987 |
| Emissive capture | S8 Queens House | Ultra | 0.098 ms | 0.29% | 2; 9861,9987 |
| Emissive no capture | S8 Queens House | Ultra | 0.000 ms | 0.00% | 2; 9861,9987 |
| Emissive calibration [mean] | S8 Queens House | Ultra | 0.011 ms | 0.03% | 3; 9843,9970,10096 |
| Scene total static | S8 Queens House | Ultra | 1.454 ms | 4.30% | 2; 9894,10020 |
| Scene static convert/extract | S8 Queens House | Ultra | 0.621 ms | 1.84% | 2; 9894,10020 |
| Scene static BLAS | S8 Queens House | Ultra | 0.381 ms | 1.13% | 2; 9894,10020 |
| Scene static TLAS | S8 Queens House | Ultra | 0.382 ms | 1.13% | 2; 9894,10020 |
| CPU submit [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9896,10022 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9896,10022 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | Ultra | 0.000 ms | NR | 2; 9896,10022 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | Ultra | NR | NR | 0 |

### H: static-a [proxy], 12:15:27.002-12:16:03.015

Rendered-frame median: 33.360 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.643 ms | 4.93% | 4; 2694,2799,2874,2958 |
| RT region [mean, nested] | S8 Queens House | High | 1.086 ms | 3.26% | 3; 2723,2823,2899 |
| GI trace | S8 Queens House | High | 0.302 ms | 0.91% | 3; 2724,2824,2900 |
| Local shadows incl. join | S8 Queens House | High | 0.121 ms | 0.36% | 3; 2731,2830,2907 |
| LS join [nested] | S8 Queens House | High | 0.011 ms | 0.03% | 3; 2731,2830,2907 |
| Reflections | S8 Queens House | High | 0.370 ms | 1.11% | 3; 2729,2828,2905 |
| RR evaluate [mean] | S8 Queens House | High | 2.511 ms | 7.53% | 7; 2699,2749,2793,2842,2872,2911,2953 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 7; 2699,2749,2793,2842,2872,2911,2953 |
| RR combined [mean] | S8 Queens House | High | 2.512 ms | 7.53% | 7; 2699,2749,2793,2842,2872,2911,2953 |
| Post render chain [mean] | S8 Queens House | High | 0.144 ms | 0.43% | 7; 2698,2748,2792,2841,2871,2910,2952 |
| Post output grade [mean] | S8 Queens House | High | 0.065 ms | 0.19% | 7; 2698,2748,2792,2841,2871,2910,2952 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 8; 2715,2751,2798,2827,2863,2892,2927,2956 |
| Scene total static | S8 Queens House | High | 1.177 ms | 3.53% | 8; 2710,2745,2773,2821,2859,2887,2923,2949 |
| Scene static convert/extract | S8 Queens House | High | 0.482 ms | 1.44% | 8; 2710,2745,2773,2821,2859,2887,2923,2949 |
| Scene static BLAS | S8 Queens House | High | 0.362 ms | 1.09% | 8; 2710,2745,2773,2821,2859,2887,2923,2949 |
| Scene static TLAS | S8 Queens House | High | 0.330 ms | 0.99% | 8; 2710,2745,2773,2821,2859,2887,2923,2949 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 2717,2758,2796,2843,2873,2912,2955 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 2717,2758,2796,2843,2873,2912,2955 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 2717,2758,2796,2843,2873,2912,2955 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | 0.015 ms | 0.04% | 1; 2791 |

### H: pan-q [proxy], 12:16:03.015-12:16:36.798

Rendered-frame median: 33.315 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.642 ms | 4.93% | 3; 3893,4103,4266 |
| RT region [mean, nested] | S8 Queens House | High | 1.102 ms | 3.31% | 4; 2985,3975,4151,4325 |
| GI trace | S8 Queens House | High | 0.310 ms | 0.93% | 4; 2986,3976,4152,4326 |
| Local shadows incl. join | S8 Queens House | High | 0.128 ms | 0.38% | 4; 2992,3984,4170,4332 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 4; 2992,3984,4170,4332 |
| Reflections | S8 Queens House | High | 0.362 ms | 1.09% | 4; 2990,3982,4168,4330 |
| RR evaluate [mean] | S8 Queens House | High | 2.534 ms | 7.61% | 7; 3245,3891,3992,4099,4181,4264,4424 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 7; 3245,3891,3992,4099,4181,4264,4424 |
| RR combined [mean] | S8 Queens House | High | 2.535 ms | 7.61% | 7; 3245,3891,3992,4099,4181,4264,4424 |
| Post render chain [mean] | S8 Queens House | High | 0.146 ms | 0.44% | 7; 3244,3890,3991,4098,4180,4263,4423 |
| Post output grade [mean] | S8 Queens House | High | 0.062 ms | 0.19% | 7; 3244,3890,3991,4098,4180,4263,4423 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 9; 2989,3858,3947,4041,4102,4167,4234,4297,4466 |
| Scene total static | S8 Queens House | High | 1.179 ms | 3.54% | 2; 2983,3796 |
| Scene total moving | S8 Queens House | High | 1.183 ms | 3.55% | 8; 3797,3931,4025,4083,4149,4218,4281,4450 |
| Scene static convert/extract | S8 Queens House | High | 0.483 ms | 1.45% | 2; 2983,3796 |
| Scene static BLAS | S8 Queens House | High | 0.365 ms | 1.10% | 2; 2983,3796 |
| Scene static TLAS | S8 Queens House | High | 0.330 ms | 0.99% | 2; 2983,3796 |
| Scene camera moving convert/extract | S8 Queens House | High | 0.217 ms | 0.65% | 8; 3797,3931,4025,4083,4149,4218,4281,4450 |
| Scene camera moving BLAS | S8 Queens House | High | 0.652 ms | 1.96% | 8; 3797,3931,4025,4083,4149,4218,4281,4450 |
| Scene camera moving TLAS | S8 Queens House | High | 0.318 ms | 0.95% | 8; 3797,3931,4025,4083,4149,4218,4281,4450 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 3338,3892,4014,4101,4182,4265,4434 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 3338,3892,4014,4101,4182,4265,4434 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 3338,3892,4014,4101,4182,4265,4434 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### H: pan-e [proxy], 12:16:36.798-12:17:10.023

Rendered-frame median: 33.315 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.732 ms | 5.20% | 4; 4536,4760,4979,5204 |
| RT region [mean, nested] | S8 Queens House | High | 1.140 ms | 3.42% | 3; 4601,4830,5048 |
| GI trace | S8 Queens House | High | 0.312 ms | 0.94% | 3; 4602,4831,5049 |
| Local shadows incl. join | S8 Queens House | High | 0.127 ms | 0.38% | 3; 4620,4852,5067 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 3; 4620,4852,5067 |
| Reflections | S8 Queens House | High | 0.361 ms | 1.08% | 3; 4618,4839,5065 |
| RR evaluate [mean] | S8 Queens House | High | 2.490 ms | 7.47% | 7; 4528,4657,4746,4865,4975,5102,5190 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 7; 4528,4657,4746,4865,4975,5102,5190 |
| RR combined [mean] | S8 Queens House | High | 2.491 ms | 7.48% | 7; 4528,4657,4746,4865,4975,5102,5190 |
| Post render chain [mean] | S8 Queens House | High | 0.147 ms | 0.44% | 7; 4527,4656,4745,4864,4974,5101,5189 |
| Post output grade [mean] | S8 Queens House | High | 0.063 ms | 0.19% | 7; 4527,4656,4745,4864,4974,5101,5189 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 8; 4535,4617,4713,4799,4894,4978,5064,5156 |
| Scene total static | S8 Queens House | High | 1.326 ms | 3.98% | 1; 4510 |
| Scene total moving | S8 Queens House | High | 1.206 ms | 3.62% | 8; 4511,4599,4697,4783,4878,4959,5046,5140 |
| Scene static convert/extract | S8 Queens House | High | 0.588 ms | 1.76% | 1; 4510 |
| Scene static BLAS | S8 Queens House | High | 0.390 ms | 1.17% | 1; 4510 |
| Scene static TLAS | S8 Queens House | High | 0.336 ms | 1.01% | 1; 4510 |
| Scene camera moving convert/extract | S8 Queens House | High | 0.215 ms | 0.65% | 8; 4511,4599,4697,4783,4878,4959,5046,5140 |
| Scene camera moving BLAS | S8 Queens House | High | 0.681 ms | 2.05% | 8; 4511,4599,4697,4783,4878,4959,5046,5140 |
| Scene camera moving TLAS | S8 Queens House | High | 0.319 ms | 0.96% | 8; 4511,4599,4697,4783,4878,4959,5046,5140 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 4533,4658,4747,4866,4976,5103,5191 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 4533,4658,4747,4866,4976,5103,5191 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 4533,4658,4747,4866,4976,5103,5191 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | 0.020 ms | 0.06% | 1; 4526 |

### H: rotate-r [proxy], 12:17:10.023-12:17:43.428

Rendered-frame median: 33.300 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.635 ms | 4.91% | 3; 5865,6202,6536 |
| RT region [mean, nested] | S8 Queens House | High | 1.117 ms | 3.35% | 4; 5309,6028,6384,6716 |
| GI trace | S8 Queens House | High | 0.311 ms | 0.93% | 4; 5310,6029,6385,6717 |
| Local shadows incl. join | S8 Queens House | High | 0.127 ms | 0.38% | 3; 5346,6065,6393 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 3; 5346,6065,6393 |
| Reflections | S8 Queens House | High | 0.358 ms | 1.08% | 3; 5317,6063,6390 |
| RR evaluate [mean] | S8 Queens House | High | 2.399 ms | 7.20% | 6; 5404,5861,6076,6200,6425,6523 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 6; 5404,5861,6076,6200,6425,6523 |
| RR combined [mean] | S8 Queens House | High | 2.399 ms | 7.21% | 6; 5404,5861,6076,6200,6425,6523 |
| Post render chain [mean] | S8 Queens House | High | 0.143 ms | 0.43% | 6; 5403,5860,6075,6199,6424,6522 |
| Post output grade [mean] | S8 Queens House | High | 0.059 ms | 0.18% | 6; 5403,5860,6075,6199,6424,6522 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 8; 5227,5526,5864,6062,6130,6306,6453,6535 |
| Scene total static | S8 Queens House | High | 1.379 ms | 4.14% | 9; 5222,5503,5838,6009,6110,6253,6448,6502,6713 |
| Scene total moving | S8 Queens House | High | 1.366 ms | 4.10% | 9; 5223,5504,5839,6010,6111,6254,6449,6503,6714 |
| Scene static convert/extract | S8 Queens House | High | 0.580 ms | 1.74% | 9; 5222,5503,5838,6009,6110,6253,6448,6502,6713 |
| Scene static BLAS | S8 Queens House | High | 0.448 ms | 1.35% | 9; 5222,5503,5838,6009,6110,6253,6448,6502,6713 |
| Scene static TLAS | S8 Queens House | High | 0.328 ms | 0.98% | 9; 5222,5503,5838,6009,6110,6253,6448,6502,6713 |
| Scene camera moving convert/extract | S8 Queens House | High | 0.218 ms | 0.65% | 9; 5223,5504,5839,6010,6111,6254,6449,6503,6714 |
| Scene camera moving BLAS | S8 Queens House | High | 0.826 ms | 2.48% | 9; 5223,5504,5839,6010,6111,6254,6449,6503,6714 |
| Scene camera moving TLAS | S8 Queens House | High | 0.319 ms | 0.96% | 9; 5223,5504,5839,6010,6111,6254,6449,6503,6714 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 6; 5405,5862,6077,6201,6435,6533 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 6; 5405,5862,6077,6201,6435,6533 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 6; 5405,5862,6077,6201,6435,6533 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | 0.019 ms | 0.06% | 1; 6521 |

### H: rotate-l [proxy], 12:17:43.428-12:18:17.330

Rendered-frame median: 33.300 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.681 ms | 5.05% | 3; 6864,7037,7209 |
| RT region [mean, nested] | S8 Queens House | High | 1.131 ms | 3.40% | 3; 6938,7131,7247 |
| GI trace | S8 Queens House | High | 0.307 ms | 0.92% | 3; 6939,7132,7248 |
| Local shadows incl. join | S8 Queens House | High | 0.127 ms | 0.38% | 4; 6726,6957,7138,7254 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 4; 6726,6957,7138,7254 |
| Reflections | S8 Queens House | High | 0.348 ms | 1.04% | 4; 6724,6955,7136,7252 |
| RR evaluate [mean] | S8 Queens House | High | 2.383 ms | 7.16% | 7; 6738,6850,6962,7030,7149,7207,7271 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 7; 6738,6850,6962,7030,7149,7207,7271 |
| RR combined [mean] | S8 Queens House | High | 2.384 ms | 7.16% | 7; 6738,6850,6962,7030,7149,7207,7271 |
| Post render chain [mean] | S8 Queens House | High | 0.149 ms | 0.45% | 7; 6737,6849,6961,7029,7148,7206,7270 |
| Post output grade [mean] | S8 Queens House | High | 0.059 ms | 0.18% | 7; 6737,6849,6961,7029,7148,7206,7270 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 9; 6723,6841,6910,6980,7033,7135,7186,7240,7290 |
| Scene total static | S8 Queens House | High | 1.315 ms | 3.95% | 8; 6800,6902,6975,7025,7128,7181,7235,7285 |
| Scene total moving | S8 Queens House | High | 1.306 ms | 3.92% | 8; 6801,6903,6976,7026,7129,7182,7236,7286 |
| Scene static convert/extract | S8 Queens House | High | 0.581 ms | 1.74% | 8; 6800,6902,6975,7025,7128,7181,7235,7285 |
| Scene static BLAS | S8 Queens House | High | 0.415 ms | 1.25% | 8; 6800,6902,6975,7025,7128,7181,7235,7285 |
| Scene static TLAS | S8 Queens House | High | 0.320 ms | 0.96% | 8; 6800,6902,6975,7025,7128,7181,7235,7285 |
| Scene camera moving convert/extract | S8 Queens House | High | 0.206 ms | 0.62% | 8; 6801,6903,6976,7026,7129,7182,7236,7286 |
| Scene camera moving BLAS | S8 Queens House | High | 0.776 ms | 2.33% | 8; 6801,6903,6976,7026,7129,7182,7236,7286 |
| Scene camera moving TLAS | S8 Queens House | High | 0.319 ms | 0.96% | 8; 6801,6903,6976,7026,7129,7182,7236,7286 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 6739,6851,6963,7031,7151,7208,7272 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 6739,6851,6963,7031,7151,7208,7272 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 6739,6851,6963,7031,7151,7208,7272 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### H: static-b [proxy], 12:18:17.330-12:18:53.774

Rendered-frame median: 33.320 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 1.694 ms | 5.08% | 4; 7340,7428,7510,7584 |
| RT region [mean, nested] | S8 Queens House | High | 1.150 ms | 3.45% | 3; 7363,7455,7535 |
| GI trace | S8 Queens House | High | 0.304 ms | 0.91% | 3; 7364,7456,7536 |
| Local shadows incl. join | S8 Queens House | High | 0.122 ms | 0.37% | 3; 7370,7462,7542 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 3; 7370,7462,7542 |
| Reflections | S8 Queens House | High | 0.323 ms | 0.97% | 3; 7368,7460,7540 |
| RR evaluate [mean] | S8 Queens House | High | 2.470 ms | 7.41% | 7; 7336,7381,7419,7453,7491,7522,7575 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 7; 7336,7381,7419,7453,7491,7522,7575 |
| RR combined [mean] | S8 Queens House | High | 2.472 ms | 7.42% | 7; 7336,7381,7419,7453,7491,7522,7575 |
| Post render chain [mean] | S8 Queens House | High | 0.148 ms | 0.44% | 7; 7335,7380,7418,7452,7490,7521,7574 |
| Post output grade [mean] | S8 Queens House | High | 0.059 ms | 0.18% | 7; 7335,7380,7418,7452,7490,7521,7574 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 8; 7339,7367,7417,7445,7480,7509,7539,7576 |
| Scene total static | S8 Queens House | High | 1.205 ms | 3.62% | 8; 7331,7360,7398,7441,7476,7505,7533,7570 |
| Scene total moving | S8 Queens House | High | 1.526 ms | 4.58% | 1; 7332 |
| Scene static convert/extract | S8 Queens House | High | 0.488 ms | 1.46% | 8; 7331,7360,7398,7441,7476,7505,7533,7570 |
| Scene static BLAS | S8 Queens House | High | 0.374 ms | 1.12% | 8; 7331,7360,7398,7441,7476,7505,7533,7570 |
| Scene static TLAS | S8 Queens House | High | 0.339 ms | 1.02% | 8; 7331,7360,7398,7441,7476,7505,7533,7570 |
| Scene camera moving convert/extract | S8 Queens House | High | 0.266 ms | 0.80% | 1; 7332 |
| Scene camera moving BLAS | S8 Queens House | High | 0.919 ms | 2.76% | 1; 7332 |
| Scene camera moving TLAS | S8 Queens House | High | 0.335 ms | 1.01% | 1; 7332 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 7337,7382,7414,7447,7488,7519,7560 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 7337,7382,7414,7447,7488,7519,7560 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 7; 7337,7382,7414,7447,7488,7519,7560 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | 0.016 ms | 0.05% | 1; 7487 |

### H: diagnostic-settings [proxy], 12:18:53.774-12:18:54.967

Rendered-frame median: NR. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| FG input copy | S8 Queens House | High | 0.026 ms | NR | 1; 7605 |
| Scene total static | S8 Queens House | High | 1.203 ms | NR | 1; 7601 |
| Scene static convert/extract | S8 Queens House | High | 0.489 ms | NR | 1; 7601 |
| Scene static BLAS | S8 Queens House | High | 0.374 ms | NR | 1; 7601 |
| Scene static TLAS | S8 Queens House | High | 0.339 ms | NR | 1; 7601 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7607 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7607 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7607 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### H: diagnostic-OMM [proxy], 12:18:54.967-12:19:14.712

Rendered-frame median: 33.340 ms. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| GI enclosing total [mean] | S8 Queens House | High | 12.345 ms | 37.03% | 2; 7687,7773 |
| RT region [mean, nested] | S8 Queens House | High | 8.002 ms | 24.00% | 2; 7619,7720 |
| GI trace | S8 Queens House | High | 1.946 ms | 5.84% | 2; 7620,7721 |
| Local shadows incl. join | S8 Queens House | High | 0.123 ms | 0.37% | 2; 7628,7726 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | 0.04% | 2; 7628,7726 |
| Reflections | S8 Queens House | High | 0.321 ms | 0.96% | 2; 7624,7724 |
| RR evaluate [mean] | S8 Queens House | High | 2.460 ms | 7.38% | 4; 7616,7663,7700,7763 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | 0.00% | 4; 7616,7663,7700,7763 |
| RR combined [mean] | S8 Queens House | High | 2.463 ms | 7.39% | 4; 7616,7663,7700,7763 |
| Post render chain [mean] | S8 Queens House | High | 0.142 ms | 0.43% | 4; 7615,7662,7699,7762 |
| Post output grade [mean] | S8 Queens House | High | 0.064 ms | 0.19% | 4; 7615,7662,7699,7762 |
| FG input copy | S8 Queens House | High | 0.026 ms | 0.08% | 5; 7652,7686,7723,7764,7797 |
| Scene total static | S8 Queens House | High | 1.206 ms | 3.62% | 5; 7648,7682,7717,7758,7793 |
| Scene static convert/extract | S8 Queens House | High | 0.489 ms | 1.47% | 5; 7648,7682,7717,7758,7793 |
| Scene static BLAS | S8 Queens House | High | 0.373 ms | 1.12% | 5; 7648,7682,7717,7758,7793 |
| Scene static TLAS | S8 Queens House | High | 0.339 ms | 1.02% | 5; 7648,7682,7717,7758,7793 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 3; 7659,7697,7742 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 3; 7659,7697,7742 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 3; 7659,7697,7742 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### H: ab-off, 12:19:14.712-12:19:18.515

Rendered-frame median: NR. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| RR evaluate [mean] | S8 Queens House | High | 2.427 ms | NR | 1; 7808 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | NR | 1; 7808 |
| RR combined [mean] | S8 Queens House | High | 2.428 ms | NR | 1; 7808 |
| Post render chain [mean] | S8 Queens House | High | 0.141 ms | NR | 1; 7807 |
| Post output grade [mean] | S8 Queens House | High | 0.065 ms | NR | 1; 7807 |
| FG input copy | S8 Queens House | High | 0.026 ms | NR | 1; 7836 |
| Scene total static | S8 Queens House | High | 1.206 ms | NR | 1; 7833 |
| Scene static convert/extract | S8 Queens House | High | 0.489 ms | NR | 1; 7833 |
| Scene static BLAS | S8 Queens House | High | 0.376 ms | NR | 1; 7833 |
| Scene static TLAS | S8 Queens House | High | 0.338 ms | NR | 1; 7833 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7802 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7802 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7802 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### H: ab-on, 12:19:18.515-12:19:22.961

Rendered-frame median: NR. Values assigned by receipt timestamp; see rolling-window limits above.

| Pass | Scene | Preset | Median ms | Share of frame | n; log lines |
|---|---|---|---:|---:|---|
| RT region [mean, nested] | S8 Queens House | High | 9.137 ms | NR | 1; 7847 |
| GI trace | S8 Queens House | High | 3.391 ms | NR | 1; 7848 |
| Local shadows incl. join | S8 Queens House | High | 0.124 ms | NR | 1; 7856 |
| LS join [nested] | S8 Queens House | High | 0.013 ms | NR | 1; 7856 |
| Reflections | S8 Queens House | High | 0.326 ms | NR | 1; 7855 |
| RR evaluate [mean] | S8 Queens House | High | 2.429 ms | NR | 1; 7858 |
| RR guides [mean] | S8 Queens House | High | 0.001 ms | NR | 1; 7858 |
| RR combined [mean] | S8 Queens House | High | 2.430 ms | NR | 1; 7858 |
| Post render chain [mean] | S8 Queens House | High | 0.143 ms | NR | 1; 7857 |
| Post output grade [mean] | S8 Queens House | High | 0.065 ms | NR | 1; 7857 |
| FG input copy | S8 Queens House | High | 0.026 ms | NR | 1; 7874 |
| Scene total static | S8 Queens House | High | 1.206 ms | NR | 1; 7871 |
| Scene static convert/extract | S8 Queens House | High | 0.488 ms | NR | 1; 7871 |
| Scene static BLAS | S8 Queens House | High | 0.375 ms | NR | 1; 7871 |
| Scene static TLAS | S8 Queens House | High | 0.339 ms | NR | 1; 7871 |
| CPU submit [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7853 |
| CPU descriptor tracker [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7853 |
| CPU alpha descriptor [watch sum; disabled] | S8 Queens House | High | 0.000 ms | NR | 1; 7853 |
| CPU life bookkeeping [per frame; 60 s] | S8 Queens House | High | NR | NR | 0 |

### Cost interpretation and missing observations

Final F static-a has GI trace 7.461 ms (22.44% of 33.250 ms rendered cadence), RR evaluate 2.426 ms (7.30%), scene total static 1.183 ms (3.56%), reflections 1.137 ms (3.42%), cascade captured pass 0.653 ms (1.96%) and local shadows 0.648 ms (1.95%). These are the six expensive separately visible regions used for code inspection; scene moving rises to 3.079 ms in pan-q, and Dialogue1 LS reaches 1.212 ms. The ranking is scene dependent. Cascade's total is an upper bound on removable incremental capture cost. Water compose is next at 0.432-0.536 ms in F measured motion/static intervals.

F final ab-on lasts only 1.563 s before High settings reset and has no fresh full GI/LS/RF timing receipt; NR is intentional. F ab-off can contain preceding ON samples and is not a clean OFF GPU baseline. D static-b has no GI trace receipt. U ab-on contains rolling transition samples; do not use it to calculate a toggle saving. H's 300-frame GI/LS/RF summaries lag the proxy boundaries and diagnostic settings; no direct comparison with F is valid.

All GPU-labelled cost families were inspected, including emissive build/capture/calibration and nested water copy/dispatch costs. Water sun, local shadows and refraction repeat the compose timer, rather than adding new rows three times. Other GPU mentions concern capabilities, allocation, warnings or fences, not pass timings. FG neural evaluation itself has no exposed separate timer; only its input copy is measured here. Game raster, transparency, native post and isolated sun/primary origin remain NR.

CPU evidence outside measured windows is retained as context: F 23:36:54.370 reports life bookkeeping 0.018 ms/frame over 1,439 frames (warmup); D 23:29:06.511 reports 0.020 ms/frame over 1,386 frames (warmup); U 21:01:14.376 reports 0.028 ms/frame over 1,547 frames (producer warmup). These minute-wide histories are not measured-window CPU medians. H life receipts inside proxies still cover about a minute, not that phase alone. `[watch]` submit/tracker sums of 0.000 ms are disabled counters; source: `layer/perf_submit.h:7`, `layer/tracer.cpp:44`. Lifetime timing is independent (`layer/tracker.cpp:77`, `:208`). None proves a CPU bottleneck or a CPU saving.

### Cheap cached shader inspection

Read the existing main shader cache index with SQLite `mode=ro&immutable=1` and decoded cached SPIR-V instruction headers; no DXC invocation, debug compile, cache write or GPU work. These are the newest cached matching requests, not verified compiler artifacts of this worktree or the deciding live build. Counts include unspecialized branches; OpLoad is not a physical memory transaction count.

| Cached request family | SHA-256 prefix | Bytes | SPIR-V instructions | OpLoad / OpStore | Loops / selections |
|---|---|---:|---:|---:|---:|
| gi_trace_rt_hit_sun_em; native, hit instancing, cascade, far specialization | 1e1c07c53575 | 1,230,984 | 71,319 | 4,320 / 82 | 37 / 4,770 |
| rt_reflections_hit_shade_em; native, hair origin, hit instancing, cascade, hit GI specialization | f47bad52bb34 | 1,970,012 | 114,164 | 6,694 / 105 | 57 / 7,655 |
| rt_local_shadows_box_k; native, hair origin, cascade specialization | 785f671edcac | 962,520 | 55,033 | 3,339 / 73 | 45 / 3,418 |

This supports trying shorter live state and less divergent work, not claiming a register count. DXC -Zi does not supply NVIDIA physical-register allocation or achieved occupancy; those require driver/ISA profiling. No new compile is justified for a read-only candidate map. Existing local-shadow split/reload already addresses register pressure (`core/gi.cpp:3269`, `shaders/gi/rt_local_shadows.hlsl:1342`); simply undoing those reloads may make occupancy worse.

## Candidates ranked

These are proposals, not measured wins. Rank uses midpoint of estimated saving x subjective implementation confidence; confidence is the chance of a useful positive result, not a statistical probability. Savings include a possible 0 ms if the compiler already removes the work or the relevant workload is absent. Estimates are for the measured RTX 5090 scenes, not other GPUs. Do not add them: several overlap GI/AS work. All changes would start behind off-by-default central settings with specialization constants for shader branches; no new variant requests unless a layout requires one.

| Rank | Candidate / region | Planning saving | Confidence | Midpoint x confidence |
|---|---|---:|---:|---:|
| 1 | Reject roof-blocked far GI shading before it runs | 0.30-1.50 ms GPU | 0.70 | 0.630 ms |
| 2 | Reflections: bypass probe/BRDF evaluation for ineligible receivers | 0.05-0.25 ms GPU | 0.85 | 0.128 ms |
| 3 | Scene/cascade conversion: coalesce strided triangle accesses | 0.05-0.30 ms GPU | 0.60 | 0.105 ms |
| 4 | GI temporal: current-sample path when RR forces history weight 1 | 0.03-0.12 ms GPU | 0.70 | 0.053 ms |
| 5 | Scene extraction: batch small extraction/audit dispatches | 0.02-0.12 ms GPU | 0.65 | 0.046 ms |
| 6 | Cascade: reuse only positively proven unchanged opaque geometry | 0.10-0.35 ms GPU | 0.20 | 0.045 ms |
| 7 | Water: skip empty tiles without dropping writes or group reduction | 0.01-0.08 ms GPU | 0.60 | 0.027 ms |
| 8 | Local shadows: reuse identical selected visibility queries | 0.01-0.15 ms GPU in duplicate-heavy lists; likely 0 ms here | 0.10 | 0.008 ms |
| 9 | Lifetime references: deduplicate once per command-buffer recording | 0.003-0.012 ms CPU | 0.65 | 0.005 ms |
| 10 | RR wrapper: narrow proven dependencies, preserve SDK contract | 0.00-0.03 ms GPU | 0.30 | 0.005 ms |

### 1. Roof rejection before hidden-hit GI shading

Code: `shaders/gi/gi_trace_rt.hlsl:128`, `:399`, `:412`, `:421`, `:451`, `:458`, `:487`, `:510`; `shaders/gi/gi_far.hlsli:43`. Dispatch selection: `core/gi.cpp:3045` and the hit-GI dispatch helpers at `:2340` onward. Ultra traces two rays at full internal resolution. A hidden hit beyond RB can evaluate its material, local-light visibility, sun visibility and hit AO before `gi_far_roof_clear(o, dir, t)` makes `far_sun` false. `gi_far_hit` then rejects all direct bounce beyond RB and uses fr.w0 = 0. This is real discarded work, rather than reducing samples.

Mechanism: for far specialization ON, hidden non-aborted t > RB hits, evaluate that same roof predicate before material/direct/sun/AO work. A failed predicate can take the existing zero-direct fallback immediately. Preserve emission already added at :403-404 and all screen/SHaRC paths, near-hit work, output depths and accumulation order of surviving terms. Keep the existing path in diagnostic modes if counters would otherwise change. No near-hit or sky/fallback estimator change. Hoist only the predicate that already rejects that exact ray; never infer it from a screen roof mask.

Estimate: 0.30-1.50 ms, assuming 4-20% of the 7.36-8.08 ms trace is removable hidden-hit shading, minus the extra roof scans for hits that previously failed material/light qualification before reaching that test. The logs do not give this intersection of predicates, so this is the first useful candidate, not a promised 1.50 ms. Main risk: moving a long roof loop earlier can cost more than it saves; emission, fallback and diagnostic side effects can be accidentally dropped. Unknown roofs must keep the same fail-closed rule.

Offline: use existing `frame_replay` production hit-GI/far support and replay packs carrying sun, roofs, materials, emission and the correct specialization/push values; refuse incomplete packs. Compare giRaw, giRawDepth and composed scene floats for identical seeds, including unknown roofs, t = RB, aborted queries, emitters and cut-away roofs. Extend the existing analytic far fixture (`tools/native_hit_gpu_fixture.hlsl`, gi_far_hit) for branch boundaries. PT is a secondary physical sanity check; equality to the same production baseline is the primary gate, since PT lacks some terms. Use `tools/shader_identity.py` for the disabled path; `tools/release_shader_equivalence.py` when comparing frozen specialization branches. No offline check was run in this audit.

Live after explicit authorization: S8 Queens House and Dialogue1, one run per save, baseline/candidate/baseline/candidate static and motion windows with all qualified Ultra keys on. Predict unchanged energy/coverage and 0.30-1.50 ms lower trace, no change to LS/RF cost. Inspect room roofs, emissive hits and far bounce; reject lower energy or extra unknowns. First record rejected-hit share in a short diagnostic window; do not put diagnostics in cost windows.

### 2. Reflection ineligible-pixel early-out

Code: `shaders/gi/rt_reflections.hlsl:186-234`, `:433-436`, `:486-495`; dispatch `core/gi.cpp:2470`. The pass computes `bg3_cluster_raw`, all ambient probes, split-sum weights and specular occlusion before testing material model and roughness eligibility. In ordinary non-validation mode, ineligible receivers never trace a ray, w stays 0 and their expensive Tp/E is discarded.

Mechanism: after computing the same material model and derivative-adjusted roughness, for rf_debug = rf_stats = rf_validate = 0, branch before :223's ambient work if eligibility is false. Write rfHitDist = kNoHit and preserve the original gSpecular max/finite fallback semantics (do not assume the original is positive/finite). gDiffuse remains untouched. Leave diagnostics on the full path. This removes per-pixel probe fetches, branch state and unnecessary stores/loads without changing sampled rays or roughness coverage.

Estimate: 0.05-0.25 ms, about 5-22% of the 1.11-1.18 ms S8 reflection pass if a substantial receiver fraction fails eligibility; no logged eligible fraction in cost mode establishes that share. Risk: divergent early-outs can leave low wave utilization; preserving NaN/negative handling and hit-distance writes is essential. Keeping ambient state live across nested shading also pressures registers; computing it later is a follow-up only if ISA/replay shows a benefit, because a compiler may already schedule it well.

Offline: full reflection replay only when its lighting sets/probes/sky/DFG inputs are present (`tools/frame_replay.md`); compare specular and hit-distance bytes for identical seeds, including NaN/Inf/negative input, invalid depth, roughness endpoints and all model flags. Existing gi_hit_direct-only packs cannot prove this pass. Disabled-path shader identity/equivalence, then full-buffer output equality. Live: S8 pan and Dialogue1, predict RF 0.05-0.25 ms lower with unchanged GI/LS/energy; inspect rough surfaces, mirrors, disocclusions and RR specular hit distances.

### 3. Coalesced XFB conversion for scene and cascade

Code: `shaders/rt/xfb_to_world.hlsl:26-72`, `shaders/rt/xfb_to_world_ortho.hlsl:1`; `layer/rtscene.cpp:921-945`, `:952-975`. Every lane processes one triangle and issues three 16-byte loads/stores separated by the captured vertex stride; adjacent lanes are separated by 3 x stride (often large when material fields are captured). This is a memory-access candidate, not evidence of uncoalesced hardware transactions from SPIR-V counts.

Mechanism: map adjacent lanes to adjacent triangle corners, then combine the same three `vok` predicates per triangle and perform the original collapse-to-w[0] rule. Alternatively test tiled staging with a fixed corner-to-lane map. Keep each vertex's arithmetic, matrix precision and rounding identical; change scheduling only. Capture fields after position must remain untouched. Do not add unconditional full-group barriers to sky/invalid early-outs.

Estimate: 0.05-0.30 ms combined scene/cascade GPU, assuming 8-30% of F static conversion/extraction (~0.59-0.66 ms) plus some conversion within the 0.63-0.66 ms cascade region is address/coalescing overhead. Extraction and native raster are not all removable; subgroup exchange/staging can erase the gain. Risk: wave boundaries across triangles, partial tail triangles, large-stride transactions and invalid vertex collapse. This candidate touches both regions; never count its saving twice.

Offline: replay recorded XFB and camera UBO through old/new conversion kernels; compare every output byte for perspective and orthographic matrices, 16/24/88-byte and actual supported strides, NaN/Inf, overridden sky depth, singular matrices and partial groups. Existing scene/mesh fixtures provide the invalid-triangle contract; synthetic conversion fixtures may need extending. GPU replay would be run separately, at BelowNormal priority. Live: S8 static/pan, scene convert/extract and cascade timestamps plus total build+trace. Predict the 0.05-0.30 ms combined reduction, unchanged captured triangles, rt_primary_check and zero new unknowns.

### 4. GI current sample under RR

Code: `core/gi.cpp:2098-2103`, `:3062`; `shaders/gi/gi_temporal.hlsl:32-67`. RR forces pc.history = 1, but the temporal shader still reads velocity, old history/depth, reconstructs positions/normals and evaluates depth rejection. Both accepted and rejected history should produce the current sample mathematically.

Mechanism: a history == 1 path copies current sample/depth while retaining bindings and ring lifetime. Do not alias history images initially. Floating lerp evaluation can differ from direct cur even for finite history, and invalid current samples need the same downstream behavior, so this is not assumed byte-exact merely from the algebra. Keep it only after proving equality or an encoding-derived invisible bound at the stored output; no arbitrary brightness threshold.

Estimate: 0.03-0.12 ms, an unseparated full-resolution reprojection dispatch, not the 0.08-0.18 ms **rr_post_temporal** timer (a different pass). This candidate already appeared in `docs/PERF_AUDIT_GPU.md`; it remains pending, not newly discovered or already implemented. Risk: finite rounding, NaN recovery, stale depth/history and changing the RR-off path.

Offline: deterministic temporal fixtures with valid/rejected/NaN/Inf history and motion vectors, disocclusions, res_shift 0/1, ring swaps and history weights just below/at 1; compare stored formats, depth and resulting AO/composite. Live: S8 and Dialogue1, predict enclosing RT/GI cost lower by 0.03-0.12 ms with trace/LS/RF unchanged; same static float energy, no new trails or disocclusion darkening.

### 5. Batch small scene extraction and audit dispatches

Code: `layer/bvhsplit.cpp:3186-3224`, `shaders/rt/mesh_extract.hlsl`, `shaders/rt/mesh_extract_basis.hlsl`. Each Ext entry records its own push constants and dispatch, even when a job has very few triangles; descriptors are already bound once, and a shared barrier already follows the loop. Do not claim that loop has one barrier per mesh.

Mechanism: a compact job table and prefix/group mapping dispatch multiple small jobs together, preserving each job's original matrix, tolerance, status destination, triangle order and completion tracking. Keep large jobs unchanged. This may improve occupancy of small launches and reduce command recording without changing extraction/audit results. No blanket audit removal: audits invalidate bad cached geometry.

Estimate: 0.02-0.12 ms GPU if 3-20% of the observed ~0.6 ms convert+extract is small-job launch overhead. Existing logs do not isolate extraction, job counts or CPU encoding time, so no positive CPU-ms estimate is supported. Mostly relevant during new-mesh churn, possibly 0 ms on a fully cached static view. Risk: status races, job indexing, synchronization and changing cache publication timing. Static BLAS cache, dynamic refit and TLAS refit already exist (`layer/bvhsplit.cpp:2363`, `:3316`, `:3452`); this is not a proposal to add them again.

Offline: replay extraction/audit fixtures byte-for-byte including status words, collisions, invalid/collapsed geometry and disjoint job output ranges; compare cache acceptance/eviction decisions and failed-submit lifetime behavior. Live: S8 pan with extraction counters in a separate diagnostic window; predict 0.02-0.12 ms only when small jobs occur, no change to coverage and same-frame scene readiness. Judge build + subsequent trace together, not only a faster build.

### 6. Cascade reuse with complete immutability proof

Code: `layer/rtscene.cpp:952-977`, `layer/bvhsplit.cpp:2916-2947`, `:3346`; capture timing is the full native cascade pass plus XFB/conversion. Cascade BLAS already refits for stable list signature/count; periodic rebuild defaults to 60 refits. Draw-list equality alone does not prove unchanged vertices or UBOs.

Mechanism: retain an immutable opaque subset's already converted geometry/BLAS only while buffer/image/descriptor generations, transform/camera origin, VS inputs and every animation/displacement dependency are proven unchanged. Keep animated/unclassified casters on the existing capture/refit path and invalidate conservatively. Begin with one positively classified rigid subset, not broad camera-static caching. Avoid a new general provenance framework; use existing content epochs and mesh classifications only where they cover all dependencies.

Estimate: 0.10-0.35 ms in a rigid-heavy static S8 view, assuming 15-50% of the 0.65 ms captured-pass region can actually be skipped. Native shadow raster may still run, so this is an optimistic ceiling and confidence is low. Additional BLAS reuse could save part of scene BLAS time but is deliberately excluded from the estimate. Risk: wind, skinning, shader-time parameters, camera-relative conversion and descriptor mutations make stale shadows visible. If existing epochs cannot prove immutability cheaply, reject rather than invent tooling.

Offline: mutation/invalidation fixtures and identical triangle/ray-hit replay after camera rebases, wind/time changes, uploads, descriptor replacement and queue retirement. Require exact current geometry and hit equivalence; AS binary identity is unnecessary. Live: S8 static and pan/rotation, inspect offscreen-caster shadows and roof bounce; predict 0.10-0.35 ms only in proven static subsets, unchanged dynamic work, no stale-frame or primary-coverage regression.

### 7. Empty water tile fast path

Code: `core/water.cpp:1022-1025`, `:1220-1228`, `:1401-1404`; `shaders/rt/water_compose.hlsl:360-417`, `:847`. Compose dispatch covers the whole render target even when only a small subset owns water records. The shader includes group reductions, so a per-lane return before a barrier is invalid.

Mechanism: a uniform whole-group no-owned-record test can bypass record decoding/rays while still reaching required reductions and preserving record consumption/header writes. Keep the full lighting copy because reflection/refraction can sample away from water receivers. Start with an in-dispatch tile fast path; indirect compact dispatch needs extra record list synchronization and may cost more. Water reflection tracing already rejects non-water/ineligible pixels; do not claim all dispatched pixels issue rays.

Estimate: 0.01-0.08 ms, about 2-18% of the 0.432-0.536 ms F compose timer, minus ownership ballot overhead. No saving is assumed for actual water shading. Risk: consumed records, barrier uniformity, partially covered tiles and missing writes when lighting pairs are stale. Offline: existing water compose/shift fixtures plus full record/state output equality for empty/partial/full tiles, multiple water passes and stale-lighting pairs. Live: S8, predict compose lower by 0.01-0.08 ms without changed composed/uncomposed counts, water boundaries, refraction or sun/local shadow terms.

### 8. Reuse identical local-shadow queries after exact-k selection

Code: `shaders/gi/rt_local_shadows.hlsl:1307-1325`, `:1329-1340`, `:1345-1364`, `:616-635`; dispatch/variant selection `core/gi.cpp:3292-3320`. The selected top/remainder slots can refer to the same light, and the shader separately fetches the index/position and traces each selected slot. Phase 3 must retain each slot's independent estimator factor and accumulation order.

Mechanism: within one pixel, reuse an earlier visibility result only if the complete query tuple is identical: selected light, stochastic key (q == 5 changes it), origin, direction, extent, cone and policy. Under the current K1 + one remainder profile top and remainder usually differ, so do not promise a large win there. A second remainder can repeat a light but its different soft-shadow seed forbids unconditional reuse. Cache inexpensive selected indices/ray parameters only if the extra live registers do not reverse the gain. Box loops can use the same exact-query rule where duplicate list entries are proven.

Estimate: 0.01-0.15 ms in duplicate-heavy light lists, bounded by the 0.65-0.70 ms S8 / 1.17-1.21 ms Dialogue1 LS pass; likely near 0 ms for the measured K1 profile if no duplicate tuples exist. Its low ranking reflects the lack of useful duplicates in the measured K1 profile; the positive range applies only to a future duplicate-heavy configuration. Risk: accidentally correlating independent soft samples changes the estimator; extra VGPR state hurts occupancy. The code already splits lightweight tracing from material reloading to avoid that problem.

Offline: ls_check/reference fixtures and deterministic selected-slot records; compare visibility, light sums and final buffers for every K/remainder/soft-shadow configuration, unknown-query fallback and repeated list entries. Require identical estimator factors/order, not just equal mean. Live: Dialogue1 and S8 dense lights, short ls_stats query-duplication diagnostic followed by clean cost windows. Predict at most 0.01-0.15 ms where duplicates are present and identical shadow noise/energy. Drop this candidate if the relevant duplicate count is zero.

### 9. Deduplicate lifetime references per recording

Code: `layer/tracker.cpp:69-98`, `:208`; submit hooks `layer/layer.cpp:481-523`, descriptor tracking `layer/tracker.cpp:844-888`. life_note only removes adjacent duplicate handles. life_on_submit walks the entire vector and writes the same map entry for recurring handles. Same recording/submit/queue uses the same LifeRef value.

Mechanism: retain the original recording vector while recording, then lazily cache a unique handle list per command-buffer recording generation; invalidate on every reference addition/reset/secondary merge. Use it on submit without dropping any handle or changing seq/frame/queue publication and failure semantics. Preserve the existing multi-queue lifetime contract; no resource-retirement relaxation. The existing `perf_submit_batch` flag already implements scratch retention, batched tracker locks and signal coalescing, so those are qualification candidates, not new implementations. Do not suppress driver descriptor writes merely because tracker records look unchanged.

Estimate: 0.003-0.012 ms CPU/frame (about 15-50% of the 0.018-0.028 ms minute-wide context), minus one dedup cost amortized across submissions. Current unique-handle ratio and recording reuse are unknown; on one-shot recordings this may lose. No estimate from the disabled submit/tracker 0.000 ms logs. Risk: stale lists, missed secondary refs, extra allocation and lifetime regressions.

Offline: lifetime/submit-batch fixture tests with repeated nonadjacent refs, rerecording, secondary command buffers, failed submits, handle reuse and multiple queues; assert identical final referenced handle sets and last-use records. Live: independent short CPU diagnostic window, normalize watcher interval sums by rendered presents, then clean S8/Dialogue1 windows. Predict no GPU/energy change, lower life submit CPU only if duplication is substantial, zero new deferred-destroy/device-loss/queue retirement issues.

### 10. RR boundary dependencies

Code: `layer/ngx.cpp:1294-1297` (ALL_COMMANDS to ALL_COMMANDS guide visibility), `:1465-1472` (optional output dispatch barriers); `layer/rrpost.cpp:951-980` and `:1130-1218`. RR evaluate itself costs 2.42-2.59 ms but is an opaque SDK implementation. There is no justified image-identical way here to remove a significant part of that kernel. Guides already cost only ~0.001 ms in steady windows.

Mechanism: narrow a wrapper barrier only after enumerating its actual producers/consumers, including early G-buffer guide recording, native colour/transfer writers, NGX internal accesses and post consumers. Preserve memory access dependencies, image layouts and SDK-required boundaries. Do not simply replace every ALL_COMMANDS by COMPUTE or remove the dependency because guide work is recorded earlier.

Estimate: 0.00-0.03 ms GPU wrapper overhead, not a percentage of the 2.42 ms SDK evaluation. No evidence currently supports a larger saving. Risk: SDK operations are not fully visible, cross-command-buffer hazards, rare corruption/device loss. Offline: synchronization validation and replay scheduling fixtures plus pixel/guide/output equality; these cannot fully emulate the NGX live contract. Live: same-session S8/Dialogue1 A/B, require RR success, no new fallback or API errors and identical energy/detail. Keep only a measured positive win above key-off repeat noise.

### Common qualification contract

No candidate is READY_FOR_LIVE from this audit alone. Implement the smallest off-by-default change, update its dependent offline tests and record a predicted.json before proposing its live batch. Current authorization is for no game launches; this document does not queue or request a batch.

For exact scheduling/caching candidates, require identical produced buffers and visibility answers, preserving stochastic seeds and addition order. Frozen specialization equivalence proves disabled/release paths only; it does not prove an algorithmically changed ON path. Use replay equality for ON. If a change is only invisibly different, derive a bound from the destination encoding and propagate it through subsequent operations; one output ulp alone does not prove invisible temporal behavior. Reject candidates that cannot meet that contract. PT supports physical checks only for terms actually present in the pack; full NGX, live forward water and lifetime behavior still need live evidence.

After separate explicit user steering, group by save into 2-5 minute sessions with toggles in one run, all Ultra qualified keys enabled, `visible_unfocused_fg` when the user is at the PC, patterns/expect/expect_every and a smoke_ultra check. Do not compile/build during cost windows. Use a few complete 120-frame ON/OFF windows (or the actual longer logging period), expensive diagnostics only separately. Demand current-frame scenes, no new capacity drops/unknowns/fallbacks, same static pre-post scene float mean within key-off repeat noise, equal detail/coverage and stable motion. Judge GPU ms when capped/occluded; do not promise FPS from these 33 ms rendered periods. No live job is prepared or queued by this report.

## Rejected ideas

| Idea | Why rejected for this task |
|---|---|
| Ultra two GI rays to one, fewer hit AO rays, smaller far radius, half resolution or checkerboarding | Changes variance, contact coverage, far bounce and/or RR input stability. Equal expected brightness does not establish invisible motion quality. These are preset tradeoffs, not identical-image optimizations. |
| Drop hidden-hit lighting, sun/emission, far GI or cascade casters | Removes visible bounce, emissive contribution or offscreen roof/occluder shadows. Never claim the 7-10 ms GI trace is all waste. |
| Stop GI/reflection closest-hit on first candidate | Wrong nearest hit and screen/material classification; only binary visibility queries can use first accepted hit, which already do so. |
| Lower rt_max_candidates to cap foliage cost or skip tiny alpha/foliage | Produces unknown queries or changes alpha visibility and energy. Current F logs already contain unknowns; performance must not increase their rate. |
| Treat missing alpha data or unsupported materials as opaque/transparent for speed | Reintroduces foliage cardboard or light leaks; invalidates physical coverage. |
| Use stale TLAS / reuse all geometry whenever camera stops | Wind, skinning, displacement and light motion continue; the correct current scene is mandatory. Candidate 6 requires a positive dependency proof for each reused subset. |
| Turn off water refraction/local/sun correction or omit the full lighting copy without access proof | Removes visible water effects or breaks off-water samples. Water sun/local/refraction log timings overlap, so their apparent sum is not a saving opportunity. |
| Replace RR with SR, reduce RR resolution/preset, turn off post temporal or quantization matching | Changes reconstruction/detail/stability and native post matching. RR kernel is opaque; its full 2.4 ms is not available to a wrapper optimization. |
| Broad FP16, fast-math, reassociated sums or compressed material precision | No derived error bound for HDR, roughness, ray origins or recursive lighting; small local errors can alter intersections and temporal history. |
| Reuse visibility for repeated selected light with a different stochastic key | Correlates independent soft-shadow samples. Candidate 8 explicitly requires complete query identity. |
| Remove barriers or merge submits based only on visual success in one scene | Hazards and ordering regressions can be intermittent. Existing submit batching must preserve semaphore/fence/lifetime semantics and be qualified separately. |
| PREFER_FAST_TRACE everywhere, longer unconditional refit intervals, rebuild/refit tuning from only AS cost | Refits already exist; earlier FAST_TRACE trial increased build/refit cost without useful trace improvement (`docs/TRACK_D_SCENE_BUILD.md:475`). Need total build + all trace costs and moving-geometry equivalence; no new visible degradation is allowed. |
| Async compute or SER as an immediate saving estimate | No queue critical-path/driver occupancy evidence in these logs; current same-frame dependencies and GPU contention can prevent overlap. No defensible ms estimate, and this audit does not open a new scheduling research lane. |
| Promote perf_submit_batch / perf_hook_state / perf_hitmat_lists solely from this map | Existing implementations are off in the measured release jobs; disabled CPU timing cannot qualify them. Use their existing offline tests and qualification plans, not duplicate implementations or invented CPU savings. |

## Tooling round opt-tools2, 2026-11-05 (historical)

Both cost analyzers now share the real complete-period text contract. Reports retain per-window GPU medians, OFF repeat spread, signed ON deltas, float pre-post RGB/luminance from rrpost slot 2 dumps.json, and rendered QA verdicts. Detailed captures include optional post histories; animation masks are resized to final-image resolution before the existing detector runs.

Reanalysis of scene 014837 and origin 012845: valid FAIL receipts, not analysis exceptions. Scene convert saves 0.8703 ms versus 0.0750 ms OFF spread; origin ON costs increase by 0.1295/0.0155 ms versus 0.0190 ms OFF spread. Float luminance deltas fit OFF repeat spread for both runs. Every QA pair fails; no quality qualification is claimed. Full reports and retained run-era plans: `.scratch/opt-tools2/`.

At that revision, startup alternatives included roof reject (SpecId 1028) and lazy origin normal (1027) only for dev/detailed logging or explicit opt_prewarm mask 1/2/3. Release startup remains unchanged; layer-only compact holds has no specialization. GI attribution then awaited each feature variant and fresh driver quiescence in dedicated warm phases, with 240 s total warm readiness budget before any measured window. No new shader requests or live runs.

Round 1 supersedes that prewarm/readiness plan: both candidate registrations and
`opt_prewarm` are removed, and the GI attribution job is abandoned.

## Round 6 (2026-11-05 afternoon): lane permutation rejected, attribution

- `gi_far_roof_early` (SpecId 1038): qualified, always on like `rt_compact_hit_state`. Default and every
  preset use 1; release policy forces 1, while dev=1 preserves raw 0 for A/B. No player knob.
  Host statistics mode still forces the original path. Exact (bit-equal); shader request delta 0, ABI unchanged.
  S8 Ultra `opt-roof-s8.1.211159` VALID: GI trace OFF/ON/OFF/ON 7.655/6.275/7.937/6.226 ms
  (-1.38/-1.71 ms); GI total 12.349/10.514/12.386/10.382 ms. RR about +0.2 ms in ON windows.
  Inn INVALID (last window short); complete windows about 12.2 ms ON vs 12.5/13.2 ms OFF, supporting only.
  Offline paired: S8 -1.73 ms, inn -1.36 ms, camp neutral (240 pairs +0.005 [-0.06,+0.06] ms).
  Check S8 Ultra roofed-room far bounce with developer A/B: unchanged image, lower GI trace cost.
  Live results supplied for this offline qualification; no new game or queue run.
- `gi_lane_permute` (SpecId 1032): bijective thread-to-pixel remap of the GI trace kernel, outputs 100% bit-equal
  offline. Live S8 Ultra (opt-perm-s8.1.181043, VALID): GI trace OFF/ON/OFF/ON 8.42/12.86/8.97/12.21 ms, about
  +3.8 ms slower. Rejected; code, key and batch removed (with `gi_ao_r16f`, exact but at most 0.06 ms). Ids 1032-1033 retired.
- Attribution, S8 Ultra (opt-attr-s8.1.173918, VALID): GI trace 8.0 ms -> 2.1 ms with `gi_hit_sun=0` (which also
  removes gi_far) and -> 1.5 ms with `gi_hit_direct=0`. Nearly all the trace cost is the hidden-hit branch with sun
  and far. Paired offline far increments: S8 +2.430 ms, camp +5.460 ms, inn +2.448 ms; sun with far off:
  +0.098 / +0.427 / +0.005 ms. See TRACK_C_GI "Far GI cost split, 2026-11-05" for the next decomposition.
- `gi_sun_wave` (SpecId 1034): deferred dense sun queries with ordered resolve, exact outputs and raw counters
  on S8/camp/inn. Forty paired ABBA/BAAB samples gave ON-OFF +0.735776 ms [95% CI +0.540272, +0.849120],
  +3.114312 ms [+2.451616, +3.897856], +0.737392 ms [+0.638336, +0.863264]. Rejected: all slower.
  Code, raw key, setting, scratch buffers, two requests and feature tests removed; 1034 retired, never reuse.
  The paired replay A/B tool and its tests remain. Manifest requests return to 125; no ABI change.
  Cleanup passes ordinary release equivalence (215/215 byte-identical to main) and the full build plus 191 CTests.
