## RF compact probe state (branch rf-on, qualified, 2026-11-06)
- `rf_compact_probe_state` / SpecId 1031 always on: default, presets and release policy 1; `dev=1` keeps raw 0.
- Core fallback and INI are 1; no GUI knob. Settings tests cover the policy.
- S8 default Ultra `opt-compact-rf-s8.1.102848`: VALID SAVING.
- Reflections OFF/ON/OFF/ON 1.200/1.034/1.224/1.066 ms; OFF spread 0.023 ms.
- Float energy within the OFF band on all channels; luminance -0.00025/-0.00018, band 0.0247.
- Inn live INVALID (readiness); offline paired -0.123/-0.129 ms. Registers 128 -> 96.
- Replay differs only in 63/9 projection-unstable pixels also differing OFF/OFF; no strict equality claim.
- Check S8 default Ultra reflections; developer raw-key A/B. Shader request delta 0; ABI unchanged.
- Defaults and schema regenerated; scripts/build.ps1 -Test PASS, 203/203 CTests (195.01 s).
