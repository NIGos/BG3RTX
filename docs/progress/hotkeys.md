## DLSS 5 page and keyboard shortcuts (branch hotkeys, offline verified, 2026-11-05)

- Dedicated DLSS 5 page in WebView and native fallback, visible without experimental opt-in. On/Off, runtime picker, discovery/signature/rejection status and shortcut capture. Experimental wording retained.
- NR remains Off in all four presets and requires Ray Reconstruction. Activation/runtime changes still require restart.
- hotkey_dlss5 defaults to Ctrl+Shift+F8. The live toggle uses the settings override for dlss5_nr_bypass, only when dlss5_nr was enabled at startup; logs [dlss5] hotkey: NR on/off. Unrelated reloads preserve the live bypass; explicit bypass INI changes supersede it.
- hotkey_rt_toggle defaults to Ctrl+Shift+F9. Capture, None, Reset to default, save/discard and conflict validation on DLSS 5 / Advanced. Ctrl+Shift+F10 stays reserved and unchanged, including its existing Alt-modifier behaviour.
- Parser runs once per settings reload. Invalid/conflicting raw INI bindings disable both configurable shortcuts and log a warning.
- Offline gates: build.ps1 -Test PASS 190/190 (189.39 s); final targeted build/tests PASS 8/8, including NR On without opt-in, parser, persistence, bridge and renderer. Defaults generation check and git diff --check pass.
- Shader request delta 0; no ABI or rendering-path change, no new GPU pass/cost. Live input confirmation remains untested (offline-only instruction); no game launch or queue operation.
- Check in game later: enable NR before startup with a compatible runtime; press the DLSS 5 shortcut twice in the same view and inspect the log. RT shortcut still switches all ray tracing off/on independently.
- No git writes; coordinator commits. layer/dlss5.cpp and tools/dlss5_fixture.cpp untouched. external/ junction preserved.
