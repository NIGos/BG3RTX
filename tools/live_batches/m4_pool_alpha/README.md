# Alpha pool rows at S6 (live_batch m4-pool-alpha), NOT authorized

One lean job, `hpa1-pool-alpha-s6-fg2` (Adamantine Forge, 144 s, FG 2, foreground): Ultra as merged (+ Bounce,
Hidden hits) with `rt_hit_material_pool_alpha` OFF / ON / OFF (live), exposure held in every A/B window: diag
windows (`rt_stats`), 20 s cost windows, three PNGs. Why: the S6 replay pack 021627 ranks "pool alpha, no row" at
39.0% of hit lookups; the pair of those meshes (Base_AlphaTest_VT STI, 2d2ac3c3/14678345) is the audited Base_VT
recipe, and live m4p3 012015 shows 66 qualified records of it but only 21 with a UV/basis (alpha meshes got none).
Gates: `[hit material pool basis] ... instances with a pool row` > 0 in diag-on; capacity 0, scene wait 0 timeouts
in every cost window. Deciding: the `hit material` qualified share (predicted +23 points, range 10-39) and the energy
of shot-on vs shot-off-a/b (predicted +0.5%, predicted.json). Undo: `rt_hit_material_pool_alpha=0` (live).
Offline check: `powershell -File tools\live_batches\m4_pool_alpha\dryrun.ps1`.
