# Nexus Mods page kit

Texts and graphics for the BG3RTX page on Nexus Mods. Nothing here is uploaded or posted
automatically. Facts come from `docs/release/README.md`, `docs/USER_SETTINGS.md` and the
release plan; update the texts when those change.

## Files and where they go

| File | Where on Nexus | Limit |
|---|---|---|
| `title.txt` | Mod name | 44 characters now; keep it short (Nexus truncates long names in lists) |
| `short_description.txt` | Brief overview / summary | 350 characters max; 320 now |
| `description.bbcode` | Description tab, BBCode editor ("Source" / BBCode mode) | Nexus allows a long description; this one is about 13 KB |
| `sticky_post.bbcode` | Posts tab: write it as a comment, then pin it (sticky) and lock replies if you wish | Same BBCode |
| `img/*.png` | Images referenced by both BBCode files | Each under 1 MB, all together about 1.3 MB |
| `make_graphics.py` | Regenerates `img/` and composes screenshot pairs | Python 3 + Pillow |

## Images: upload and replace the URLs

The BBCode uses placeholders like `[img]IMAGE_URL:banner.png[/img]`.

1. Upload each file in `img/` to an image host that allows hot-linking (on Nexus: upload them
   as images on the mod page, open each one and copy its direct image address).
2. Replace each `IMAGE_URL:<file>` with that file's direct URL, so the tag becomes
   `[img]https://.../banner.png[/img]`. Every name is used as is; search and replace works.
3. Do the same for `SCREENSHOT_URL:<file>` once the screenshots exist (below).
4. Preview the page before saving. If a header looks too wide on your screen, that is the
   page width; Nexus scales images down.

| Image | Size | Used in |
|---|---|---|
| `banner.png` | 1300 x 372 (Nexus header size) | Nexus header image; also usable at the top of the description |
| `sticky_banner.png` | 1000 x 190 | sticky post, top |
| `badges.png` | 935 x 34 | description, under the banner |
| `divider.png` | 720 x 42 | both, between blocks and at the end |
| `h_features.png` ... `h_licence.png` | 1000 x 64 | description section headers |
| `h_read_first.png`, `h_help.png`, `h_bugs.png`, `h_known_issues.png` | 1000 x 64 | sticky post section headers |

Also good as the mod's main image (thumbnail): `banner.png`, or `assets/branding/bg3rtx-512.png`.

Graphics use only project artwork: the gem emblem, the charcoal surface, the ornament SVGs and
the lantern painting in `assets/branding/`, the palette of `docs/release/VISUAL_STYLE.md`,
Lato (bundled, OFL) and Palatino Linotype for display titles (Windows system font, used only to
render the PNGs; Georgia is the fallback). To change a header title or add one, edit `HEADERS`
in `make_graphics.py` and run:

```powershell
python docs/release/nexus/make_graphics.py
```

## Screenshot shot list

Take each pair in game with **Ctrl+Shift+F9** (all ray tracing off, then on), same view, camera
still, Ultra preset, UI hidden if you can. Save them as `<name>_off.png` and `<name>_on.png` in
one folder, then join each pair into one framed, labelled image:

```powershell
python docs/release/nexus/make_graphics.py --pairs "D:\Shots" docs/release/nexus/screenshots
```

This writes `<name>_pair.jpg` (1200 px wide, well under 1 MB). Do not commit the screenshots.

| Placeholder | Scene | What to show |
|---|---|---|
| `shot01_camp_dusk_pair.jpg` | Act 1 camp at dusk or night | Campfire and lanterns casting shadows from people and tents; the light falling off into the dark |
| `shot02_water_pair.jpg` | Act 3 port near the Water Queen's House or the Blushing Mermaid | Water reflecting buildings and lights; avoid the port flicker spot that the game itself has |
| `shot03_grymforge_pair.jpg` | Grymforge or the Adamantine Forge | Fire and lava light bouncing onto stone; indirect light in the corners |
| `shot04_foliage_pair.jpg` | Act 1 outdoors in daylight, trees overhead | Sun shadows through leaves on the ground and characters |
| `shot05_interior_pair.jpg` | Inside the Blushing Mermaid | Interior lights with shadows; reflections on floors or tables |
| `shot06_hair_pair.jpg` | A character close-up near a light, e.g. at camp | Shadows and occlusion from the game's own hair on face and shoulders |
| `shot07_launcher.png` | The BG3RTX launcher | The preset row and the Lighting page (no pair; upload as is) |

Only scenes from tested places are listed (Act 1 and a few Act 3 places). Shot 01 and 02 are
shown on the page; the others sit in a spoiler. Also upload the best `_on` shots as gallery
images on the Images tab.

## Before publishing

- Replace `BG3RTX-<version>-Setup.exe` in the installation section with the real file name.
- Check that the launcher labels quoted in the texts still match the current launcher
  ("Report a problem...", "Save report...", "Disable other Vulkan overlays",
  "Show experimental options", "Water surface reflections").
- Check the Bugs tab is enabled on the mod page; both texts send reports there.
