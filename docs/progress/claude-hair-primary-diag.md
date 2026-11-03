## Strand hair diagnostics (branch claude/hair-primary-diag, 2026-11-03)
- hv5 (main a8382760) was valid but its primary stage drew no strand: after the stage-d reload every draw struck
  ("its strands drew no pixel in 2 frames"), 0 G-buffer strand draws in every 300-frame line. Cause unknown.
- Now: the 300-frame line "[hair primary] counted frame F: strand pixels per groom (LOD, draw key prefix): ...;
  pool-bound refusals N (session M)"; each strike names its groom k, LOD and every groom's pixels of the counted
  frame; the count pass counts the strand pixels whose segment fails the pool bound (hp_segment), so it is no
  longer silent. hv5's primary survival windows (25 s) and hv4's after-reload window now need strand draws > 0.
  Identity: only rt/hair_primary_count.hlsl differs (key-on). live_flow 5 jobs 0 failing; the hair_validation
  dryrun fails on every job (also unchanged hv1-hv3: the lean_dryrun rules on main are stricter than these jobs).
