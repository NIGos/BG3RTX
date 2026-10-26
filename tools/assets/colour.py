"""Colour helpers: sRGB transfer and black-body (Kelvin) chromaticity.

The engine converts `Kelvin` with its own `Blackbody_Lut.dds` (Engine.pak); this is the
analytic Planck locus (Kim et al. 2002 cubic fit in CIE 1931 xy) mapped to linear
Rec.709, normalised so the largest channel is 1. It is used for joins and as a
reference, not to reproduce the engine's values bit for bit.
"""
from __future__ import annotations


def srgb_to_linear(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def kelvin_xy(t: float) -> tuple[float, float]:
    t = min(max(t, 1667.0), 25000.0)
    if t <= 4000.0:
        x = -0.2661239e9 / t ** 3 - 0.2343589e6 / t ** 2 + 0.8776956e3 / t + 0.179910
    else:
        x = -3.0258469e9 / t ** 3 + 2.1070379e6 / t ** 2 + 0.2226347e3 / t + 0.240390
    if t <= 2222.0:
        y = -1.1063814 * x ** 3 - 1.34811020 * x ** 2 + 2.18555832 * x - 0.20219683
    elif t <= 4000.0:
        y = -0.9549476 * x ** 3 - 1.37418593 * x ** 2 + 2.09137015 * x - 0.16748867
    else:
        y = 3.0817580 * x ** 3 - 5.87338670 * x ** 2 + 3.75112997 * x - 0.37001483
    return x, y


def kelvin_to_linear_rgb(t: float) -> tuple[float, float, float]:
    """Linear Rec.709 colour of a black body at `t` Kelvin, max channel = 1."""
    x, y = kelvin_xy(t)
    X, Y, Z = x / y, 1.0, (1.0 - x - y) / y
    r = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z
    g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z
    b = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z
    r, g, b = max(r, 0.0), max(g, 0.0), max(b, 0.0)
    m = max(r, g, b) or 1.0
    return r / m, g / m, b / m
