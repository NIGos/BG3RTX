"""Builds the two static launcher mockups (a.html, b.html) from one table of the real controls.

Run: py -3 docs/ui_rework/mockups/build_mockups.py
Labels, choices, preset values and costs are copied from common/settings.h (2026-11-14, 0.9.6).
The output is self-contained HTML: no fonts, CDNs or frameworks.
"""
from html import escape as e
from pathlib import Path

OUT = Path(__file__).resolve().parent
K = {}


def k(id, label, sub, ctl, cost="", restart="", badge="", what="", needs="", presets="", applies="", hot="", alts=None,
      rs=False):
    K[id] = dict(id=id, label=label, sub=sub, ctl=ctl, cost=cost, restart=restart, badge=badge, what=what or sub,
                 needs=needs, presets=presets, applies=applies or "When you save, also while the game runs.", hot=hot,
                 alts=alts or {}, rs=rs)


# ---- the real controls (ids, labels and choices from common/settings.h) ----
k("rt_geometry", "Ray-tracing detail", "What ray tracing sees: solid surfaces only, or also foliage, grates and chains.",
  ("seg", ["Off", "Solid surfaces", "Include foliage"], 2), restart="Restart",
  what="Chooses what ray-traced lighting and shadows include. Off turns off all ray-traced lighting, shadows, "
       "reflections and water.",
  needs="Hardware ray tracing.", presets="Off · Solid surfaces · Include foliage · Include foliage",
  applies="After a game restart.")
k("rr", "DLSS Ray Reconstruction", "Removes noise from ray-traced lighting.", ("tog", 1),
  needs="An NVIDIA RTX GPU, and DLSS chosen as the upscaler in the game's graphics options.",
  presets="Off · On · On · On", applies="When you save. The game may pause for a moment.")
k("rt_intensity", "Mod lighting strength", "Lower values blend toward the game's original look.",
  ("rng", 50, 100, 100, "%", False),
  what="Blends indirect lighting, local shadows and reflections toward the game's original appearance at lower values.",
  presets="100% in every preset")
k("sun_shadows", "Sun shadows", "Ray-traced sun shadows. The top level keeps shadows of roofs the game hides.",
  ("seg", ["Visible surfaces", "Ray-traced", "Include hidden roofs"], 2), cost="+0.3–1.3 ms at the top level",
  what="Ray-traced sun shadows. Include hidden roofs also keeps shadows from roofs and canopies the game hides "
       "near the camera.",
  needs="Ray-tracing detail: Solid surfaces or higher.",
  presets="Visible surfaces · Ray-traced · Ray-traced · Include hidden roofs")
k("local_shadows", "Local light shadows", "Shadows from torches, lamps and spells.", ("seg", ["Off", "Fast", "Full"], 2),
  what="Adds shadows from torches, lamps and spells. Full keeps the game's own shadows where they are needed.",
  needs="Ray-tracing detail. Fast also needs DLSS Ray Reconstruction.", presets="Off · Fast · Full · Full")
k("box_lights", "Interior light shadows", "Shadows from rectangular interior lights that are meant to cast them.",
  ("tog", 1), cost="≈ 0.13 ms",
  what="Adds shadows from rectangular interior lights. Lights the game marks as not casting shadows stay unshadowed.",
  needs="Local light shadows: Fast or Full.", presets="Off · Off · Off · On")
k("gi", "Indirect lighting", "Bounced light and soft shading where surfaces meet.",
  ("seg", ["Off", "Visible surfaces", "Ray-traced", "Ray-traced high"], 3),
  what="Adds bounced light and soft shading where surfaces meet. Ray-traced high traces two rays per pixel for less noise.",
  needs="Ray-traced levels need Ray-tracing detail.",
  presets="Visible surfaces · Ray-traced · Ray-traced · Ray-traced high")
k("exp_hidden_surface", "Off-screen bounce light", "Bounced light and glow from surfaces outside the camera's view.",
  ("seg", ["Off", "Near", "Far (6 m)"], 2), cost="≈ 0.6 ms Near · 3–4 ms Far", restart="Restart to turn on or off",
  what="Adds bounced light and glow from surfaces outside the camera's view. Far carries sunlight bounce up to 6 m.",
  needs="Ray-tracing detail: Include foliage. Indirect lighting: Ray-traced.", presets="Off · Off · Off · Far (6 m)",
  applies="Turning it on or off: after a game restart. Near to Far: when you save, with a short pause.",
  alts={"unsaved": 1, "restart": 1})
k("exp_native_coverage", "Hair shadows", "The game's own hair casts shadows on faces and necks.", ("tog", 1),
  cost="≈ 2–6 ms tracing + 3–9 ms indirect light", restart="Restart to turn on",
  what="Adds shadows and contact shading from the game's hair, including on faces and necks. Visible hair stays "
       "the game's own hair; this is not RTX Hair.",
  needs="Ray-tracing detail: Include foliage.", presets="Off · Off · Off · On",
  applies="On: after a game restart; the first start may pause while shaders compile. Off: when you save.")
k("exp_hair_groom_primary", "RTX Hair", "Hair as individual ray-traced strands instead of flat cards.", ("tog", 1),
  badge="Beta", hot="Ctrl+Shift+F7",
  what="Shows hair as individual ray-traced strands. A style gets strands after the game has shown it once and the "
       "launcher has converted it; until then it keeps the game's look.",
  needs="Converted hair styles (see Home › Preparation).", presets="Off in every preset",
  applies="When you save, or at once in game with its key.", alts={"dependency": 0})
k("hair_distant_detail", "Detail at distance", "Fewer, wider strands on distant heads. Same look, lower cost.",
  ("seg", ["Off", "1/4 (Standard)", "1/8 (Sharper)"], 1), cost="saves ≈ 0.08–0.10 ms",
  what="Only changes RTX Hair. Off traces every strand at every distance. 1/4 (Standard): small, distant heads trace "
       "a quarter of their strands, each drawn wider. 1/8 (Sharper): finer-looking and slightly cheaper, but the "
       "outline is about 2% fuller than physically correct.",
  needs="Only has an effect with RTX Hair on.", presets="Off in every preset")
k("hair_motion_stability", "Motion stability", "Less smearing and fewer trails behind moving hair.", ("tog", 1),
  restart="Restart to turn on",
  what="Only changes RTX Hair. Strand pixels take the motion of the hair they belong to, so the denoiser stops "
       "dragging old hair behind moving heads.",
  needs="Only has an effect with RTX Hair on.", presets="Off · Off · On · On",
  applies="On: after a game restart. Off: when you save.")
k("exp_hair_strand_aa", "Strand hair edge resolve", "Blends thin strand edges by exact coverage instead of dithering.",
  ("seg", ["Off", "Strands unresolved", "Analytic coverage"], 2), badge="Experimental",
  what="How on-screen strand edges enter Ray Reconstruction. With Analytic coverage, thin strands at the edges are "
       "blended with their background instead of flickering. Appearance and cost are not qualified.",
  needs="RTX Hair: On. An NVIDIA RTX GPU with DLSS.", presets="Off in every preset", alts={"dependency": 0})
k("reflections", "Reflections", "Reflections on smooth surfaces and, at higher levels, rougher ones.",
  ("seg", ["Off", "Glossy", "Standard", "Rough"], 3),
  what="Adds reflections to smooth surfaces and, at higher levels, rougher surfaces. Without DLSS Ray "
       "Reconstruction, only mirror-like reflections are traced.",
  needs="Ray-tracing detail and Indirect lighting.", presets="Off · Glossy · Standard · Rough")
k("exp_water", "Water lighting", "Water reflections, shadows and a refracted view of the riverbed.",
  ("seg", ["Off", "Reflections", "Reflections and shadows", "Include refraction"], 3),
  cost="≈ 0.5–0.7 ms (estimate) · up to 520 MB video memory", restart="Restart to turn on or raise",
  what="Adds water reflections, shadows and, at the highest level, a refracted view of the riverbed or pool floor. "
       "Each level includes the lower ones.",
  needs="Ray-tracing detail.", presets="Off · Off · Off · Include refraction",
  applies="Turning it on or raising it: after a game restart. Lowering it or turning it off: when you save.")
k("water_surface_fresnel", "Water surface reflections", "Separate reflection at the water surface; shows at the edges of shallow pools.",
  ("tog", 0), badge="Experimental", restart="Restart to turn on",
  what="Works out the reflection at the water surface separately from the water's depth colour. Mostly visible at "
       "the edges of shallow pools; open water barely changes.",
  needs="Water lighting: Reflections or higher.", presets="Off in every preset",
  applies="On: after a game restart. Off: when you save.")
k("rr_post_temporal", "Depth of field and bloom smoothing", "Steadier background blur and bloom around fine edges.",
  ("tog", 1), cost="≈ 0.15 ms · 250 MiB video memory",
  what="Blends background blur and bloom across frames to smooth fine edges. Subtle; most visible in dialogue "
       "close-ups, on hair against a blurred background.",
  needs="DLSS Ray Reconstruction.", presets="Off · Off · Off · On")
k("frame_generation", "Frame Generation", "Adds generated frames between rendered frames for smoother motion.",
  ("seg", ["Off", "2x", "3x", "4x"], 1), restart="Restart to turn on or off",
  what="Generated frames make motion smoother but do not raise the rendered frame rate.",
  needs="NVIDIA Reflex: On or On + Boost. 2x needs RTX 40 or later; 3x and 4x need RTX 50.",
  presets="2x in every preset",
  applies="Turning it on or off: after a game restart. Switching 2x, 3x, 4x: when you save.",
  alts={"unsaved": 2, "restart": 2})
k("reflex", "NVIDIA Reflex", "Shortens the delay between your input and the image on screen.",
  ("seg", ["Off", "On", "On + Boost"], 1), what="Reduces the delay between your input and the image on screen. "
  "Frame Generation needs On or On + Boost.", presets="On in every preset")
