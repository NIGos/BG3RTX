"""Offline launcher renderer, contrast and reflow contract. No browser dependency."""
from pathlib import Path
import re
import shutil
import subprocess
import sys

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
        for bg in ('bg', 'bg-2', 'panel', 'panel-hi'):
            ratio = contrast(tokens[fg], tokens[bg])
            assert ratio >= 4.5, (fg, bg, ratio)
            ratios.append(ratio)
    for fg, bg in (('#1a1610', tokens['gold']), ('#fff1d8', '#92502e'), ('#efb888', tokens['panel']), ('#e2d6c1', '#171b20')):
        assert contrast(fg, bg) >= 4.5, (fg, bg)
    print(f'Token text contrast minimum {min(ratios):.2f}:1; selected and Play text pass.', flush=True)
    # Rule fallback: no layout engine is installed. No fixed width text boxes;
    # wrapping controls and short-viewport scroll avoid clipped 150/200% zoom.
    for selector in ('.card', '.stat', '.hero-copy', '.description', '.help-copy', '.status'):
        for body in re.findall(re.escape(selector) + r'\s*\{([^{}]*)\}', css):
            assert not re.search(r'(?<![-\w])width\s*:\s*\d+px', body), (selector, body)
    native = (ROOT / 'tools/launcher/ui_product.h').read_text(encoding='utf-8')
    assert 'if((entry.first==3||entry.first==5)&&!g.dev)continue;' in native
    for label in ('Lighting', 'Surfaces', 'Performance', 'Help & support'):
        assert 'L"' + label + '"' in native
    assert 'g.tab=g_tab_ids[' in native and 'BS_AUTORADIOBUTTON|BS_PUSHLIKE' in native
    assert 'if(g.st.values[i]<0||!st::knob_visible' not in native
    assert '&&g.st.values[i]>=0)custom' not in native
    # Choice labels never break inside a word; narrow cards switch to the dropdown instead.
    seg = re.search(r'\.seg button \{([^}]*)\}', css).group(1)
    assert 'white-space: nowrap' in seg and 'word-break: keep-all' in seg and 'flex: none' in seg
    assert '.control.choices.compact .select-wrap { display: block;' in css
    assert not re.search(r'\.seg button[^{]*\{[^}]*white-space:\s*normal', css)
    assert '@media (max-height: 560px)' in css and 'main { overflow: visible; }' in css
    assert '@media (max-width: 720px)' in css
    assert '@media (prefers-reduced-motion: reduce)' in css
    forced = css[css.rfind('@media (forced-colors: active)'):]
    assert ':is(button,input,select,textarea,summary,a):focus-visible { outline-color: Highlight; }' in forced
    js = (ROOT / 'tools/launcher/ui/app.js').read_text(encoding='utf-8')
    assert 'function fitChoices()' in js and "addEventListener('resize',fitChoices)" in js
    # Inline icons in one stroke style; no ornament glyph images.
    assert 'ornaments/glyph-' not in js and "stroke-width: 1.75" in css
    node = shutil.which('node')
    assert node, 'Node.js required for the shipped renderer regression test'
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/launcher_web_bridge_test.exe'
    state = subprocess.check_output([str(exe), '--json'])
    subprocess.run([node, 'tests/launcher_ui_test.js'], input=state, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
