"""Reading a trace's dumps back into linear values (layer/dump.cpp previews).

Preview encodings (dump.cpp `process`): HDR formats -> Reinhard v/(1+v) then
gamma 1/2.2; UNORM colour as is; one channel -> min..max stretch; two channels
-> 0.5 + 0.5 v / max|v|. A *.f32 next to the PNG (dump_raw=1, where the layer
has it) is used instead: float32 RGBA at the dump's full size.
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image

HDR = {"R11G11B10F", "RGBA16F", "RGB9E5", "RGBA32F"}

# Passes by first fragment-shader hash (docs/FRAME_MAP.md).
PASS_TONEMAP = "d25b8086"   # output-res RGB10A2, HDR10 PQ: the frame as presented, before UI
PASS_COMPOSE = "5c3fdabc"   # render-res R11G11B10F, lit HDR before transparents and post
PASS_DIFFUSE = ("d1669194", "7b44a567")   # writes into the lit diffuse (b10) after the tiled lighting
PASS_SUNMASK = "0687fa32"   # R8, the game's screen-space sun shadow mask (set 1 b7), before our fold
# The G-buffer pass is found by its signature, not a hash: a pass's hash is its first draw's
# fragment shader, which changes with the scene (aa4efa4d in the foundry, 181045cc at camp).


class Trace:
    def __init__(self, path: Path):
        self.path = Path(path)
        self.dumps = json.loads((self.path / "dumps.json").read_text())
        self._cache = {}

    def nonfinite(self):
        return sum(int(d.get("nonfinite") or 0) for d in self.dumps.values())

    def nonfinite_by_dump(self):
        return {k: (d["fmt"], d["pass_fs"][:8], int(d["nonfinite"])) for k, d in self.dumps.items() if d.get("nonfinite")}

    def find(self, pass_fs, fmt=None, slot=0, last=True):
        if isinstance(pass_fs, str):
            pass_fs = (pass_fs,)
        for p in pass_fs:
            c = [(int(k[1:]), k) for k, d in self.dumps.items() if d["pass_fs"].startswith(p)
                 and (fmt is None or d["fmt"] == fmt) and d["slot"] == slot]
            if c:
                return sorted(c)[-1 if last else 0][1]
        return None

    def gbuffer_fs(self):
        """pass_fs of the first G-buffer pass: RG16F normals in slot 0 and RGBA8 in slots 1-3."""
        sig = {}
        for k, d in self.dumps.items():
            sig.setdefault(d["pass_fs"], {})[d["slot"]] = (d["fmt"], int(k[1:]))
        c = [min(i for _, i in s.values()) for p, s in sig.items()
             if s.get(0, ("",))[0] == "RG16F" and all(s.get(j, ("",))[0] == "RGBA8" for j in (1, 2, 3))]
        if not c:
            return None
        first = min(c)
        return next(p for p, s in sig.items() if min(i for _, i in s.values()) == first)

    def final_key(self):
        k = self.find(PASS_TONEMAP, "RGB10A2")
        if k:
            return k
        c = [(d["w"] * d["h"], int(k[1:]), k) for k, d in self.dumps.items() if d["fmt"] == "RGB10A2"]
        return max(c)[2] if c else None

    def load(self, key, alpha=False):
        """Linear channel values (H, W, C) float32 of one dump."""
        ck = (key, alpha)
        if ck in self._cache:
            return self._cache[ck]
        d = self.dumps[key]
        png = self.path / d["png"]
        raw = png.with_suffix(".f32")
        if raw.exists() and not alpha:
            a = np.fromfile(raw, np.float32).reshape(d["h"], d["w"], 4)
        else:
            if alpha:
                png = png.with_name(png.stem + "_a.png")
                if not png.exists():   # alpha did not vary: the dump's constant value
                    v = d["min"][3] if len(d["min"]) > 3 else 1.0
                    base = Image.open(self.path / d["png"])
                    return np.full((base.height, base.width, 1), v, np.float32)
            p = np.asarray(Image.open(png).convert("RGB"), np.float32) / 255.0
            a = self._unpreview(p, d, alpha)
        self._cache[ck] = a
        return a

    @staticmethod
    def _unpreview(p, d, alpha):
        ch = len(d["min"])
        if alpha:
            return p[..., :1]
        if d["fmt"] in HDR:
            t = np.clip(p ** 2.2, 0, 0.9995)
            return t / (1.0 - t)
        if d["slot"] == "depth" or ch == 1:
            lo, hi = d["min"][0] or 0.0, d["max"][0] or 0.0
            return lo + p[..., :1] * (hi - lo)
        if ch == 2:
            am = [max(abs(d["min"][c] or 0), abs(d["max"][c] or 0)) + 1e-20 for c in range(2)]
            return np.stack([(p[..., c] - 0.5) * 2 * am[c] for c in range(2)], -1)
        return p


def pq_to_linear(e):
    """SMPTE ST 2084 code value (0..1) -> linear, 1.0 = 10000 nits."""
    m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
    p = np.power(np.clip(e, 0, 1), 1 / m2)
    return np.power(np.maximum(p - c1, 0) / (c2 - c3 * p), 1 / m1)


def lum709(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def lum2020(rgb):
    return rgb[..., 0] * 0.2627 + rgb[..., 1] * 0.6780 + rgb[..., 2] * 0.0593


class Frame:
    """The views of one trace the detectors use (loaded lazily)."""

    def __init__(self, trace: Trace, pq=True):
        self.t = trace
        self.pq = pq

    def final_code(self):
        """Final frame in its encoded form (PQ code values): perceptually uniform."""
        return self.t.load(self.t.final_key())[..., :3]

    def final_linear(self):
        c = self.final_code()
        return pq_to_linear(c) * 10000.0 if self.pq else np.power(c, 2.2) * 100.0   # nits

    def compose(self):
        k = self.t.find(PASS_COMPOSE, "R11G11B10F")
        return self.t.load(k) if k else None

    def diffuse(self):
        k = self.t.find(PASS_DIFFUSE, "R11G11B10F")
        return (self.t.load(k), self.t.dumps[k]["pass_fs"][:8]) if k else (None, None)

    def sun_mask(self):
        k = self.t.find(PASS_SUNMASK, "R8")
        return self.t.load(k)[..., 0] if k else None

    def depth(self):
        g = self.t.gbuffer_fs()
        k = self.t.find(g, slot="depth") if g else None
        return self.t.load(k)[..., 0] if k else None

    def albedo(self):
        """G-buffer base colour (RT1): independent of every lighting setting, so it
        tells whether two traces show the same view."""
        g = self.t.gbuffer_fs()
        k = self.t.find(g, "RGBA8", slot=1) if g else None
        return self.t.load(k)[..., :3] if k else None

    def normal(self):
        """G-buffer normal (RT0, RG16F stereographic, view space): moves with the geometry
        even where the albedo does not change (a grass blade in front of grass)."""
        g = self.t.gbuffer_fs()
        k = self.t.find(g, "RG16F", slot=0) if g else None
        if not k:
            return None
        # The 8-bit preview scales each channel by its max |value|, and sky pixels hold the
        # encoder's huge sentinel (gi_trace_rt.hlsl: |e|^2 > 5.4e10), so one code step can be
        # larger than any real normal change. detectors.animated() measures the code step from
        # the data and adds it to its bound, so such a preview simply marks nothing.
        return self.t.load(k)[..., :2]

    def is_raw(self, which):
        """True when the final frame ("final") or the lighting buffer ("compose") is read
        from a float32 dump instead of an 8-bit preview (no preview rounding)."""
        k = self.t.final_key() if which == "final" else self.t.find(PASS_COMPOSE, "R11G11B10F")
        return bool(k) and (self.t.path / self.t.dumps[k]["png"]).with_suffix(".f32").exists()

    def material_class(self):
        g = self.t.gbuffer_fs()
        k = self.t.find(g, "RGBA8", slot=2) if g else None
        if not k:
            return None
        a = self.t.load(k, alpha=True)[..., 0]
        return (np.round(a * 255).astype(np.int32) & 7)
