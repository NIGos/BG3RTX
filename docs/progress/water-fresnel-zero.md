## Foam-floor predicate (branch water-fresnel-zero, READY_FOR_LIVE, 2026-11-03)
- wsf1 pool c4e92ccd provisioned; ON selects 120 draws/window, missing 0; clean owned 0, invalid 0. Sea 7b44a567 stays native; 99c1fcef occurs before save load.
- Final foam SSA 751 floors abs(signal) to 1e-4 before pow (~3.19e-8 at template values): exact final zero excludes absent foam. Test exact pre-floor zero independently for colour/F0/roughness; retain final-zero fallback and native inputs, no epsilon.
- Layer/test build and 4/4 water CTests pass, including seven FS forms validated and emitted-predicate cases for zero, tiny real foam, multiplier zero and key OFF. ABI/stride/request delta 0; no compose shader edit.
- Predict both ON windows clean owned > 0, invalid 0; fully clean pool ~20% of water records (~56k/frame, old 2.05% view / live ~10.1% records). Actual runtime masks/share are unmeasured; nonzero flow foam stays native.
- Off by default; restart layer, inspect S8 pool rim using same-run wsf1 A/B. No game launched, queue submitted or commit made; GPU cost and live participation remain open.