k("fps_cap", "Rendered frame limit", "Caps rendered frames, before Frame Generation adds its own.",
  ("rng", 30, 240, 58, "fps", True),
  what="Limits the frame rate before Frame Generation adds frames. A limit is a ceiling, not a target.",
  presets="Off · Off · Off · 58 fps")
k("exp_gi_rt_pipeline", "Faster bounce light tracing", "Same lighting, about 2 ms faster.", ("tog", 1),
  cost="saves ≈ 2 ms",
  what="Traces the off-screen bounce light with the GPU's ray tracing pipeline instead of a compute pass. The "
       "first time, the pipeline builds for up to a minute while the usual trace keeps running.",
  presets="On in every preset")
k("exp_gi_rt_pipeline_reorder", "Bounce trace reordering", "Same lighting, a further 0.8–1.5 ms faster.",
  ("seg", ["Off", "By hit kind", "By hit kind and object"], 1), cost="saves ≈ 0.8–1.5 ms",
  what="Sorts the bounce rays by what they hit before shading them (shader execution reordering).",
  needs="Faster bounce light tracing: On.", presets="By hit kind in every preset",
  applies="When you save. The game may pause for a moment.")
k("scene_handout", "Low-latency scene", "Lighting stops waiting for each frame's scene build. Same image, less CPU wait.",
  ("tog", 0), badge="Experimental", cost="no GPU cost · CPU wait 4.4–5.1 → 0.5–0.7 ms in one scene",
  restart="Restart to turn on",
  what="Today every frame's ray-traced lighting waits 3 to 9 ms for that frame's scene. On removes the wait. How "
       "much frame rate this gives depends on the scene: little where the GPU is the limit. Not qualified yet.",
  needs="Ray-tracing detail.", presets="Off in every preset",
  applies="On: after a game restart. Off: when you save.")
k("fg_destroy_wait_exact", "Frame generation pacing test", "May reduce Frame Generation stutter.",
  ("seg", ["Standard", "Deferred"], 0), badge="Experimental",
  what="May reduce frame generation stutter when the game replaces temporary images. Cost and smoothness are "
       "not qualified. Compare smoothness while turning in the same scene.",
  needs="Frame Generation: 2x or higher.", presets="Standard in every preset")
k("fg_present_metering", "Multi-frame presentation test", "Driver-paced presentation for 3x and 4x.",
  ("seg", ["Standard", "Driver metering"], 0), badge="Experimental", restart="Restart",
  what="Enables an optional driver presentation capability for frame generation. Smoothness and cost are not "
       "qualified. Compare 3x smoothness in the same scene.",
  needs="Frame Generation and Multi Frame Generation support (RTX 50).", presets="Standard in every preset",
  applies="After a game restart.")
k("dlss5_nr", "DLSS 5 Neural Rendering", "Neural relighting after Ray Reconstruction, before post effects.",
  ("tog", 0), badge="Experimental", restart="Restart", hot="Ctrl+Shift+F8",
  what="Neural relighting after Ray Reconstruction and before post effects and frame generation. Custom or "
       "unsigned runtimes are named in the log and NVIDIA may reject them.",
  needs="DLSS Ray Reconstruction: On. Your own nvngx_dlssnr.dll, version 310.8 or 310.9 (not included).",
  presets="Off in every preset", applies="After a game restart. Its key then switches it at once in game.")
k("detailed_logging", "Detailed logging", "Extra detail in the log for bug reports. Leave off for normal play.",
  ("tog", 0), restart="Restart",
  what="Writes extra performance and diagnostic details to the log. Turn it on, restart the game and reproduce "
       "the problem before creating a report.",
  presets="Off in every preset", applies="After a game restart.", alts={"unsaved": 1, "restart": 1}, rs=True)

ICON = {
    "home": '<path d="M3.5 11 12 4l8.5 7"/><path d="M5.8 9.3v10.2h12.4V9.3"/><path d="M10 19.5v-5.2h4v5.2"/>',
    "lighting": '<circle cx="12" cy="12" r="4"/><path d="M12 2.5V5M12 19v2.5M2.5 12H5M19 12h2.5M5.3 5.3l1.8 1.8M16.9 16.9l1.8 1.8M5.3 18.7l1.8-1.8M16.9 7.1l1.8-1.8"/>',
    "surfaces": '<path d="M12 3.2c3.4 4 5.8 7.2 5.8 10.3a5.8 5.8 0 0 1-11.6 0c0-3.1 2.4-6.3 5.8-10.3z"/>',
    "performance": '<path d="M4.4 17.6a8.6 8.6 0 1 1 15.2 0"/><path d="M12 14.6l4.2-4.9"/><circle cx="12" cy="14.6" r="1.4"/>',
    "dlss5": '<path d="M11 3.5l1.9 5.1L18 10.5l-5.1 1.9L11 17.5l-1.9-5.1L4 10.5l5.1-1.9z"/>',
    "experimental": '<path d="M9.5 3.5h5M10.6 3.5v5.9L5.3 18.1a1.6 1.6 0 0 0 1.4 2.4h10.6a1.6 1.6 0 0 0 1.4-2.4l-5.3-8.7V3.5"/><path d="M7.5 14.6h9"/>',
    "hotkeys": '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6.5 10h1M10.5 10h1M14.5 10h1M8 14h8"/>',
    "help": '<circle cx="12" cy="12" r="9"/><path d="M9.6 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.3-1.1.8-1.1 1.5v.6"/><path d="M12 16.9v.2"/>',
    "picture": '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m3.5 16 5-5 4 4 2.5-2.5 5.5 5"/><circle cx="15.5" cy="9.5" r="1.3"/>',
    "play": '<path d="M8 5.5v13l10.5-6.5z"/>',
    "warn": '<circle cx="12" cy="12" r="9"/><path d="M12 7.5v5.5M12 16.4v.2"/>',
    "ok": '<circle cx="12" cy="12" r="9"/><path d="m8 12.3 2.7 2.7L16 9.6"/>',
    "undo": '<path d="M8 8H4V4"/><path d="M4.5 8a8 8 0 1 1-.4 6"/>',
    "info": '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.4v.2"/>',
}


def ic(n, cls="ic"):
    return f'<svg class="{cls}" viewBox="0 0 24 24" aria-hidden="true">{ICON[n]}</svg>'


GEM = ('<svg class="gem" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2.5 20 8l-8 13.5L4 8z" fill="var(--accent)"/>'
       '<path d="M12 2.5 15 8l-3 13.5L9 8z" fill="var(--accent-hi)"/><path d="M4 8h16" stroke="var(--bg)" stroke-width="1"/></svg>')


def alt_attrs(c):
    return "".join(f' data-v-{s}="{v}"' for s, v in c["alts"].items())


def control(c):
    ct = c["ctl"]
    a = f'data-v="{ct[-1] if ct[0] != "rng" else ct[3]}"{alt_attrs(c)}'
    if ct[0] == "seg":
        b = "".join(f'<button type="button" aria-pressed="false">{e(x)}</button>' for x in ct[1])
        return f'<div class="seg" role="group" aria-label="{e(c["label"])}" {a}>{b}</div>'
    if ct[0] == "tog":
        return (f'<div class="togw" {a}><button type="button" class="tog" role="switch" aria-checked="false" '
                f'aria-label="{e(c["label"])}"><i></i></button><span class="togl">Off</span></div>')
    _, lo, hi, v, unit, off = ct
    sp = "" if unit == "%" else " "
    offb = ('<div class="seg mini" role="group" aria-label="Limit on or off"><button type="button">Off</button>'
            '<button type="button" class="on" aria-pressed="true">On</button></div>') if off else ""
    return (f'<div class="rng">{offb}<input type="range" min="{lo}" max="{hi}" value="{v}" aria-label="{e(c["label"])}" '
            f'data-unit="{sp}{unit}"><output>{v}{sp}{unit}</output></div>')


def badges(c, restart=True):
    out = ""
    if c["badge"]:
        out += f'<span class="bdg {"exp" if c["badge"] == "Experimental" else "beta"}">{c["badge"]}</span>'
    return out


def meta(c):
    out = ""
    if c["restart"]:
        out += f'<span class="bdg rst" title="{e(c["applies"])}">{e(c["restart"])}</span>'
    if c["cost"]:
        out += f'<span class="chip" title="Measured on an RTX 5090. Other GPUs differ.">{e(c["cost"])}</span>'
    if c["hot"]:
        out += f'<span class="keyl">In game <kbd>{c["hot"]}</kbd></span>'
    return out


def detail(c):
    rows = [("What it does", c["what"]), ("GPU cost", (c["cost"] + " on an RTX 5090.") if c["cost"] else "Not measured yet."),
            ("Takes effect", c["applies"]), ("Needs", c["needs"] or "Nothing else."),
            ("Low · Medium · High · Ultra", c["presets"])]
    if c["hot"]:
        rows.append(("In-game key", f'<kbd>{c["hot"]}</kbd> · change it on Hotkeys'))
    return "".join(f'<dt>{h}</dt><dd>{v if "<kbd>" in v else e(v)}</dd>' for h, v in rows)


def row(id, child=False, note="", locked=False):
    c = K[id]
    cls = "row" + (" child" if child else "") + (" lockable" if locked else "")
    rs = ' data-rs="1"' if c["rs"] else ""
    m = meta(c)
    return (f'<div class="{cls}" id="r-{id}" tabindex="0" data-title="{e(c["label"])}"{rs}>'
            f'<div class="rt"><div class="rl"><span class="rname">{e(c["label"])}</span>{badges(c)}'
            f'<span class="was"></span><span class="bdg pend">Restart pending</span></div>'
            f'<div class="rs">{e(c["sub"])}</div>{f"<div class=rm>{m}</div>" if m else ""}{note}</div>'
            f'<div class="rc">{control(c)}<button type="button" class="undo" title="Undo this change" aria-label="Undo change to {e(c["label"])}">{ic("undo")}</button></div>'
            f'<dl class="rd">{detail(c)}</dl></div>')


