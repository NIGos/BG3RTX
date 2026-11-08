## Launcher UX: Home dashboard, RTX Hair conversion panel, Hotkeys page (branch claude/launcher-ux, offline, 2026-11-09)
- Navigation: Home, Lighting, Surfaces, Performance, DLSS 5, Hotkeys, [Experimental], [Advanced: developer only],
  Help & support. Release Advanced held only the RT hotkey, so it is hidden (contract already said so) and the key
  moved to Hotkeys. DLSS 5 has its own icon.
- Home is a dashboard: Play card (preset, Play, version and GPU, and a Status list: game folder, GPU, mod files,
  unsaved changes with Save, restart pending); RTX Hair panel; Shaders; In-game keys. Removed the three performance
  shortcuts (SER and the RT pipeline are on in every preset; Frame Generation is the first Performance card).
- RTX Hair panel: the Lighting control and its in-game key (when the bridge has `hotkey_rtx_hair`), tiles Ready / Seen in
  game / In the game / On disk, progress with percent and ETA, the last error, Start / Pause / Resume / Try again,
  Convert: Styles seen in game / All styles (disabled with a one-line reason until granny2.dll and the game folder are
  set), granny2.dll picker with where players get it (their own LSLib ExportTool download, not shipped), When to convert.
- Launcher side of agent C's interface: `bg3rtx_hairconv --all --game <dir> --granny <dll>`; preparation.json `total`,
  `source` (or `mode`) and `error` are optional; launcher key `hair_granny`; prep actions start, convertAll,
  browseGranny, clearGranny. Unit tests in launcher_preparation_test (command lines, reasons, tolerant facts, JSON).
- Hotkeys: the bridge and launch code iterate `bg3::hotkeys::ids` (State shortcuts are std::array sized from it), send
  `shortcutDefaults`, and refuse a key used by any other binding. Agent A's third id appears without launcher changes.
- Badges: Applies now / Applies after saving / Restart to apply ("May briefly pause" folded into the title and Details).
- Help & support: no Preparation copy; compatibility and video memory merged into Graphics details.
- User text: "RTX Hair" everywhere (preparation title, `exp_hair_groom` label "RTX Hair in ray-traced lighting" shown by
  raise_needs; schema regenerated). Left for agent A: the RTX Hair knob description still says "(see Preparation)".
- Check: open BG3RTX.exe; Home shows Play with status, RTX Hair conversion, Shaders and keys; Hotkeys lists the keys.
