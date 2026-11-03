## Hair groom CPU conversion (branch codex/hair-groom-speed, kept, 2026-11-03)

- Vectorised mapped runs and exact segment medians; reused triangle Jacobians/norms and batched UV searches.
  Dedupe uses scalar pair keys and early other-card filtering; neighbour lookup searches occupied cells once.
- Every field and dropped count matches the real-style references bit for bit; max absolute/relative diff 0.
  Shadowheart: 85,667 strands, 529 dropped; Gale beard: 21,608 strands, 2,838 dropped;
  Halsin: 142,002 strands, 6,552 dropped.
- Convert/dedupe seconds, supplied baseline -> measured: Shadowheart 8.4/5.9 -> 3.253/1.431 (3.05x total);
  Gale beard 4.8/4.0 -> 2.824/0.898 (2.36x); Halsin 12.6/12.5 -> 4.475/2.834 (3.43x, target >= 3x).
- Independent final `tests/hair_groom_test.py`: exit 0, `all passed`, 5.06 s;
  `tools/hair_groom/speed_check.py <inputs dir>` exits 0.
  Python ran BelowNormal; `python.exe` substituted for unavailable `py -3`. Baselines were supplied,
  not rerun. No game/GPU tools, commits or Git metadata writes; no defaults, thresholds or format changes.
- Offline check: run the two commands above with the saved real-style fixtures. No in-game check is needed
  for this CPU-only, output-identical optimisation. Shader variant request delta: 0.