def group(title, lead, body, extra=""):
    return (f'<section class="grp {extra}"><div class="gh"><h2>{title}</h2>{f"<p>{lead}</p>" if lead else ""}</div>'
            f'<div class="rows">{body}</div></section>')


HAIR_NOTE = ('<div class="note dep" data-show="dependency" role="status">Also turned off: <b>Strand hair edge resolve</b> '
             '(Experimental), because it needs RTX Hair. <b>Detail at distance</b> and <b>Motion stability</b> keep '
             'their values and resume when RTX Hair is on. <button type="button" class="lnk" data-state="normal">Undo</button></div>')
EXP_DEP_NOTE = '<div class="note dep" data-show="dependency">Turned off because RTX Hair is off.</div>'
BOUNCE_NOTE = ('<div class="note" data-show="unsaved">Takes effect when you save, with a short pause.</div>')


def conversion(detail_open=False):
    """RTX Hair style conversion: a launcher job. Applies at once, never counted as an unsaved change."""
    return f'''<div class="job" id="job-hair">
<div class="jobh"><div><div class="rname">RTX Hair styles</div>
<div class="rs" data-hide="prep hairerr">559 of 559 styles seen in game are converted · 1.4 GB on disk</div>
<div class="rs" data-show="prep">412 of 559 styles ready · 1.1 GB on disk · about 35 min left</div></div>
<span class="bdg ok" data-hide="prep">Ready</span><span class="bdg work" data-show="prep">Converting</span>
<button type="button" class="btn sm" data-show="prep">Pause</button></div>
<div class="bar" data-show="prep" role="progressbar" aria-valuenow="74" aria-valuemin="0" aria-valuemax="100" aria-label="RTX Hair conversion"><i style="width:74%"></i></div>
<p class="hint" data-show="prep">You can play now. A style that is not converted yet keeps the game's look.</p>
<details{" open" if detail_open else ""}><summary>Conversion options</summary>
<div class="opt"><span class="ol">Convert</span><div class="seg" data-v="0"><button type="button">Styles seen in game</button><button type="button" disabled title="Needs granny2.dll">All styles</button></div>
<p class="hint">All styles needs your own granny2.dll (BG3RTX cannot include it). <button type="button" class="lnk">Choose granny2.dll…</button></p></div>
<div class="opt"><span class="ol">When</span><div class="seg" data-v="0"><button type="button">Game closed</button><button type="button">Also while playing</button></div>
<p class="hint">While playing, it uses one core at low priority.</p></div>
<p class="hint">These options apply at once. They are not part of Save.</p></details></div>'''


def shaders():
    return '''<div class="job"><div class="jobh"><div><div class="rname">Shaders</div>
<div class="rs" data-hide="prep">71 of 71 prepared</div><div class="rs" data-show="prep">Preparing 41 of 71. The driver still builds the first ray tracing pipeline in game, about a minute.</div></div>
<span class="bdg ok" data-hide="prep">Ready</span><span class="bdg work" data-show="prep">Preparing</span></div>
<div class="bar" data-show="prep" role="progressbar" aria-valuenow="58" aria-valuemin="0" aria-valuemax="100" aria-label="Shader preparation"><i style="width:58%"></i></div></div>'''


def attention(direction):
    li = lambda show, kind, text, btn="": (f'<li class="{kind}" data-show="{show}">{ic("warn" if kind != "ok" else "ok")}'
                                         f'<span>{text}</span>{btn}</li>')
    return ('<ul class="att">'
            + li("missing", "err", "<b>Baldur's Gate 3 was not found.</b> Choose the folder that contains bin\\bg3.exe. "
                 "You can still look through the settings.", '<button type="button" class="btn sm pri">Choose folder…</button>')
            + li("unsaved", "warn", "<b>3 unsaved changes.</b> One needs a game restart.",
                 '<button type="button" class="btn sm" data-dialog="review">Review</button>')
            + li("dependency", "warn", "<b>2 unsaved changes.</b> RTX Hair is off, and one option that needs it.",
                 '<button type="button" class="btn sm" data-dialog="review">Review</button>')
            + li("restart", "warn", "<b>The game is running with older settings.</b> Restart it to apply 1 change: Detailed logging.")
            + li("prep", "info", "<b>RTX Hair styles are still converting</b> (about 35 min). You can play now.")
            + li("normal", "ok", "Nothing needs attention.")
            + '</ul>')


def keys_list():
    return ('<dl class="keys"><div><dt>All ray tracing off/on</dt><dd><kbd>Ctrl+Shift+F9</kbd></dd></div>'
            '<div><dt>DLSS 5 off/on</dt><dd><kbd>Ctrl+Shift+F8</kbd></dd></div>'
            '<div><dt>RTX Hair off/on</dt><dd><kbd>Ctrl+Shift+F7</kbd></dd></div></dl>')


def hotkeys_editor():
    def r(name, note, key):
        return (f'<div class="hk"><div><div class="rname">{name}</div><div class="rs">{note}</div></div>'
                f'<div class="hkc"><input type="text" readonly value="{key}" aria-label="{name}: press a key combination">'
                f'<button type="button" class="btn sm" disabled>Reset</button><button type="button" class="btn sm">Clear</button></div></div>')
    return (r("All ray tracing off/on", "Compare with the game's own lighting. DLSS and Frame Generation stay as they are.", "Ctrl+Shift+F9")
            + r("DLSS 5 off/on", "Works when DLSS 5 was on at game start.", "Ctrl+Shift+F8")
            + r("RTX Hair off/on", "Switches between strands and the game's own hair while you play.", "Ctrl+Shift+F7")
            + '<p class="hint">Click a field, then press Ctrl, Shift or Alt with F1–F12, A–Z or 0–9. Esc cancels. '
              'Ctrl+Shift+F10 is reserved. Key changes are saved with your other changes and apply to the running game.</p>')


def runtime_file():
    return ('<div class="row static"><div class="rt"><div class="rl"><span class="rname">Runtime file</span></div>'
            '<div class="rs">Your own nvngx_dlssnr.dll, version 310.8 or 310.9. BG3RTX does not include it.</div>'
            '<div class="path">No file chosen. Looking in the game folder\'s bin: none found.</div></div>'
            '<div class="rc"><button type="button" class="btn">Browse…</button></div></div>')


def help_cards():
    return dict(
        game=('<div class="row static"><div class="rt"><div class="rl"><span class="rname">Game folder</span></div>'
              '<div class="path" data-hide="missing">C:\\Games\\Baldurs Gate 3</div>'
              '<div class="path errt" data-show="missing">Not found. Choose the folder that contains bin\\bg3.exe.</div>'
              '<div class="rs">Play starts the Vulkan version of the game (bin\\bg3.exe) with BG3RTX.</div></div>'
              '<div class="rc"><button type="button" class="btn">Choose folder…</button></div></div>'
              '<div class="row static"><div class="rt"><div class="rl"><span class="rname">Graphics</span>'
              '<span class="bdg ok">Supported</span></div>'
              '<div class="rs">NVIDIA GeForce RTX 5090 · 32 GB · driver 621.01 · hardware ray tracing</div></div>'
              '<div class="rc"><button type="button" class="btn">Check again</button></div></div>'),
        report=('<div class="row static"><div class="rt"><div class="rl"><span class="rname">Report a problem</span></div>'
                '<div class="rs">Builds a ZIP on your PC with logs, settings and graphics details. You see every file '
                'first. Nothing is uploaded.</div></div><div class="rc"><button type="button" class="btn pri">Create report…</button></div></div>'
                + row("detailed_logging")),
        tips=('<ul class="tips"><li>Game does not start, or Frame Generation does nothing: turn off other overlays '
              '(Steam, Discord, GOG Galaxy) for one launch.</li><li>To compare with the game\'s own lighting, press '
              '<kbd>Ctrl+Shift+F9</kbd> in game.</li><li>Ray Reconstruction needs DLSS as the upscaler in the game\'s '
              'graphics options. Play sets it for you.</li></ul>'),
        about=('<div class="row static"><div class="rt"><div class="rl"><span class="rname">BG3RTX 0.9.6</span></div>'
               '<div class="rs">Independent fan mod; not affiliated with the game\'s developers. Mod files present.</div></div>'
               '<div class="rc"><button type="button" class="btn sm">Copy version</button><button type="button" class="lnk">Fonts and third-party notices</button></div></div>'))


def exp_switch(text="Show experimental options"):
    return (f'<div class="expsw"><div><div class="rname">{text}</div><div class="rs">Not fully tested. Off in every '
            f'preset. May change the image or cost performance. Turning this off resets them all to Off.</div></div>'
            f'<div class="togw"><button type="button" class="tog" role="switch" id="expsw" aria-checked="true" '
            f'aria-label="{text}"><i></i></button><span class="togl">On</span></div></div>')


REVIEW = '''<dialog id="review" aria-labelledby="rvt"><h2 id="rvt">Review changes</h2>
<div data-hide="dependency"><h3>Applies when you save</h3><ul class="chg">
<li><span>Frame Generation</span><span>2x <b>→</b> 3x</span></li>
<li><span>Off-screen bounce light</span><span>Far (6 m) <b>→</b> Near <em>short pause</em></span></li></ul>
<h3>Applies after a game restart</h3><ul class="chg"><li><span>Detailed logging</span><span>Off <b>→</b> On</span></li></ul></div>
<div data-show="dependency"><h3>You changed</h3><ul class="chg"><li><span>RTX Hair</span><span>On <b>→</b> Off</span></li></ul>
<h3>Changed with it</h3><ul class="chg"><li><span>Strand hair edge resolve <span class="bdg exp">Experimental</span></span><span>Analytic coverage <b>→</b> Off <em>needs RTX Hair</em></span></li></ul>
<p class="hint">Detail at distance and Motion stability keep their values for the next time RTX Hair is on.</p></div>
<p class="hint">Your preset becomes <b>Custom</b>, based on Ultra.</p>
<div class="dact"><button type="button" class="btn" data-close>Keep editing</button><button type="button" class="btn" data-state="normal" data-close>Discard all</button><button type="button" class="btn pri" data-state="restart" data-close>Save</button></div></dialog>'''

