# BG3RTX 0.9.6-beta: release notes

What changed since 0.9.5-beta. All numbers were measured on one PC with an RTX 5090. They are not a promise for
another card or scene. Sources for every number: [RELEASE_EVIDENCE_0.9.6.md](RELEASE_EVIDENCE_0.9.6.md).

## In short

- Less video memory: about 0.8 to 1.0 GB less in the two test scenes, same image.
- A new launcher window: a setting applies the moment you click it. There is no Save button.
- RTX Hair (beta) now works when you turn it on, and holds its strands while the camera moves. It stays off by default.
- A new option, Low-latency scene, is on in High and Ultra.
- Faster game start from the second launch: the hair shader variants now come from the driver's cache.
- Opacity micromaps work again in High and Ultra, and a one-frame water reflection flicker is fixed.
- Smoother RTX Hair in crowded rooms: loading a hair style no longer freezes the frame.
- A clearer launcher: plainer restart badges, a tidier detail panel and one name, Hotkeys, for the in-game keys.

## Performance and memory

- **Less video memory for ray tracing.** The buffers that hold the ray-traced scene are now sized by what the scene
  needs, and shrink again when the need falls. Measured on Ultra with RTX Hair on and Frame Generation 3x: 16.4 GB
  to 15.6 GB used at Water Queen's House, and 18.6 to 18.9 GB down to 17.7 GB at the inn. No object was left out of
  ray tracing, and the brightness is the same. There is no option for this: it is always on.
  When a buffer shrinks you may notice one short pause (36 to 56 ms measured). It is rare.
- **Fewer large reallocations while a scene grows.** A growing buffer now grows in bigger steps. In a recorded
  sequence, ten reallocations of 1.4 to 1.8 GB became three.
- **Low-latency scene** (Performance page, on in High and Ultra). The lighting no longer waits for the ray-traced
  scene of the same frame. The wait fell from 3.5 to 0.3 ms at Water Queen's House and from 8.5 to 2.9 ms at the inn.
  This saves about 0.5 ms per frame on High at Water Queen's House. On Ultra the frame time did not change in both
  scenes, because the graphics card is the limit there. So expect 0 to 0.5 ms. The image and its brightness are the
  same. Turning it on needs a game restart; after that restart the lighting shaders rebuild once.
- **Less background work while you play.** BG3RTX checked its shader files four times per second. It now checks
  every 5 seconds.
- **Faster game start from the second launch.** At every start BG3RTX makes 519 hair shader variants, also with
  RTX Hair off. Their memory address now stays the same between starts, so the driver reuses them from its cache:
  15.4 s became 0.27 s on the second start of the test. The first start after installing still pays the full time.
- **Less work when the ray-traced scene is rebuilt.** Snapshots of objects that do not move are stored in a smaller
  format: at Water Queen's House 91 MB became 36 MB copied per rebuild, the rebuild took 0.81 instead of 1.04 ms of
  GPU time, and 53 MB less mesh memory was used. Same brightness. Always on.
- **Better use of ray-tracing memory near its limit.** Small structures no longer settle in the large blocks made for
  sun-shadow casters and hair, which pinned those blocks (one session log showed 4.0 GB reserved for 1.27 GB in use).
  In a recorded sequence, refused allocations fell from 53 to 2. When an allocation is still refused, the objects
  that cast sun shadows keep their last shape for that frame instead of losing their ray-traced shadows. The test PC
  never reached the limit, so the gain was not seen there; costs and brightness were unchanged. Always on.
- **RTX Hair appears sooner after loading.** Strands showed about 310 frames after a level load. Now they show
  after about 100 frames (96 to 116 in the latest test runs).

## Stability

- **Sun shadows under roofs, far bounce light and per-light data work again.** Since game Patch 8 BG3RTX no longer
  recognised the level, so these silently did nothing. It now recognises the level from the game's lights, also
  right after a load (Medium, High and Ultra; about 0.2 ms).
- **Launcher and data fixes.** Play on a fresh install saves the shown preset again (High and Ultra had run without
  opacity micromaps and cutout masks). The launcher asks for a restart when Hair shadows or Detailed logging are
  turned on while playing. 17 of the 71 cutout masks were rejected at every start; all 71 now load (about 36 MB more
  video memory).
