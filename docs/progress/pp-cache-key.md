## Preprocessed-text shader cache key + derived prune cap (branch pp-cache-key, kept, 2026-11-02)
- tools/shader_identity.py: second key level in compile_many (compile_shaders.py, merge-green, every
  *_shader_test.py gate): an entry whose include-closure key misses is looked up by its DXC -P text
  (#line, CRLF, blank lines dropped) + DXC id + args + entry; a hit reuses the SPIR-V and links the closure key.
  compile_shaders.py backfills old entries once (34 s). BG3RTX_SHADER_PP=0 turns it off.
- Self-check: `--pp-verify N` (shader_identity.py, compile_shaders.py) compiles N random requests from scratch
  and fails on a difference; merge-green runs N = 4 (+4 compiles per merge).
- Measured, comment-only edit of shaders/rt/rt_common.hlsli, merge-green's shader step: before 2892 entries to
  compile (40-50 min idle; 120 took 810 s today under load, ~5 h extrapolated); after 0 compiles, 35 s
  (2892 by preprocessed text); --pp-verify 4: 4/4 identical. Earlier samples: 52/52 agree (study branch).
- Prune: cap = max(cap, 1.25 x protected SPIR-V) (2.47 GB protected -> 3.0 GB; the fixed 2 GB cap was always
  exceeded); rows sharing a protected blob are kept.
- shader_identity_test 47 checks (fake preprocessor: comment edit compiles nothing, code edit its dependents,
  self-check catches a blind key, failed preprocess falls back, backfill, derived cap). Build + 100 CTests pass.
- TODO: the DirectXShaderCompiler NCSA licence text is nowhere on disk or in the repo; external/licences still
  lacks DirectXShaderCompiler-LICENSE.txt (package.ps1 warns). Fetch it from the DXC repository by hand.