PRESET_DESC = [
    ("Low", "Lightest. Lighting and sun shadows use visible surfaces only; ray tracing and Ray Reconstruction are off."),
    ("Medium", "Ray-traced lighting and sun shadows on solid surfaces, Fast local light shadows, Glossy reflections."),
    ("High", "Adds foliage to ray tracing, Full local light shadows and Standard reflections."),
    ("Ultra", "Adds hidden-roof, interior light and hair shadows, two-ray indirect lighting, Rough reflections, "
              "water lighting and Far off-screen bounce light. Limits rendering to 58 fps."),
]

STATES = [("normal", "Saved"), ("unsaved", "Unsaved changes"), ("restart", "Restart needed"),
          ("dependency", "Dependency change"), ("missing", "Game folder missing"), ("prep", "Preparation running")]


def mockbar(name, other):
    st = "".join(f'<button type="button" data-state="{s}">{t}</button>' for s, t in STATES)
    return (f'<div class="mockbar"><b>Mockup · {name}</b><span>State</span><div class="mseg" id="mstate">{st}</div>'
            f'<button type="button" id="mtheme">Light / dark</button><span class="msp"></span>'
            f'<a href="{other[1]}">{other[0]}</a><a href="index.html">Index</a></div>')


BASE_CSS = r'''
:root{--bg:#0f1114;--surface:#171a1f;--surface-2:#1e2228;--surface-3:#282d35;--line:#2c3139;--line-2:#626a78;
--text:#eee8da;--text-2:#bcb6a7;--text-3:#948f83;--accent:#d9ae68;--accent-hi:#f0cb8a;--accent-ink:#1b1408;
--info:#7fc4d6;--ok:#8fc49a;--warn:#f0b860;--err:#f1948a;--exp:#bba6f2;
--warn-bg:#2b2416;--err-bg:#2e1b19;--info-bg:#15262b;--exp-bg:#221d33;--ok-bg:#16261b;
--r:8px;--r-sm:6px;color-scheme:dark}
:root[data-theme=light]{--bg:#efece5;--surface:#fbfaf7;--surface-2:#f3f0e9;--surface-3:#e8e4da;--line:#dcd7cb;--line-2:#858072;
--text:#1d1b16;--text-2:#4a463d;--text-3:#676257;--accent:#8f5e12;--accent-hi:#b07a22;--accent-ink:#ffffff;
--info:#0f6a80;--ok:#2c7341;--warn:#855600;--err:#ad3327;--exp:#5a3fc0;
--warn-bg:#f8ecd2;--err-bg:#f9e1dd;--info-bg:#dff0f4;--exp-bg:#e9e3fa;--ok-bg:#e0f1e4;color-scheme:light}
*{box-sizing:border-box}[hidden]{display:none!important}
html,body{margin:0;height:100%}
body{font:14px/1.45 "Lato","Segoe UI",system-ui,sans-serif;color:var(--text);background:var(--bg);display:flex;flex-direction:column}
button,input{font:inherit;color:inherit}button{cursor:pointer}button:disabled{cursor:not-allowed;opacity:.45}
:is(button,a,summary,input,.row):focus-visible{outline:2px solid var(--info);outline-offset:2px}
h1,h2,h3,p,ul,dl,dd{margin:0;padding:0}ul{list-style:none}
.ic{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.75;stroke-linecap:round;stroke-linejoin:round;flex:none}
.gem{width:22px;height:22px;flex:none}
kbd{font:600 12px/1 "Segoe UI",sans-serif;padding:3px 6px;border:1px solid var(--line-2);border-radius:4px;background:var(--surface-2);white-space:nowrap}
/* mockup chrome: not part of the app */
.mockbar{display:flex;align-items:center;gap:10px;padding:6px 12px;background:#000;color:#cfcfcf;font:12px/1.2 "Segoe UI",sans-serif;flex:none;flex-wrap:wrap}
.mockbar b{color:#fff}.mockbar a{color:#9fd3e0}.msp{flex:1}
.mockbar button{background:#1c1c1c;border:1px solid #444;color:#ddd;border-radius:4px;padding:3px 8px}
.mseg{display:flex;gap:2px}.mseg button.on{background:#d9ae68;color:#000;border-color:#d9ae68}
body.noshell .mockbar{display:none}
/* buttons */
.btn{min-height:32px;padding:0 14px;border-radius:var(--r-sm);border:1px solid var(--line-2);background:var(--surface-2);white-space:nowrap}
.btn:hover:not(:disabled){background:var(--surface-3)}
.btn.pri{background:var(--accent);border-color:var(--accent);color:var(--accent-ink);font-weight:600}
.btn.pri:hover:not(:disabled){background:var(--accent-hi);border-color:var(--accent-hi)}
.btn.sm{min-height:28px;padding:0 10px;font-size:13px}
.lnk{background:none;border:0;padding:0;color:var(--info);text-decoration:underline;text-underline-offset:2px}
/* segmented choice */
.seg{display:inline-flex;border:1px solid var(--line-2);border-radius:var(--r-sm);background:var(--bg);padding:2px;gap:2px;max-width:100%;flex-wrap:wrap}
.seg button{min-height:28px;padding:0 11px;border:0;border-radius:4px;background:none;color:var(--text-2);white-space:nowrap}
.seg button:hover:not(:disabled){background:var(--surface-3);color:var(--text)}
.seg button.on{background:var(--accent);color:var(--accent-ink);font-weight:600}
/* toggle */
.togw{display:inline-flex;align-items:center;gap:8px}.togl{min-width:22px;color:var(--text-2)}
.tog{width:40px;height:22px;border-radius:11px;border:1px solid var(--line-2);background:var(--bg);padding:0;position:relative}
.tog i{position:absolute;left:2px;top:2px;width:16px;height:16px;border-radius:50%;background:var(--text-3);transition:left .12s}
.tog[aria-checked=true]{background:var(--accent);border-color:var(--accent)}.tog[aria-checked=true] i{left:20px;background:var(--accent-ink)}
/* slider */
.rng{display:inline-flex;align-items:center;gap:10px}.rng input{width:150px;accent-color:var(--accent)}.rng output{min-width:52px;text-align:right;font-variant-numeric:tabular-nums}
/* badges and chips */
.bdg{display:inline-flex;align-items:center;height:20px;padding:0 7px;border-radius:10px;font-size:12px;font-weight:600;white-space:nowrap;border:1px solid transparent}
.bdg.rst{color:var(--warn);border-color:color-mix(in srgb,var(--warn) 55%,transparent)}
.bdg.pend{background:var(--warn);color:var(--accent-ink);display:none}
.bdg.exp{color:var(--exp);background:var(--exp-bg)}.bdg.beta{color:var(--info);background:var(--info-bg)}
.bdg.ok{color:var(--ok);background:var(--ok-bg)}.bdg.work{color:var(--info);background:var(--info-bg)}
.chip{display:inline-flex;align-items:center;height:20px;padding:0 7px;border-radius:4px;font-size:12px;color:var(--text-2);background:var(--surface-3);font-variant-numeric:tabular-nums}
.keyl{font-size:12px;color:var(--text-3)}
/* setting row */
.row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:6px 20px;align-items:center;padding:12px 16px;border-top:1px solid var(--line)}
.rows>.row:first-child{border-top:0}
.rl{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.rname{font-weight:600}
.rs{color:var(--text-2);font-size:13px;margin-top:1px}.rm{display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin-top:6px}
.rc{display:flex;align-items:center;gap:8px;justify-content:flex-end;flex-wrap:wrap}
.rd{display:none}.rd dt{font-size:12px;color:var(--text-3);margin-top:12px;text-transform:uppercase;letter-spacing:.04em}.rd dd{color:var(--text-2);font-size:13px;margin-top:2px}
.row.child{padding-left:40px;position:relative}.row.child::before{content:"";position:absolute;left:22px;top:0;bottom:0;border-left:2px solid var(--line)}
.was{display:none;font-size:12px;color:var(--accent)}.undo{display:none;width:28px;height:28px;padding:0;border:1px solid var(--line-2);border-radius:var(--r-sm);background:var(--surface-2);align-items:center;justify-content:center}
.undo .ic{width:15px;height:15px}
.row.changed{box-shadow:inset 3px 0 0 var(--accent)}.row.changed .was,.row.changed .undo{display:inline-flex}
.row.pending .bdg.pend{display:inline-flex}
.row.off-parent .rc,.row.off-parent .rs,.row.off-parent .rm{opacity:.45}
.note{grid-column:1/-1;font-size:13px;color:var(--text-2);margin-top:6px}
.note.dep{background:var(--warn-bg);border:1px solid color-mix(in srgb,var(--warn) 40%,transparent);border-radius:var(--r-sm);padding:8px 10px;color:var(--text)}
.path{font:12px/1.4 Consolas,monospace;color:var(--text-2);margin-top:4px;word-break:break-all}.errt{color:var(--err)}
.hint{font-size:12.5px;color:var(--text-3);margin-top:6px}
/* cards / groups */
.grp{margin-bottom:24px}.gh{display:flex;align-items:baseline;gap:12px;margin:0 0 8px}
.gh h2{font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--text-3)}.gh p{font-size:13px;color:var(--text-3)}
.rows,.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--r)}
.card{padding:16px}.card>h2{font-size:15px;margin-bottom:10px}
/* jobs (preparation) */
.job{padding:12px 16px;border-top:1px solid var(--line)}.job:first-child{border-top:0}
.jobh{display:flex;gap:10px;align-items:center}.jobh>div:first-child{flex:1}
.bar{height:6px;border-radius:3px;background:var(--surface-3);margin-top:10px;overflow:hidden}.bar i{display:block;height:100%;background:var(--accent)}
details>summary{cursor:pointer;color:var(--info);font-size:13px;margin-top:8px;width:fit-content}
.opt{display:grid;grid-template-columns:70px 1fr;gap:4px 10px;align-items:center;margin-top:10px}.opt .hint{grid-column:2;margin:0}.ol{color:var(--text-2);font-size:13px}
/* attention list */
.att li{display:flex;gap:10px;align-items:center;padding:10px 14px;border-radius:var(--r);margin-bottom:6px;border:1px solid var(--line);background:var(--surface)}
.att li span{flex:1}.gcard .att li{background:var(--bg)}.att .ok{color:var(--text-2)}.att .ok .ic{color:var(--ok)}
.att .warn{background:var(--warn-bg);border-color:color-mix(in srgb,var(--warn) 40%,transparent)}.att .warn .ic{color:var(--warn)}
.att .err{background:var(--err-bg);border-color:color-mix(in srgb,var(--err) 45%,transparent)}.att .err .ic{color:var(--err)}
.att .info{background:var(--info-bg);border-color:color-mix(in srgb,var(--info) 35%,transparent)}.att .info .ic{color:var(--info)}
.keys div{display:flex;justify-content:space-between;align-items:center;padding:5px 0}.keys dt{color:var(--text-2)}
.hk{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:16px;align-items:center;padding:12px 16px;border-top:1px solid var(--line)}.hk:first-child{border-top:0}
.hkc{display:flex;gap:6px}.hkc input{width:150px;height:28px;border-radius:var(--r-sm);border:1px solid var(--line-2);background:var(--bg);text-align:center;font-weight:600}
.tips{padding:12px 16px 12px 32px;list-style:disc;color:var(--text-2);font-size:13px}.tips li{margin:4px 0}
.expsw{display:flex;gap:16px;align-items:center;padding:14px 16px;background:var(--exp-bg);border:1px solid color-mix(in srgb,var(--exp) 35%,transparent);border-radius:var(--r);margin-bottom:16px}.expsw>div:first-child{flex:1}
body[data-exp=off] .needexp{display:none}body[data-exp=on] .noexp{display:none}
.empty{padding:20px 16px;color:var(--text-3);text-align:center;border:1px dashed var(--line-2);border-radius:var(--r)}
/* banner */
.banner{display:flex;gap:10px;align-items:center;padding:9px 16px;font-size:13px}
.banner.warn{background:var(--warn-bg);color:var(--text)}.banner.warn .ic{color:var(--warn)}
.banner.err{background:var(--err-bg)}body[data-page=home] .banner.err{display:none}.banner.err .ic{color:var(--err)}.banner span{flex:1}
/* dialog */
dialog{border:1px solid var(--line-2);border-radius:12px;background:var(--surface);color:var(--text);padding:24px;width:min(520px,calc(100vw - 32px));box-shadow:0 20px 60px #000a}
dialog::backdrop{background:#000a}dialog h2{font-size:18px;margin-bottom:8px}dialog h3{font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:var(--text-3);margin:16px 0 6px}
.chg li{display:flex;justify-content:space-between;gap:16px;padding:7px 0;border-top:1px solid var(--line)}.chg li:first-child{border-top:0}
.chg b{color:var(--accent);font-weight:400}.chg em{font-style:normal;font-size:12px;color:var(--text-3);margin-left:6px}
.dact{display:flex;gap:8px;justify-content:flex-end;margin-top:20px}
/* changes bar */
.cbar{display:flex;gap:10px;align-items:center;padding:10px 20px;background:var(--surface-2);border-top:1px solid var(--line-2)}
.cbar .dot{width:8px;height:8px;border-radius:50%;background:var(--accent)}.cbar span.t{flex:1}
'''

