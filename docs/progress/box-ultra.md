## Fixed box shadows (branch box-ultra, KEPT, 2026-11-03)
- User approved valid ba1 S6 Grymforge 08:02: Ultra ls_box=2; ls_box_honour_shadow stays 1. Lower presets/base stay Off.
- Fixed vs OFF: 8.6% pixels > 8 levels vs OFF/OFF 8.5%; mean signed difference 0.0, energy within noise.
- Unshadowed traces 0; old mode 369M per 120 frames and -5.27% lit luminance.
- S6 local-light pass 0.45 ms off -> 0.58 ms fixed (+0.13 ms, RTX 5090).
- Qualified option/cost, schema, release review, TRACK 9.10 and smoke_ultra key list updated; shader request delta 0, ABI unchanged.
- Build passed; all 131 CTests passed across full run and corrected launcher/settings rerun (4/4); defaults generator/check and git diff --check passed.
- Check S6 Adamantine Forge lower-right walkway and central pillar with Box light shadows = All: art lights remain visible. No game launch, queue submission or git metadata changes.
