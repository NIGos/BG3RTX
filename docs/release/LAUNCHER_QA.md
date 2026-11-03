# Launcher QA — 2026-11-03

- PASS: offline QA on launcher-qa; no game launched, no settings saved, Git metadata unchanged.
- PASS: visible categories have navigation pages; Experimental uses native details/summary.
- FIXED: Developer navigation and content require the bridge's dev=1 authorization.
- FIXED: reviewed presets replace visible custom values; hidden developer controls stay intact.
- PASS: all four presets match the central table on supported hardware; capability clamps remain.
- FIXED: native fallback counts custom values when reporting a custom preset.
- FIXED: card Apply badges use the selected choice, including mixed live/restart controls.
- FIXED: every choice shows all prerequisites as text, including satisfied prerequisites.
- PASS: save requires dependency review and confirmation; bridge tests check enforcement.
- FIXED: keyboard focus survives bridge redraws; toggles accept Enter and native Space.
- PASS: buttons, sliders and summaries retain native keyboard semantics; focus styles include select.
- PASS: navigation icons are decorative beside text; toggles and groups have accessible names.
- FIXED: cost appears in every card description; missing measurements say Not measured.
- PASS: copy is factual; no poetic tagline in the shipped page.
- FIXED: ornate theme no longer overrides forced-color hero width/background or focus colors.
- FIXED: disabled controls preserve text opacity; forced colors use system disabled/focus colors.
- PASS: reduced-motion removes transitions; text panels have flat backgrounds.
- PASS: CSS token text contrast minimum 6.68:1; Play text 5.52:1 (WCAG sRGB calculation).
- FIXED: controls wrap; short viewports scroll the full workbench to keep actions reachable.
- PASS: DPI rule fallback checks no fixed pixel widths on text containers and wrapping/scroll rules.
- OPEN: actual WebView2 100/150/200% zoom, Windows high contrast and Narrator acceptance.
- PASS: shipped JS runs against a minimal offline DOM; no browser dependency was available.
- PASS: bridge regression suite 695 checks; renderer, contrast and reflow contract registered in CTest.
- OPEN: PRODUCT_REVIEW.md is absent from this checkout and local refs; RELEASE_BETA,
  VISUAL_STYLE and USER_TRIAL_20261103 were read. No external document was substituted.
- Validation: full build and all 134 CTests; git diff --check (final results in progress fragment).
- Scope: launcher only; ABI and shader request delta 0; no renderer GPU cost change.
