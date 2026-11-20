"""Offline launcher renderer, contrast and reflow contract. No browser dependency."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def luminance(hex_color):
    rgb = [int(hex_color[i:i+2], 16) / 255 for i in (1, 3, 5)]
    linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in rgb]
    return sum(a*b for a, b in zip(linear, (.2126, .7152, .0722)))


def contrast(a, b):
    x, y = sorted((luminance(a), luminance(b)))
    return (y + .05) / (x + .05)


def main():
    css = (ROOT / 'tools/launcher/ui/app.css').read_text(encoding='utf-8')
    tokens = dict(re.findall(r'--([\w-]+):\s*(#[0-9a-fA-F]{6})\b', css))
    ratios = []
    for fg in ('text', 'text-2', 'text-3', 'gold', 'gold-hi', 'teal', 'warn', 'err'):
        for bg in ('bg', 'bg-2', 'panel', 'panel-hi'):   # every surface text sits on
            ratio = contrast(tokens[fg], tokens[bg])
            assert ratio >= 4.5, (fg, bg, ratio)
            ratios.append(ratio)
    for fg in ('ok', 'exp'):
        assert contrast(tokens[fg], tokens['panel']) >= 4.5, fg
    # Selected choices and Play: dark ink on gold. Tinted badges and banners keep their text readable.
    for fg, bg in ((tokens['gold-ink'], tokens['gold']), (tokens['gold-ink'], tokens['warn']), (tokens['gold-ink'], tokens['err']),
                   (tokens['exp'], tokens['exp-bg']), (tokens['teal'], tokens['info-bg']), (tokens['ok'], tokens['ok-bg']),
                   (tokens['text'], tokens['warn-bg']), (tokens['text'], tokens['err-bg']), (tokens['text-2'], tokens['panel-3'])):
        assert contrast(fg, bg) >= 4.5, (fg, bg)
    # Control borders are told from their panel (WCAG 1.4.11, 3:1).
    assert contrast(tokens['line'], tokens['panel']) >= 3, 'control border'
    print(f'Token text contrast minimum {min(ratios):.2f}:1; selected, Play, badge and border contrast pass.', flush=True)
    # Rule fallback: no layout engine is installed. No fixed width text boxes;
    # wrapping controls and short-viewport scroll avoid clipped 150/200% zoom.
    for selector in ('.row', '.row-text', '.stat', '.description', '.help-copy', '.status', '.rows'):
        for body in re.findall(re.escape(selector) + r'\s*\{([^{}]*)\}', css):
            assert not re.search(r'(?<![-\w])width\s*:\s*\d+px', body), (selector, body)
    native = (ROOT / 'tools/launcher/ui_product.h').read_text(encoding='utf-8')
    assert 'if(entry.first==5&&!g.dev)continue;' in native
    assert 'st::knob_visible(k,g.dev,opt_in())' in native
    assert 'CLEAN' not in native
    host = (ROOT / 'tools/launcher/web_host.cpp').read_text(encoding='utf-8')
    assert 'auto owner=std::make_shared<Host>()' in host
    assert '[&h,ui](HRESULT' not in host
    assert host.count('[owner,ui](HRESULT') == 2
    assert 'if(h.closed)return S_OK' in host
    assert 'if(h.closed){if(controller)controller->Close();return S_OK;}' in host
    assert 'if(!created){h.closed=true;h.failed=true;}' in host
    assert 'if(h.failed||profile.empty())' in host
    # A page that never loads opens the native launcher for the player (failed -> -1), not an empty exit.
    assert 'else if(w==3){h->result=2;h->failed=h->shot.empty();' in host
    for label in ('Lighting', 'Surfaces', 'Performance', 'Help & support'):
        assert 'L"' + label + '"' in native
    assert 'g.tab=g_tab_ids[' in native and 'BS_AUTORADIOBUTTON|BS_PUSHLIKE' in native
    assert 'if(g.st.values[i]<0||!st::knob_visible' not in native
    assert '&&g.st.values[i]>=0)custom' not in native
    # Choice labels never break inside a word; a row too narrow for its choices wraps them and, below 860 px, puts
    # the control under the text. One look at every width: no dropdown fallback.
    seg = re.search(r'\.seg button \{([^}]*)\}', css).group(1)
    assert 'white-space: nowrap' in seg and 'word-break: keep-all' in seg and 'flex: none' in seg
    assert re.search(r'\.seg \{[^}]*flex-wrap: wrap', css)
    assert not re.search(r'\.seg button[^{]*\{[^}]*white-space:\s*normal', css)
    assert '@media (max-height: 560px)' in css and 'main { overflow: visible; }' in css
    for width in (1100, 860, 720):
        assert f'@media (max-width: {width}px)' in css, width
    assert '.row.selected > .row-detail { display: block; }' in css   # the detail panel moves under the row
    assert '@media (prefers-reduced-motion: reduce)' in css
    forced = css[css.rfind('@media (forced-colors: active)'):]
    assert ':is(button,input,select,textarea,summary,a):focus-visible { outline-color: Highlight; }' in forced
    js = (ROOT / 'tools/launcher/ui/app.js').read_text(encoding='utf-8')
    assert 'fitChoices' not in js and 'choice-select' not in js
    # Inline icons in one stroke style; no ornament glyph images.
    assert 'ornaments/glyph-' not in js and "stroke-width: 1.75" in css
    node = shutil.which('node')
    assert node, 'Node.js required for the shipped renderer regression test'
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/launcher_web_bridge_test.exe'
    state = subprocess.check_output([str(exe), '--json'])
    with tempfile.TemporaryDirectory() as tmp:
        opt_in = Path(tmp) / 'experimental.json'
        opt_in.write_bytes(subprocess.check_output([str(exe), '--json', 'experimental']))
        subprocess.run([node, 'tests/launcher_ui_test.js', str(opt_in)], input=state, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
