"""Perceptual difference against a reference.

Uses NVIDIA's `flip_evaluator` package when it is installed (pip; not
downloaded by this harness). Otherwise a simplified LDR-FLIP: the same
structure (YCxCz spatial filtering, HyAB colour difference in L*a*b* with
FLIP's compression, edge/point feature difference, colour^(1 - feature)) but
Gaussian approximations of the contrast sensitivity filters instead of FLIP's
exact ones. Its values track FLIP's ordering, not its exact numbers.
"""
import numpy as np

from . import imgops as im

try:   # pragma: no cover - not installed on the dev machine
    import flip_evaluator as _flip
except Exception:
    _flip = None

NAME = "FLIP (flip_evaluator)" if _flip else "simplified FLIP (flip_evaluator not installed)"


def _srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


_M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], np.float32)
_WHITE = _M.sum(axis=1)


def _lab(lin):
    xyz = lin @ _M.T / _WHITE
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def _hyab(a, b):
    d = a - b
    return np.abs(d[..., 0]) + np.hypot(d[..., 1], d[..., 2])


def flip(ref, test, ppd=67.0):
    """ref, test: display-referred sRGB in [0, 1], (H, W, 3). Returns the error map."""
    if _flip is not None:
        err, _, _ = _flip.evaluate(ref.astype(np.float32), test.astype(np.float32), "LDR")
        return np.asarray(err, np.float32)
    lr, lt = _srgb_to_lin(ref), _srgb_to_lin(test)
    # Opponent space (YCxCz via XYZ), CSF-like low-pass per channel.
    def ycxcz(lin):
        xyz = lin @ _M.T / _WHITE
        return np.stack([116 * xyz[..., 1] - 16, 500 * (xyz[..., 0] - xyz[..., 1]), 200 * (xyz[..., 1] - xyz[..., 2])], -1)

    def back(o):
        y = (o[..., 0] + 16) / 116
        x = o[..., 1] / 500 + y
        z = y - o[..., 2] / 200
        xyz = np.stack([x, y, z], -1) * _WHITE
        return np.clip(xyz @ np.linalg.inv(_M).T, 0, 1)

    sig = (0.5 * ppd / 67, 1.5 * ppd / 67, 2.0 * ppd / 67)
    fr, ft = ycxcz(lr), ycxcz(lt)
    fr = np.stack([im.gauss(fr[..., c], sig[c]) for c in range(3)], -1)
    ft = np.stack([im.gauss(ft[..., c], sig[c]) for c in range(3)], -1)
    qc, pc, pt = 0.7, 0.4, 0.95
    de = _hyab(_lab(back(fr)), _lab(back(ft))) ** qc
    cmax = _hyab(_lab(np.array([[0.0, 1.0, 0.0]])), _lab(np.array([[0.0, 0.0, 1.0]])))[0] ** qc
    pcc = pc * cmax
    de = np.where(de < pcc, pt / pcc * de, pt + (de - pcc) / (cmax - pcc) * (1 - pt))
    # Feature difference on achromatic lightness (edges: 1st, points: 2nd derivative).
    s = 0.5 * 0.082 * ppd

    def feats(lin):
        L = (_lab(lin)[..., 0] / 100.0).astype(np.float32)
        g = im.gauss(L, s)
        gy, gx = np.gradient(g)
        e = np.hypot(gx, gy)
        gyy, _ = np.gradient(gy)
        _, gxx = np.gradient(gx)
        return e * s, np.abs(gxx + gyy) * s * s

    er, pr = feats(lr)
    et, ptt = feats(lt)
    df = np.clip(np.maximum(np.abs(er - et), np.abs(pr - ptt)) * 4.0 / np.sqrt(2), 0, 1) ** 0.5
    return np.clip(de, 0, 1) ** (1 - df)