JS = r'''
const B=document.body,q=new URLSearchParams(location.hash.slice(1));
const $$=s=>[...document.querySelectorAll(s)];
function setCtl(w,v){const s=w.matches('.seg')?w.querySelectorAll('button'):null;
 if(s)s.forEach((b,i)=>{b.classList.toggle('on',i==v);b.setAttribute('aria-pressed',i==v);});
 else{const t=w.querySelector('.tog');t.setAttribute('aria-checked',v==1);w.querySelector('.togl').textContent=v==1?'On':'Off';}}
function label(w,v){return w.matches('.seg')?w.querySelectorAll('button')[v].textContent:(v==1?'On':'Off');}
function state(s){B.dataset.state=s;
 $$('[data-show]').forEach(x=>x.hidden=!x.dataset.show.split(' ').includes(s));
 $$('[data-hide]').forEach(x=>x.hidden=x.dataset.hide.split(' ').includes(s));
 $$('[data-v]').forEach(w=>{const k='v'+s[0].toUpperCase()+s.slice(1),alt=w.dataset[k],v=alt??w.dataset.v;setCtl(w,v);
  const r=w.closest('.row');if(!r)return;const ch=alt!==undefined&&alt!==w.dataset.v;
  r.classList.toggle('changed',ch&&s!=='restart');r.classList.toggle('pending',ch&&s==='restart'&&!!r.dataset.rs);
  const was=r.querySelector('.was');if(was)was.textContent=ch?'Changed · was '+label(w,w.dataset.v):'';});
 $$('.hairchild').forEach(r=>r.classList.toggle('off-parent',s==='dependency'));
 $$('#mstate button').forEach(b=>b.classList.toggle('on',b.dataset.state===s));}
function page(p){B.dataset.page=p;$$('.page').forEach(x=>x.hidden=x.id!=='p-'+p);
 $$('[data-page-link]').forEach(b=>{const on=b.dataset.pageLink===p;b.classList.toggle('on',on);if(b.closest('nav'))b.setAttribute('aria-current',on?'page':'false');});
 const sc=document.querySelector('.scroll');if(sc)sc.scrollTop=0;if(window.onPage)onPage(p);}
document.addEventListener('click',ev=>{const t=ev.target.closest('button,a,.row');if(!t)return;
 if(t.dataset.close!==undefined)t.closest('dialog').close();
 if(t.dataset.state)state(t.dataset.state);
 if(t.dataset.pageLink){page(t.dataset.pageLink);if(t.dataset.sel){const x=document.getElementById('r-'+t.dataset.sel);if(x){x.scrollIntoView({block:'center'});if(window.onRow)onRow(x);}}return;}
 if(t.dataset.dialog)document.getElementById(t.dataset.dialog).showModal();
 if(t.id==='mtheme'){const r=document.documentElement;r.dataset.theme=r.dataset.theme==='light'?'':'light';}
 if(t.id==='expsw'||t.classList.contains('expsw-b')){const on=B.dataset.exp!=='on';B.dataset.exp=on?'on':'off';$$('#expsw,.expsw-b').forEach(x=>{x.setAttribute('aria-checked',on);x.nextElementSibling.textContent=on?'On':'Off';});return;}
 if(t.classList.contains('undo')){state('normal');return;}
 const w=t.closest('[data-v]');if(w&&t.matches('.seg button,.tog')){
  if(w.closest('#r-exp_hair_groom_primary')){state(B.dataset.state==='dependency'?'normal':'dependency');return;}
  const v=t.matches('.tog')?(t.getAttribute('aria-checked')==='true'?0:1):[...w.querySelectorAll('button')].indexOf(t);setCtl(w,v);
  const r=w.closest('.row');if(r){const ch=String(v)!==w.dataset.v;r.classList.toggle('changed',ch);r.querySelector('.was').textContent=ch?'Changed · was '+label(w,w.dataset.v):'';}}
 const rr=ev.target.closest('.row');if(rr&&window.onRow)onRow(rr);});
document.addEventListener('input',ev=>{if(ev.target.type==='range')ev.target.nextElementSibling.textContent=ev.target.value+ev.target.dataset.unit;});
document.addEventListener('keydown',ev=>{if(ev.key==='Enter'&&ev.target.classList.contains('row'))ev.target.click();});
if(q.get('theme')==='light')document.documentElement.dataset.theme='light';
if(q.get('shell')==='0')B.classList.add('noshell');
B.dataset.exp=q.get('exp')==='0'?'off':'on';if(B.dataset.exp==='off')$$('#expsw,.expsw-b').forEach(x=>{x.setAttribute('aria-checked',false);x.nextElementSibling.textContent='Off';});
page(q.get('page')||'home');state(q.get('state')||'normal');
if(q.get('sel')){const x=document.getElementById('r-'+q.get('sel'));if(x){x.scrollIntoView({block:'center'});if(window.onRow)onRow(x);if(q.get('info'))x.classList.add('open');}}
if(q.get('open'))q.get('open').split(',').forEach(i=>{const d=document.getElementById(i);if(d)d.open=true;});
if(q.get('scroll'))document.getElementById(q.get('scroll'))?.scrollIntoView();
if(q.get('dialog'))document.getElementById(q.get('dialog')).showModal();
'''


def doc(title, css, body, js=""):
    return (f'<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{title}</title><style>{BASE_CSS}{css}</style></head>\n<body data-page="home" data-state="normal" data-exp="on">\n'
            f'{body}\n{REVIEW}\n<script>{js}{JS}</script></body></html>\n')


