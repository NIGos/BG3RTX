## Release licence audit (branch release-licences, blocked, 2026-11-03)
- Preserved 11 local notice texts verbatim; recorded sources/hashes and protected bytes from Git normalization.
- Added package manifest coverage for 10 third-party DLLs and 6 compiled SDK components; uncovered notices fail before writes.
- Selected DXC LICENSE.TXT and Vulkan-Headers LICENSE.md / LICENSES/{Apache-2.0,MIT}.txt remain missing; old sample NCSA text is reference only.
- Packaging CTests: 3/3 passed (40.13 s); offline beta blocker status in docs/release/RELEASE_OFFLINE_STATUS.md.
- No game, download, player archive, commit or Git metadata mutation; release remains paused.
## Version-matched notices integrated (branch release-licences, kept, 2026-11-03)
- Resolved the missing-text blockers above: five supplied DXC/Vulkan texts retained verbatim, with URLs, revisions and SHA-256; DXC identifies University of Illinois/NCSA plus included third-party terms.
- Real-text enforcement and packaging CTests passed 3/3 (38.98 s); ListOnly validated 17 entries. No downloads, game, player archive or Git metadata writes; broader release decisions remain open.
