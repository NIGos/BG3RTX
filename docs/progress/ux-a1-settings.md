## Settings presentation and policy (branch ux-a1-settings, offline verified, 2026-11-02)
- Added categories, short player descriptions, full water labels and cost/provenance metadata.
- Water and far-field costs are labelled Estimate with GPU, scenes, output class, date,
  ABI context, level and incremental/total scope. Other costs remain Unknown.
- Choice maturity separates qualified, experimental, awaiting qualification and developer.
  Hidden-surface reflections needs Experimental; box shadows still await qualification.
- Fast shadows requires RR; frame generation requires Reflex. Full shadows does not need RR.
  Capability-clamped presets now settle dependent choices. Custom dependencies require review.
- A2 contract: use Knob.category/desc/cost, choice_maturity, choice_visible and choice_available.
  Visibility never depends on the stored selection. Experimental gates availability, not value storage.
- Use presentation_values(effective, dev) for editing/saving: hidden developer/custom values
  become -1, so render_user_ini preserves their existing keys. Display a neutral custom indicator.
  Explicit reset uses preset_values; do not reset hidden values during an unrelated Save.
- preview_dependencies is pure: values is the candidate, changed lists every adjusted card,
  required_keys lists startup adjustments, blocked lists unresolved/unsupported requirements.
  false previews lowering dependents; true previews enabling required settings transitively.
  Show all adjustments, including required_keys, before applying; blocked is not approval to save.
- Exported schema matches the model byte for byte (settings_test).
- Validation: settings_test and bg3rtx_launcher C++ targets built; CTest settings_test passed;
  2,146 assertions, zero failures. Fixtures include legacy All hits, migration/custom retention,
  all clamped presets, Fast/RR, FG/Reflex, transitive and raw startup previews.
- Baseline schema comparison: rendering values, presets, capabilities, migration markers,
  startup requirements and existing maturity flags unchanged. Shader request delta 0.
- No game, launcher UI, installer, saves, DXC or shader tools run. No Git writes or commit.
- A2 still must consume these helpers; existing launcher developer-choice leakage is not
  fixed by model changes alone. No rendering promotion, new pass or GPU cost change.