- **Video memory guard.** When Windows lowered the video memory budget of the game once, BG3RTX could stay in
  its low-memory mode for the rest of the session: objects were left out of ray tracing and the game stuttered.
  The guard now recovers when the budget comes back. This fix is not yet confirmed in a long play session.
- **A hair style that does not fit in memory no longer stops ray tracing.** Before, one such style could stop
  the whole ray-traced scene from being rebuilt. Now only that style is left out for that frame.
- **Frame Generation.** A stale record of a destroyed Frame Generation input is now removed.
- **Opacity micromaps work again with Low-latency scene.** In the 0.9.6 test builds, opacity micromaps (High and
  Ultra; they speed up foliage) were silently off whenever Low-latency scene was on. 0.9.5-beta was not affected.
  Measured at Water Queen's House: about 0.33 ms less indirect lighting on Ultra, about 0.3 to 0.4 ms more on High,
  same image. They stay on in both presets.
- **Water reflection flicker fixed.** With Low-latency scene on, water reflections and shadows could switch off for
  one frame (in 7 to 24% of the 60-frame test periods at Water Queen's House). After the fix, 28 of 29 test periods
  had no such frame.
- **Fewer pauses when RTX Hair loads a hair style.** When memory for hair was short, a style could be read and
  unpacked up to 9 times (about 1 second in one player log, with pauses over 100 ms). It is now unpacked once.
  This is fixed offline; it is not yet confirmed in play.

## RTX Hair (beta, off by default)

RTX Hair draws hair as individual ray-traced strands instead of the game's flat hair cards. It is **off in every
preset**. The game's own hair is what you see unless you turn it on.

How to turn it on:

- In the launcher: **Lighting > RTX Hair** (marked Beta).
- In game: **Ctrl+Shift+F7** (you can change the key on the Hotkeys page) switches it off and back on while you
  play, after it was turned On in the launcher. It cannot turn RTX Hair on while it is Off in the launcher.
- A hair style gets strands after the game has shown it once and the launcher has converted it. Home shows the
  progress under Preparation. "Convert all styles" is optional and needs a `granny2.dll` that you provide;
  BG3RTX does not ship or download that file.

New in this version:

- **Fixed: RTX Hair did nothing in 0.9.5-beta.** The option could be turned on but no strands were drawn. It works now.
- **Smoother in crowded rooms.** A hair style is now read and unpacked in the background instead of inside the frame.
  At the inn, the slowest frames while the camera pulled back fell from 98 to 111 ms to 40 ms (p99). The price: a head
  whose hair style is loaded for the first time shows the game's hair for about 0.6 seconds first.
- **Strands stay while the camera moves.** In the test scene, fast zooms and orbits no longer switch hair back to
  the game's cards: 0 switches in every test window. In that run 97 to 99% of the hair was drawn as strands.
- **RTX Hair detail at distance** (Full, 1/4, 1/8; off by default; applies while you play). On small, distant heads
  it traces fewer, wider strands. In one scene with 13 distant characters it saved about 0.08 ms (1/4) or 0.10 ms
  (1/8) and used about 75 to 100 MB more video memory.
- **RTX Hair motion stability** (on in High and Ultra; only acts when RTX Hair is on). Each strand follows the
  motion of the hair it belongs to, so hair no longer smears or trails behind moving heads. Turning it on needs a
  game restart.
- If you had RTX Hair on in an older version, the new settings it needs are added by themselves.

Known limits of RTX Hair:

- A hair style that has not been converted stays as the game's hair. Some styles in the test scene are not
  converted yet.
- Hair on distant characters can look softer than the game's hair: there a strand is much thinner than a pixel,
  and the image shows the average of many strands.
- A head that comes into view shows the game's hair for a few frames (about 3) before its strands appear. A head
  whose hair style is loaded for the first time shows it for about 0.6 seconds (18 to 25 frames in the test).
- The cost of RTX Hair depends on how much hair is on screen. It is not measured across scenes yet. The strand
  pass itself measured 0.13 to 0.35 ms in the test scene, but one player session with RTX Hair on reported a large
  frame-rate loss that is not explained yet.
- Strand edges can still show light dithering in motion.

## Launcher

- **New window layout.** Pages in a sidebar with Play and the version, the preset in the header, settings in
  groups, and a detail panel that says what a setting does, what it costs and when it applies.
- **A setting applies when you click it.** There is no Save button. A strip at the bottom names the change and
  anything that changed with it, and offers Undo. A setting that needs a game restart carries an amber badge:
  "Restart to apply", "Restart to turn on" (or off), "Restart for" one named choice, or "Restart for some choices".
- **Clearer pages.** Rows with long choices (Water lighting, Sun shadows, Indirect lighting) put the choices under the
  text. The detail panel uses paragraphs and lists the four presets on their own lines, the current one in bold.
  The in-game keys have one name everywhere: Hotkeys.
- If Microsoft WebView2 is not installed, the launcher opens its basic window instead. That window keeps its Save button.
- All pages are reachable in a narrow window (640 px wide).
- **Rendered frame limit** starts at 58 fps when you turn it on (it started at 30).
- The window shows the version of the installed package. It showed 0.9.5.0 in every build before.
- The hair converter runs at idle priority while the game runs. Play stops shader preparation and the converter at once.
- **Create a diagnostic report** is on Home. The report can include your latest save if you tick it (off by default).

## Fixes

- RTX Hair on did nothing in 0.9.5-beta (see above).
- A report was marked "Partial" only because the settings files were older than a day.
- A style captured while the converter was running waited for the next capture before it was converted.
- A game folder path that ended in a backslash broke the hair converter.
- Dragging a slider was interrupted by the launcher's refresh.

## Known issues

- Tested only on an RTX 5090 with driver 621.01. No other card or driver has been tested.
- Video memory after a fast zoom out in a large interior: ray-tracing structures held 2.3 to 2.7 GB for 0.65 to
  0.8 GB in use at the inn. It falls back to about 0.9 to 1.0 GB after the camera rests for about 15 seconds.
- There are still no tested video memory recommendations for any card. Start with Low and raise step by step.
- The first game start after installing can spend 15 to 50 seconds longer creating hair shader variants, also with
  RTX Hair off (17 and 20 seconds in two player logs). Later starts reuse them from the driver's cache (see above).
- The new launcher window is new in this version. If it misbehaves, please report it.
- Act 2 and most of Act 3 are not systematically tested. Sessions of several hours are not tested.
- Everything listed under "Known limits of RTX Hair".

## Requirements

- Windows 10 or 11, 64-bit. An NVIDIA GeForce RTX card. **Tested only on an RTX 5090.**
- An NVIDIA Game Ready driver. The test driver is 621.01. This is a reference, not a minimum.
- Baldur's Gate 3 with the **Vulkan** renderer, and **DLSS** selected as the upscaler in the game's Graphics options.
- Frame Generation 2x needs RTX 40 or newer; 3x and 4x need RTX 50.
- DLSS 5 Neural Rendering is experimental and off by default. Its runtime file is **not included**: you provide it.
- Microsoft WebView2 is optional. Without it the launcher uses its basic window.

## How to report a problem

1. Turn on **Detailed logging** in the launcher (Help & support), restart the game and reproduce the problem.
2. Right after the problem, use **Create a report** in the launcher (Home, or Help & support), or run
   **Diagnostics.cmd** in the BG3RTX folder. It writes one ZIP file on your PC.
3. **Nothing is uploaded automatically.** Open `CONTENTS.txt` in the ZIP and check it before you share it.
   No save is included unless you tick it.
4. Attach the ZIP on the mod page's Bugs tab. Add your card and its memory, the driver version, the preset,
   the game version and any other graphics mods. Say where it happened and what you saw.

Press **Ctrl+Shift+F9** in game to switch all ray tracing off and on. It shows whether a problem comes from BG3RTX.

## Licence and source

BG3RTX is free software under GPL-3.0-or-later with an additional permission for the NVIDIA SDK libraries it ships.
Source code: https://github.com/NIGos/BG3RTX. Downloads are on the Files tab of the BG3RTX page on Nexus Mods only.
