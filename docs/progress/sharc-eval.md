## SHaRC evaluation (branch sharc-eval requested, KEEP EXPERIMENTAL OFF, 2026-11-03)
- No PT superiority established: retain sharc=0; no promotion or removal.
- Five packs (camp, S8, inn, two S6) have one frame each, no set 4 cache state/history.
- Replay writer rejects SHaRC; repeat dispatches cannot measure cache temporal variance.
- Native/PT far baseline only: camp bias -15.61%, RMSE 0.337235, energy 0.843932;
  inn -21.95%, 0.100429, 0.780487. These are not SHaRC ON/OFF results.
- Retained S8 PT lacks roofs; S6 PT outputs and regional masks absent. ON metrics unavailable.
- Historical S8 cost re-read: trace +0.494 ms, reflections +0.168 ms; whole GI +0.810 ms.
- Recorded -0.86% energy vs +1.5% prediction cannot be re-qualified: dumps missing, gate INCOMPLETE.
- PT self-test 11/11, validation errors 0. See docs/SHARC_EVAL.md and captures/sharc-eval/.
- No game, queue or git writes; supplied pak-textures checkout and existing gi.cpp change preserved.
- No shader/ABI/settings change; variant delta 0; no promotion job. Cache history is the blocker.
