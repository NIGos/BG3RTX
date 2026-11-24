# BG3RTX 0.9.7-beta: release notes

What changed since 0.9.6-beta. All numbers were measured on one PC with an RTX 5090. They are not a promise for
another card or scene. Everything else of 0.9.6-beta is unchanged: [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md).

## In short

- Sun shadows under roofs, far bounce light and per-light data work again after game Patch 8.
- Play on a fresh install saves the shown preset again.
- All 71 cutout masks load; 17 were rejected before.

## Lighting

- **Sun shadows under roofs, far bounce light and per-light data work again.** Since game Patch 8 BG3RTX no longer
  recognised the level, so these silently did nothing. It now recognises the level from the game's lights, also
  right after a load (Medium, High and Ultra; about 0.2 ms).

## Fixes

- **Play on a fresh install saves the shown preset again.** High and Ultra had run without opacity micromaps and
  cutout masks.
- **Restart notices.** The launcher asks for a restart when Hair shadows or Detailed logging are turned on while
  playing.
- **Cutout masks.** 17 of the 71 cutout masks were rejected at every start; all 71 now load (about 36 MB more video
  memory).

## Known issues

- Tested only on an RTX 5090 with driver 621.01. No other card or driver has been tested.
- There are still no tested video memory recommendations for any card. Start with Low and raise step by step.
- After a fast zoom out in a large interior, ray-tracing memory stays high for about 15 seconds.
- The first game start after installing can spend 15 to 50 seconds longer creating hair shader variants, also with
  RTX Hair off. Later starts reuse them from the driver's cache.
- Act 2 and most of Act 3 are not systematically tested. Sessions of several hours are not tested.
- RTX Hair (beta, off by default): a style that is not converted yet stays as the game's hair, distant hair can look
  softer, a head seen for the first time shows the game's hair for about 0.6 seconds, its cost is not measured
  across scenes yet, and strand edges can still show light dithering in motion. Details:
  [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md#rtx-hair-beta-off-by-default).

Requirements, how to report a problem, licence and source: as in [RELEASE_NOTES_0.9.6.md](RELEASE_NOTES_0.9.6.md).
