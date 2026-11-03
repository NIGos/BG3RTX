## A2 launcher UX (branch ux-a2-launcher, offline candidate, 2026-11-02)

- Replaced the flat window with Overview / Graphics / Performance / Advanced / Support tabs.
- Preset and actions stay pinned; cards use A1 copy, choice maturity, apply and cost provenance.
- Experimental starts closed. Developer is absent without explicit dev=1; hidden legacy values
  have a neutral custom indicator and survive unrelated saves and preset changes.
- Added preset/dependency previews, Save / Discard / Cancel on Close, full error details,
  resumable first-run pages and durable restart state keyed to PID plus process creation time.
- Uses B1 emblem, private Lato fonts, charcoal/parchment colours and system high-contrast text.
- B2 hook only: launcher::diagnostics::show_report(HWND), supplied through diagnostics.h.
  The fallback states that the collector is unavailable; nothing is collected or uploaded.
- C2 handoff: register B2 implementation and ui_state_test.cpp; embed B1 resources separately.
- Validation: launcher C++ target built; rebuilt settings_test CTest passed (1/1).
  CPU-only UI state fixture: 15 checks, 0 failed. Mixed-mode launch/write guards: 3/3 passed.
- tools/launcher/ui_review.py produces 15 PNGs and verifies configuration hashes unchanged.
  Evidence: captures/gui-product-review/ux-a2-launcher/ (normal, Experimental, missing game,
  failed probe, long error, first run, Support, collapsed Advanced and high-contrast fallback).
  Simulated scales: 100/125/150/200%; narrow reflow: 720 logical pixels; scrolled footer stays pinned.
- Remaining release QA: keyboard-only / Narrator, real system high contrast and per-monitor DPI.
  The screenshots simulate scaling and system-colour fallback; they do not certify those checks.
- PRODUCT_REVIEW.md was absent locally; read sections 5-9 from codex-product-review via git show.
- No game, installer, DXC/shader tools or saves used. Shader request delta 0; no renderer cost change.
- Git metadata left untouched; no commit or merge. All changes remain for coordinator review.