# ======================= Direction A: sidebar, compact rows, detail panel =======================
A_CSS = r'''
.app{flex:1;min-height:0;display:grid;grid-template-columns:212px minmax(0,1fr)}
.side{background:var(--surface);border-right:1px solid var(--line);display:flex;flex-direction:column;padding:14px 10px;min-height:0}
.brand{display:flex;align-items:center;gap:8px;padding:2px 8px 14px;font-weight:700;letter-spacing:.02em}
nav{display:flex;flex-direction:column;gap:2px}
nav button{display:flex;align-items:center;gap:10px;height:36px;padding:0 10px;border:0;border-radius:var(--r-sm);background:none;color:var(--text-2);text-align:left}
nav button:hover{background:var(--surface-2);color:var(--text)}
nav button.on{background:var(--surface-3);color:var(--text);font-weight:600;box-shadow:inset 3px 0 0 var(--accent)}
nav button span{flex:1}nav .sep{height:1px;background:var(--line);margin:8px 6px}
nav .cnt{flex:none;min-width:18px;height:18px;border-radius:9px;background:var(--accent);color:var(--accent-ink);font-size:11px;font-weight:700;display:inline-flex;align-items:center;justify-content:center}
nav .pdot{flex:none;width:8px;height:8px;border-radius:50%;background:var(--info)}
.sfoot{margin-top:auto;display:flex;flex-direction:column;gap:8px}
.playbtn{height:44px;font-size:16px;display:flex;align-items:center;justify-content:center;gap:8px}.playbtn .ic{fill:currentColor;stroke:none;width:18px;height:18px}
.sline{font-size:12px;color:var(--text-3);text-align:center}
.main{display:flex;flex-direction:column;min-width:0;min-height:0}
.top{display:flex;align-items:center;gap:16px;padding:14px 24px;border-bottom:1px solid var(--line);background:var(--surface)}
.top h1{font-size:20px;line-height:1.2}.top .lead{font-size:13px;color:var(--text-3)}.top .tl{flex:1;min-width:0}
.preset{display:flex;align-items:center;gap:10px}.preset .pl{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-3)}
.pcustom{font-size:12px;color:var(--text-2);text-align:right;margin-top:3px}
.body{flex:1;min-height:0;display:grid;grid-template-columns:minmax(0,1fr) 300px}
body:is([data-page=home],[data-page=hotkeys],[data-page=help]) .body{grid-template-columns:minmax(0,1fr)}
body:is([data-page=home],[data-page=hotkeys],[data-page=help]) .detail{display:none}
.scroll{overflow:auto;padding:20px 24px 28px}.page{max-width:860px}
.detail{border-left:1px solid var(--line);background:var(--surface);padding:20px;overflow:auto}
.detail h2{font-size:16px}.detail .dh{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--text-3);margin-bottom:6px}
.detail dl dt{font-size:12px;color:var(--text-3);margin-top:14px;text-transform:uppercase;letter-spacing:.04em}.detail dl dd{color:var(--text-2);font-size:13px;margin-top:3px}
.row{cursor:default}.row:hover{background:color-mix(in srgb,var(--surface-2) 60%,transparent)}.row.sel{background:var(--surface-2)}
.row.static{cursor:auto}.row.static:hover{background:none}
.hero{display:flex;gap:16px;align-items:center;margin-bottom:16px}.hero h2{font-size:22px;line-height:1.2}.hero p{color:var(--text-2);margin-top:2px}
.hero .hi{width:44px;height:44px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:var(--ok-bg);color:var(--ok)}
.hero .hi.bad{background:var(--err-bg);color:var(--err)}.hero .hi .ic{width:26px;height:26px}
.cols{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(0,1fr);gap:16px;align-items:start}
.ptr{display:flex;gap:10px;align-items:center;padding:12px 16px;border:1px dashed var(--line-2);border-radius:var(--r);color:var(--text-2);font-size:13px}.ptr span{flex:1}
@media (max-width:1100px){.body{grid-template-columns:minmax(0,1fr)}.detail{display:none}.row.sel .rd{display:block;grid-column:1/-1}.cols{grid-template-columns:minmax(0,1fr)}.top .lead{display:none}}
'''
A_JS = r'''
function onRow(r){if(r.classList.contains('static'))return;document.querySelectorAll('.row.sel').forEach(x=>x.classList.remove('sel'));r.classList.add('sel');
 document.getElementById('dt').textContent=r.dataset.title;document.getElementById('dd').innerHTML=r.querySelector('.rd').innerHTML;}
function onPage(p){document.querySelectorAll('[data-pt]').forEach(x=>x.hidden=x.dataset.pt!==p);const r=document.querySelector('#p-'+p+' .row:not(.static)');if(r)onRow(r);}
'''


def build_a():
    nav_items = [("home", "Home"), ("lighting", "Lighting"), ("surfaces", "Surfaces"), ("performance", "Performance"),
                 ("dlss5", "DLSS 5"), ("experimental", "Experimental"), None, ("hotkeys", "Hotkeys"), ("help", "Help & support")]
    nav = ""
    for it in nav_items:
        if it is None:
            nav += '<div class="sep"></div>'
            continue
        extra = ""
        if it[0] == "home":
            extra = '<i class="pdot" data-show="prep" title="Preparation running"></i>'
        if it[0] == "help":
            extra = '<i class="cnt" data-show="missing" title="1 problem">1</i>'
        nav += f'<button type="button" data-page-link="{it[0]}">{ic(it[0])}<span>{it[1]}</span>{extra}</button>'
    titles = {"home": ("Home", "Start the game and see what needs attention."),
              "lighting": ("Lighting", "Ray-traced shadows, bounced light and hair."),
              "surfaces": ("Surfaces", "Reflections, water and post effects."),
              "performance": ("Performance", "Frame rate, latency and tracing speed."),
              "dlss5": ("DLSS 5", "Neural rendering with your own runtime file."),
              "experimental": ("Experimental", "Options that are not fully tested yet."),
              "hotkeys": ("Hotkeys", "Keys that work while you play."),
              "help": ("Help & support", "Game folder, graphics check and problem reports.")}
    heads = "".join(f'<div class="tl" data-pt="{p}"><h1>{t}</h1><div class="lead">{l}</div></div>' for p, (t, l) in titles.items())
    preset = ('<div><div class="preset"><span class="pl">Preset</span><div class="seg" role="group" aria-label="Preset">'
              + "".join(f'<button type="button" title="{e(d)}"{" class=on aria-pressed=true" if n == "Ultra" else ""}>{n}</button>' for n, d in PRESET_DESC)
              + '</div></div><div class="pcustom">Custom · 3 changes from Ultra · <button type="button" class="lnk">Reset to Ultra</button></div></div>')
    home = f'''<div class="page" id="p-home">
<div class="hero" data-hide="missing"><div class="hi">{ic("ok")}</div><div><h2>Ready to play</h2><p>Ultra with 3 changes · NVIDIA GeForce RTX 5090 · 32 GB · driver 621.01</p></div></div>
<div class="hero" data-show="missing"><div class="hi bad">{ic("warn")}</div><div><h2>Game folder needed</h2><p>Play is off until BG3RTX knows where Baldur's Gate 3 is installed.</p></div></div>
<div class="cols"><div>
<section class="grp"><div class="gh"><h2>Needs attention</h2></div>{attention("a")}</section>
{group("Preparation", "Runs in the background. Never blocks Play.", shaders() + conversion())}
</div><div>
{group("This preset", "", '<div style="padding:12px 16px"><div class="rname">Ultra</div><p class="rs">' + PRESET_DESC[3][1] + '</p><p class="hint">Your 3 changes: RTX Hair On, Detail at distance 1/4 (Standard), Strand hair edge resolve Analytic coverage.</p></div>')}
{group("In-game keys", "", '<div style="padding:8px 16px 12px">' + keys_list() + '<button type="button" class="lnk" data-page-link="hotkeys">Change keys</button></div>')}
</div></div></div>'''
    hairlink = ('<div class="row static child hairchild"><div class="rt"><div class="rs">Style conversion: '
                '<span data-hide="prep">559 of 559 ready.</span><span data-show="prep">412 of 559 ready, about 35 min left.</span> '
                '<button type="button" class="lnk" data-page-link="home">Open Preparation</button></div></div></div>')
    lighting = ('<div class="page" id="p-lighting">'
                + group("Ray tracing", "The base the other options build on.", row("rt_geometry") + row("rr") + row("rt_intensity"))
                + group("Shadows", "", row("sun_shadows") + row("local_shadows") + row("box_lights", child=True))
                + group("Bounced light", "", row("gi") + row("exp_hidden_surface", child=True, note=BOUNCE_NOTE))
                + group("Hair", "", row("exp_native_coverage") + row("exp_hair_groom_primary", note=HAIR_NOTE)
                        + row("hair_distant_detail", child=True).replace('class="row child"', 'class="row child hairchild"')
                        + row("hair_motion_stability", child=True).replace('class="row child"', 'class="row child hairchild"')
                        + hairlink)
                + f'<div class="ptr needexp">{ic("experimental")}<span>1 experimental option for RTX Hair: Strand hair edge resolve.</span><button type="button" class="lnk" data-page-link="experimental" data-sel="exp_hair_strand_aa">Open Experimental</button></div>'
                + '</div>')
    surfaces = ('<div class="page" id="p-surfaces">' + group("Reflections", "", row("reflections"))
                + group("Water", "", row("exp_water")) + group("Post effects", "", row("rr_post_temporal"))
                + f'<div class="ptr needexp">{ic("experimental")}<span>1 experimental option for water: Water surface reflections.</span><button type="button" class="lnk" data-page-link="experimental" data-sel="water_surface_fresnel">Open Experimental</button></div>'
                + '</div>')
    performance = ('<div class="page" id="p-performance">'
                   + group("Frame rate", "", row("frame_generation") + row("reflex") + row("fps_cap"))
                   + group("Tracing speed", "Same image, less GPU time. On in every preset; turn off only to troubleshoot.",
                           row("exp_gi_rt_pipeline") + row("exp_gi_rt_pipeline_reorder", child=True))
                   + f'<div class="ptr needexp">{ic("experimental")}<span>3 experimental options: Low-latency scene and two Frame Generation tests.</span><button type="button" class="lnk" data-page-link="experimental" data-sel="scene_handout">Open Experimental</button></div>'
                   + f'<div class="ptr noexp">{ic("experimental")}<span>Experimental performance options are hidden.</span><button type="button" class="lnk" data-page-link="experimental">Open Experimental</button></div>'
                   + '</div>')
    dlss5 = ('<div class="page" id="p-dlss5">' + group("DLSS 5", "Experimental. Off in every preset.", row("dlss5_nr") + runtime_file()) + '</div>')
    experimental = ('<div class="page" id="p-experimental">' + exp_switch()
                    + '<div class="needexp">'
                    + group("Performance", "", row("scene_handout"))
                    + group("Frame Generation", "", row("fg_destroy_wait_exact") + row("fg_present_metering"))
                    + group("Water", "", row("water_surface_fresnel"))
                    + group("RTX Hair", "", row("exp_hair_strand_aa", note=EXP_DEP_NOTE))
                    + '</div><div class="empty noexp">Experimental options are hidden and off. Turn the switch on to see 5 options.</div></div>')
    hotkeys = '<div class="page" id="p-hotkeys">' + group("In-game keys", "", hotkeys_editor()) + '</div>'
    h = help_cards()
    helpp = ('<div class="page" id="p-help">' + group("Game and graphics", "", h["game"]) + group("Problem reports", "", h["report"])
             + group("Tips", "", h["tips"]) + group("About", "", h["about"]) + '</div>')
    body = f'''{mockbar("Direction A · sidebar, rows, detail panel", ("Direction B", "b.html"))}
<div class="app"><aside class="side"><div class="brand">{GEM}<span>BG3RTX</span></div>
<nav aria-label="Sections">{nav}</nav>
<div class="sfoot"><button type="button" class="btn pri playbtn" data-hide="missing unsaved dependency">{ic("play")}Play</button>
<button type="button" class="btn pri playbtn" data-show="unsaved dependency" title="Saves your changes, then starts the game">{ic("play")}Save &amp; play</button>
<button type="button" class="btn pri playbtn" data-show="missing" disabled>{ic("play")}Play</button>
<div class="sline" data-show="missing">Choose the game folder first</div>
<div class="sline" data-hide="missing">BG3RTX 0.9.6</div></div></aside>
<div class="main"><header class="top">{heads}{preset}</header>
<div class="banner err" data-show="missing" role="alert">{ic("warn")}<span><b>Baldur's Gate 3 was not found.</b> Choose the folder that contains bin\\bg3.exe to turn Play on.</span><button type="button" class="btn sm pri">Choose folder…</button></div>
<div class="banner warn" data-show="restart" role="status">{ic("warn")}<span><b>Saved.</b> The game is running: restart it to apply 1 change (Detailed logging). 2 changes are already active.</span><button type="button" class="lnk" data-page-link="help" data-sel="detailed_logging">Show</button></div>
<div class="body"><div class="scroll">{home}{lighting}{surfaces}{performance}{dlss5}{experimental}{hotkeys}{helpp}</div>
<aside class="detail" aria-live="polite"><div class="dh">Details</div><h2 id="dt"></h2><dl id="dd"></dl></aside></div>
<footer class="cbar" data-show="unsaved dependency"><i class="dot"></i>
<span class="t" data-show="unsaved"><b>3 unsaved changes</b> · 1 needs a game restart</span>
<span class="t" data-show="dependency"><b>2 unsaved changes</b> · RTX Hair and 1 option that needs it</span>
<button type="button" class="lnk" data-dialog="review">Review</button><button type="button" class="btn" data-state="normal">Discard</button><button type="button" class="btn pri" data-state="restart">Save</button></footer>
</div></div>'''
    return doc("BG3RTX mockup A", A_CSS, body, A_JS)


