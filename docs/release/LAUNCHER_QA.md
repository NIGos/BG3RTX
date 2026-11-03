# Launcher QA — 2026-11-03

- PASS: offline QA on gui-review-fixes; no game launched, no player settings saved, Git metadata unchanged.
- PASS: visible categories have navigation pages; Experimental uses native details/summary.
- FIXED: Developer navigation and content require the bridge's dev=1 authorization.
- FIXED: reviewed presets replace visible custom values; hidden developer controls stay intact.
- PASS: all four presets match the central table on supported hardware; capability clamps remain.
- FIXED: native fallback counts custom values when reporting a custom preset.
- FIXED: card Apply badges use the saved-to-selected transition, including frame-generation Off/on crossings.
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
- PASS: bridge regression suite 904 checks; settings suite 4,356 checks; renderer, contrast and reflow contract registered in CTest.
- OPEN: PRODUCT_REVIEW.md is absent from this checkout and local refs; RELEASE_BETA,
  VISUAL_STYLE and USER_TRIAL_20261103 were read. No external document was substituted.
- Validation: full build and all 146 CTests passed (194.04 s); git diff --check passed.
- Scope: launcher and strand-primary startup safety gate; ABI and shader request delta 0; no new GPU effect or cost claim.

## GUI review fixes (gui-review-fixes)

- Strand hair on screen is experimental and requires restart; the running process retains its startup setting, so saving On cannot activate it live. This does not qualify the pending device-loss fix.
- Strand activation checks ray queries, linear swept spheres, local groom availability and, for on-screen strands, shaderInt64 and fragment stores. Missing capabilities have factual reasons; required startup settings are reviewed before save.
- Fresnel, temporal depth of field/bloom and crowd budgets live in Advanced / Experimental, off in every preset. Awaiting-qualification choices require Experimental authorization.
- Every water optimization requires hidden-surface water shading. Combined writes bit value 7. Crowd choices state segment budgets; character counts are estimates in Details.
- Card and diagnostic disclosures retain open state across refreshes. Unchanged report previews keep their DOM; refreshed previews retain keyboard focus inside the dialog.
- Slider Off/On buttons expose pressed state. Off disables the bounded range; value text includes units. Frame-generation badges follow the saved-to-selected Off/on transition.
- Cost metadata supplies strand reference timings; other cards show numeric ms or Not measured. Reviewed card copy is one visible-effect sentence, with technical evidence and scene instructions in Details.
- OPEN manual acceptance: real WebView2 zoom at 100/150/200%, Windows high contrast, Narrator, and multi-monitor DPI. Offline DOM tests do not certify these.
- No game launch or Git metadata writes. Final build and CTest results are recorded in docs/progress/gui-review-fixes.md.
