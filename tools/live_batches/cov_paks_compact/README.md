# Compact catalogue: S6 publication qualification

`cp5-compact-s6-publication` uses Adamantine Forge (S6), the compatible
Tileset/HLOD-mask cohort. Camp cp4 and its audit are retained as historical
zero-publication evidence; they cannot qualify publication. `make_jobs.py` keeps
that original fixture; regenerate the new job with
`python -m tools.live_batches.cov_paks_compact.make_s6`.

One unfocused FG 2 launch: warm both states; static OFF-a / ON / OFF-b /
ON-repeat / OFF-c captures at one held exposure; separate diagnostics before
each ON capture; separate diagnostics-free 300-frame cost windows. Both ON
observations require positive UV attempts, exact witness, published records,
exact match and verified ready basis. Recipe/cull, COLOR/resource and full raw
witness checks remain the existing publication policy in layer/hit_material.cpp;
UV candidates do not bypass any prerequisite. Zero publication rejects this
scene/candidate rather than silently passing. Missing producers are INVALID.

Read `predicted.json` for the conservative energy and image decision rules.
Run `python -m tools.m4_promotion_gate <log> --batch cov_paks_compact --pool run`;
require all five pre-post float captures and inspect fixed-exposure material
crops. A real mean shading change needs a PT explanation before promotion.
Report trace/GI/enclosing RT ms for each ON versus its neighbouring OFF costs.
Publication alone never authorizes release inclusion.

Offline validation: `powershell -NoProfile -ExecutionPolicy Bypass -File
 tools/live_batches/cov_paks_compact/dryrun.ps1` selects cp5 only. Shared
live_flow preflight passes with 78 readable VALID logs. The plan has 15 phases,
203.3 s after load; load allowance 120 s gives **5.4 minutes expected**. There
is no same-save transition-readiness timing witness, so that estimate excludes
unproven waits. Finite readiness allowances total 480 s; load + plan + all
allowances is **13.4 minutes**, global timeout 850 s, below the 15-minute cap.
Five float/PNG traces plus logs estimate 310 MB. No game was launched; no
watched pending inbox was written. Historical gate replay remains unproven
because no S6 publication run exists yet.

Final fake negative/positive queue dryrun passed all checks at 11:34; compact
generator regression passed 1/1. Offline receipts are in `s6-lint.json`.