# ======================= Direction B: four tabs, grouped cards, progressive disclosure =======================
B_CSS = r'''
.bhead{display:flex;align-items:center;gap:20px;padding:0 24px;height:56px;background:var(--surface);border-bottom:1px solid var(--line);flex:none}
.brand{display:flex;align-items:center;gap:8px;font-weight:700}.brand small{font-weight:400;color:var(--text-3);font-size:12px}
.tabs{display:flex;gap:4px;height:100%}.tabs button{display:flex;align-items:center;gap:8px;padding:0 14px;border:0;background:none;color:var(--text-2);border-bottom:3px solid transparent;border-top:3px solid transparent;font-size:15px}
.tabs button:hover{color:var(--text)}.tabs button.on{color:var(--text);font-weight:600;border-bottom-color:var(--accent)}
.tabs .cnt{min-width:18px;height:18px;border-radius:9px;background:var(--err);color:var(--accent-ink);font-size:11px;font-weight:700;display:inline-flex;align-items:center;justify-content:center}
.hsp{flex:1}.hpre{font-size:13px;color:var(--text-2)}.hpre b{color:var(--text)}
.playbtn{height:38px;padding:0 22px;font-size:15px;display:inline-flex;align-items:center;gap:8px}.playbtn .ic{fill:currentColor;stroke:none;width:16px;height:16px}
.scroll{flex:1;min-height:0;overflow:auto;padding:24px 24px 96px}.page{max-width:900px;margin:0 auto}
.page>h1{font-size:22px;margin-bottom:2px}.page>.lead{color:var(--text-2);margin-bottom:16px}
.chips{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:16px;position:sticky;top:-24px;background:var(--bg);padding:8px 0;z-index:2}
.chips a{padding:4px 12px;border:1px solid var(--line-2);border-radius:14px;color:var(--text-2);text-decoration:none;font-size:13px}.chips a:hover{color:var(--text);background:var(--surface-2)}
.gcard{background:var(--surface);border:1px solid var(--line);border-radius:12px;margin-bottom:16px;overflow:hidden;scroll-margin-top:36px}
.gcard>header{display:flex;gap:12px;align-items:baseline;padding:14px 20px 4px}.gcard>header h2{font-size:16px}.gcard>header p{color:var(--text-3);font-size:13px;flex:1}
.gcard .row{padding:12px 20px}.gcard .row.child{padding-left:44px}.gcard .row.child::before{left:26px}
.gcard>.rows,.gcard .rows{border:0;background:none;border-radius:0}
.gcard details.more{border-top:1px solid var(--line)}.gcard details.more>summary{margin:0;padding:10px 20px;width:auto;list-style:none;display:flex;gap:8px;align-items:center;color:var(--text-2)}
.gcard details.more>summary::before{content:"+";display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid var(--line-2);border-radius:4px;font-weight:700}
.gcard details.more[open]>summary::before{content:"–"}.gcard details.more>summary::-webkit-details-marker{display:none}
.gcard details.more>.rows>.row:first-child{border-top:1px solid var(--line)}
.row .info{width:24px;height:24px;padding:0;border:0;background:none;color:var(--text-3);display:inline-flex;align-items:center;justify-content:center;border-radius:50%}
.row .info:hover{color:var(--info)}.row .info .ic{width:17px;height:17px}
.row.open .rd{display:grid;grid-template-columns:150px 1fr;gap:6px 16px;grid-column:1/-1;background:var(--surface-2);border-radius:var(--r-sm);padding:12px 14px;margin-top:6px}
.row.open .rd dt{margin:0;text-transform:none;letter-spacing:0;font-size:13px}.row.open .rd dd{margin:0}
.gcard.exp{border-color:color-mix(in srgb,var(--exp) 40%,var(--line))}.gcard.exp>header h2{color:var(--exp)}
.gcard.exp .expsw{margin:8px 20px 12px;padding:10px 14px}
.playgrid{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(0,1fr);gap:16px;align-items:start}
.ptiles{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;padding:8px 20px 4px}
.ptile{text-align:left;border:1px solid var(--line-2);border-radius:var(--r);background:var(--bg);padding:10px 12px;display:flex;flex-direction:column;gap:3px;min-height:96px}
.ptile b{font-size:15px}.ptile span{font-size:12.5px;color:var(--text-2);line-height:1.35}.ptile i{font-style:normal;font-size:11.5px;color:var(--ok)}
.ptile:hover{border-color:var(--text-3)}.ptile.on{border-color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}
.pfoot{padding:8px 20px 14px;font-size:13px;color:var(--text-2)}
.bigplay{display:flex;gap:16px;align-items:center;padding:16px 20px}.bigplay .playbtn{height:52px;font-size:18px;padding:0 36px}.bigplay p{color:var(--text-2);font-size:13px}
.pad{padding:8px 14px 10px}.facts{padding:4px 20px 14px;font-size:13px;color:var(--text-2)}
.cbar{position:fixed;left:50%;transform:translateX(-50%);bottom:16px;width:min(760px,calc(100vw - 32px));border:1px solid var(--line-2);border-radius:12px;box-shadow:0 10px 30px #0009;z-index:5}
.job{padding:12px 20px}.hk{padding:12px 20px}.gcard>p.hint{padding:0 20px 14px;margin:0}.rows>p.hint{padding:0 16px 12px;margin:0}.tips{padding:8px 20px 14px 38px}
@media (max-width:900px){.playgrid{grid-template-columns:minmax(0,1fr)}.brand small,.hpre{display:none}.row.open .rd{grid-template-columns:1fr}}
'''
B_JS = r'''
document.addEventListener('click',ev=>{const b=ev.target.closest('.info');if(b){const r=b.closest('.row');r.classList.toggle('open');b.setAttribute('aria-expanded',r.classList.contains('open'));}
 const a=ev.target.closest('.chips a');if(a){ev.preventDefault();document.querySelector(a.getAttribute('href')).scrollIntoView({behavior:'smooth'});}});
function onRow(r){}
'''


