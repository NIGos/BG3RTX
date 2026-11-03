# Water surface candidate protocols (M5.6)

The new `wsg1-s8-sun-glint` protocol is **BLOCKED_OFFLINE_REFERENCE**. The proposed
footprint filter failed the dependent native-BRDF reference; no production glint
setting or specialization was installed. This protocol is preparation for a future
passing implementation. Do not submit it to the queue.

Regenerate its artifacts with `python -m tools.live_batches.water_surface_fresnel.make_job`.
It uses the existing wsf1 contracts, with Fresnel fixed ON and glint 0/1/0/1 static,
then two OFF/two ON K8 raw bursts for each held E/Q, then capture-free cost windows.
The shared corpus model derives 12.9 s static and 9.9 s cost windows at 27.0758 FPS:
one second settings latency plus two 120-frame intervals, with three additional
one-second trace stalls for static windows. Total 163.4 s, plus load/readiness waits;
12 float shots and eight raw water bursts use about 1.8 GB. Restore releases exposure
and captures and resets both surface keys. No watched queue files are written.

`tools/water_glint_gate.py` requires every ON static shot to preserve linear mean
within the same-run OFF-noise Student t bound. It computes water residuals *within*
each raw burst, on exact all-frame record coverage; every ON residual must beat
same-direction OFF repeats beyond their spread. This is a smooth-turn proxy, with
no pixel subtraction or physical correspondence between different pans. Reject
changed framing. The gate cannot replace the failed offline native-BRDF reference.
Compose timing excludes the added forward FS work; report that measurement limit.

## Water coverage / interface / column split (step 2)

Prepared only. Lives paused; nothing submitted to a watched inbox.

One real-Ultra S8 Water Queens House launch, visible_unfocused_fg. Provision the default-off
key at startup; toggle 0/1/0/1 within the same session. Four static 11.9 s pool-rim windows
retain two post-water float shots each, followed by four 12 s held E/Q cost windows.
127.8 s scheduled plus load/readiness waits, about 490 MB. Static minimum >11.864 s derives
from the observed 27.0758 FPS corpus, one discarded straddling 120-frame interval,
one complete interval, 1 s settings latency and two 1 s trace stalls. Shared tightening
adds a producer-sanity phase and trims restore to 1.1 s.

Every ON window reads the coverage-weighted ownership, invalid and energy-partition line;
zero/partial/full histogram and source/spec counters remain available. The post gate reads
all eight full-resolution RR colour float means and lighting exposure, rejecting missing or
nonfinite evidence. Brightness may change; image quality needs review against predicted.json.
Native foam retains coverage c; only the (1-c) clean contribution receives RT replacements.
Actual FS plus compose cost is unmeasured: ON evaluates native and clean shading separately.
Compose GPU timing excludes the extra forward FS evaluation; report its measurement limit.
Coverage histogram bins use ceil(255c); bin 255 includes near-full coverage. Radiance uses float c. Last phase restores key 0 and releases exposure hold.

Offline check: `python tools/live_batches/live_flow.py tools/live_batches/water_surface_fresnel`.
The legacy `dryrun.ps1` wrapper predates this mixed-protocol directory. It requires one
`live_batch`, disallows prepared raw bursts and budgets the entire directory as one
five-minute batch; it cannot validate the added glint protocol. No fake-queue dryrun
pass is claimed. The passing shared `live_flow` lint checks both prepared jobs.
Material proof: [evidence](../../../docs/evidence/water-foam-coverage.md).