def brow(id, child=False, note="", cls=""):
    """Direction B row: same parts, with an inline 'more information' button instead of the side panel."""
    h = row(id, child, note)
    info = f'<button type="button" class="info" aria-expanded="false" aria-label="More about {e(K[id]["label"])}">{ic("info")}</button>'
    h = h.replace('<span class="was">', info + '<span class="was">', 1)
    if cls:
        h = h.replace('class="row', f'class="row {cls}', 1).replace(f'class="row {cls} child"', f'class="row child {cls}"')
    return h


def gcard(cid, title, lead, main, more="", more_label="", extra="", more_id=""):
    m = (f'<details class="more" id="{more_id}"><summary>{more_label}</summary><div class="rows">{more}</div></details>') if more else ""
    return (f'<section class="gcard {extra}" id="{cid}"><header><h2>{title}</h2><p>{lead}</p></header>'
            f'<div class="rows">{main}</div>{m}</section>')


def build_b():
    tabs = [("home", "Home", "home"), ("lighting", "Picture", "picture"), ("performance", "Performance", "performance"), ("help", "Keys & help", "help")]
    tab_html = ""
    for p, t, i in tabs:
        cnt = '<i class="cnt" data-show="missing">1</i>' if p == "home" else ""
        tab_html += f'<button type="button" data-page-link="{p}">{ic(i)}{t}{cnt}</button>'
    tiles = "".join(
        f'<button type="button" class="ptile{" on" if n == "Ultra" else ""}" aria-pressed="{"true" if n == "Ultra" else "false"}"><b>{n}</b>'
        f'<span>{e(d)}</span>{"<i>Suggested for your GPU</i>" if n == "Ultra" else ""}</button>' for n, d in PRESET_DESC)
    home = f'''<div class="page" id="p-home"><div class="playgrid"><div>
<section class="gcard"><header><h2 data-hide="missing">Ready to play</h2><h2 data-show="missing">Game folder needed</h2><p></p></header>
<div class="facts" style="padding-bottom:6px"><span data-hide="missing">Play starts Baldur's Gate 3 (Vulkan) with BG3RTX · NVIDIA GeForce RTX 5090 · 32 GB · driver 621.01</span>
<span data-show="missing">Play is off until BG3RTX knows where the game is installed.</span></div>
<div class="pad">{attention("b")}</div></section>
<section class="gcard"><header><h2>Quality preset</h2><p>Pick one, then fine-tune on Picture if you like.</p></header>
<div class="ptiles" role="group" aria-label="Preset">{tiles}</div>
<div class="pfoot"><b>Custom</b> · Ultra with 3 changes: RTX Hair On, Detail at distance 1/4 (Standard), Strand hair edge resolve Analytic coverage. <button type="button" class="lnk">Reset to Ultra</button></div></section>
</div><div>
<section class="gcard"><header><h2>Preparation</h2><p>Background work. Never blocks Play.</p></header>{shaders()}{conversion()}</section>
<section class="gcard"><header><h2>In-game keys</h2><p></p></header><div class="facts">{keys_list()}<button type="button" class="lnk" data-page-link="help">Change keys</button></div></section>
</div></div></div>'''
    hc = "hairchild"
    exp_head = lambda n: (f'<div class="expsw"><div><div class="rname">Show experimental options</div><div class="rs">Not fully tested. Off in every preset. Turning this off resets them to Off.</div></div>'
                          f'<div class="togw"><button type="button" class="tog expsw-b" role="switch" aria-checked="true" aria-label="Show experimental options"><i></i></button><span class="togl">On</span></div></div>'
                          f'<div class="empty noexp" style="margin:0 20px 16px">{n} hidden on this page.</div>')
    picture = ('<div class="page" id="p-lighting"><h1>Picture</h1><p class="lead">What BG3RTX adds to the image. Presets set all of this; change only what you care about.</p>'
               '<div class="chips"><a href="#g-rt">Ray tracing</a><a href="#g-shadows">Shadows</a><a href="#g-bounce">Bounced light</a><a href="#g-surfaces">Reflections and water</a><a href="#g-hair">Hair</a><a href="#g-dlss5">DLSS 5</a><a href="#g-exp">Experimental</a></div>'
               + gcard("g-rt", "Ray tracing", "The base the other options build on.", brow("rt_geometry") + brow("rr"),
                       brow("rt_intensity"), "1 more option: Mod lighting strength (100%)")
               + gcard("g-shadows", "Shadows", "", brow("sun_shadows") + brow("local_shadows"),
                       brow("box_lights"), "1 more option: Interior light shadows (On)")
               + gcard("g-bounce", "Bounced light", "", brow("gi"), brow("exp_hidden_surface", note=BOUNCE_NOTE),
                       "1 more option: Off-screen bounce light (Far, 6 m)", more_id="m-bounce")
               + gcard("g-surfaces", "Reflections and water", "", brow("reflections") + brow("exp_water"),
                       brow("rr_post_temporal"), "1 more option: Depth of field and bloom smoothing (On)")
               + gcard("g-hair", "Hair", "", brow("exp_native_coverage") + brow("exp_hair_groom_primary", note=HAIR_NOTE)
                       + brow("hair_distant_detail", child=True, cls=hc) + brow("hair_motion_stability", child=True, cls=hc),
                       conversion(), "RTX Hair style conversion: " "559 of 559 ready", more_id="m-conv")
               + gcard("g-dlss5", "DLSS 5", "Off in every preset. Needs your own runtime file.", brow("dlss5_nr"),
                       runtime_file(), "Runtime file: none chosen", more_id="m-dlss5")
               + f'<section class="gcard exp" id="g-exp"><header><h2>Experimental</h2><p>For Picture: water and RTX Hair.</p></header>{exp_head("2 options")}'
                 f'<div class="rows needexp">{brow("water_surface_fresnel")}{brow("exp_hair_strand_aa", note=EXP_DEP_NOTE)}</div></section>'
               + '</div>')
    performance = ('<div class="page" id="p-performance"><h1>Performance</h1><p class="lead">Frame rate and latency. None of this changes how the image looks.</p>'
                   + gcard("g-fps", "Frame rate", "", brow("frame_generation") + brow("reflex") + brow("fps_cap"))
                   + gcard("g-speed", "Tracing speed", "Same image, less GPU time. On in every preset.", "",
                           brow("exp_gi_rt_pipeline") + brow("exp_gi_rt_pipeline_reorder", child=True),
                           "2 options, both on: saves about 3 ms. Turn off only to troubleshoot.", more_id="m-speed")
                   + f'<section class="gcard exp" id="g-pexp"><header><h2>Experimental</h2><p>For Performance: scene handling and Frame Generation tests.</p></header>{exp_head("3 options")}'
                     f'<div class="rows needexp">{brow("scene_handout")}{brow("fg_destroy_wait_exact")}{brow("fg_present_metering")}</div></section>'
                   + '</div>')
    h = help_cards()
    helpp = ('<div class="page" id="p-help"><h1>Keys &amp; help</h1><p class="lead">In-game keys, setup checks and problem reports.</p>'
             '<div class="chips"><a href="#h-keys">In-game keys</a><a href="#h-game">Game and graphics</a><a href="#h-report">Problem reports</a><a href="#h-about">About</a></div>'
             + f'<section class="gcard" id="h-keys"><header><h2>In-game keys</h2><p>Work while you play.</p></header>{hotkeys_editor()}</section>'
             + gcard("h-game", "Game and graphics", "", h["game"])
             + gcard("h-report", "Problem reports", "", h["report"], '<div style="padding:0">' + h["tips"] + "</div>", "Common fixes to try first")
             + gcard("h-about", "About", "", h["about"]) + '</div>')
    body = f'''{mockbar("Direction B · four tabs, grouped cards, more on demand", ("Direction A", "a.html"))}
<header class="bhead"><div class="brand">{GEM}<span>BG3RTX</span><small>0.9.6</small></div>
<nav class="tabs" aria-label="Sections">{tab_html}</nav><span class="hsp"></span>
<span class="hpre">Preset <b>Custom</b> (Ultra + 3)</span>
<button type="button" class="btn pri playbtn" data-hide="missing unsaved dependency">{ic("play")}Play</button>
<button type="button" class="btn pri playbtn" data-show="unsaved dependency">{ic("play")}Save &amp; play</button>
<button type="button" class="btn pri playbtn" data-show="missing" disabled title="Choose the game folder first">{ic("play")}Play</button></header>
<div class="banner err" data-show="missing" role="alert">{ic("warn")}<span><b>Baldur's Gate 3 was not found.</b> Choose the folder that contains bin\\bg3.exe to turn Play on.</span><button type="button" class="btn sm pri">Choose folder…</button></div>
<div class="banner warn" data-show="restart" role="status">{ic("warn")}<span><b>Saved.</b> The game is running: restart it to apply 1 change (Detailed logging). 2 changes are already active.</span><button type="button" class="lnk" data-page-link="help" data-sel="detailed_logging">Show</button></div>
<div class="scroll">{home}{picture}{performance}{helpp}</div>
<footer class="cbar" data-show="unsaved dependency"><i class="dot"></i>
<span class="t" data-show="unsaved"><b>3 unsaved changes</b> · 1 needs a game restart</span>
<span class="t" data-show="dependency"><b>2 unsaved changes</b> · RTX Hair and 1 option that needs it</span>
<button type="button" class="lnk" data-dialog="review">Review</button><button type="button" class="btn" data-state="normal">Discard</button><button type="button" class="btn pri" data-state="restart">Save</button></footer>'''
    return doc("BG3RTX mockup B", B_CSS, body, B_JS)


if __name__ == "__main__":
    (OUT / "a.html").write_text(build_a(), encoding="utf-8", newline="\n")
    (OUT / "b.html").write_text(build_b(), encoding="utf-8", newline="\n")
    for n in ("a.html", "b.html"):
        print(n, (OUT / n).stat().st_size, "bytes")
