"""Rigid transforms as BG3 stores them: position, unit quaternion (x, y, z, w), uniform scale.

A level object's world position is `R * (s * p) + t` of its parent chain. Sub-levels
(LevelTemplate placements) nest, so a light in BGH_SteelWatchFoundry_B goes through
the foundry's placement in CTY_LowerCity_A and LowerCity's placement in CTY_Main_A.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

Vec3 = tuple[float, float, float]
Quat = tuple[float, float, float, float]


def qmul(a: Quat, b: Quat) -> Quat:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q: Quat, v: Vec3) -> Vec3:
    x, y, z, w = q
    vx, vy, vz = v
    # t = 2 * cross(q.xyz, v); v' = v + w * t + cross(q.xyz, t)
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def qnormalize(q: Quat) -> Quat:
    n = math.sqrt(sum(c * c for c in q))
    if n < 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    return tuple(c / n for c in q)  # type: ignore[return-value]


@dataclass(frozen=True)
class Transform:
    pos: Vec3 = (0.0, 0.0, 0.0)
    rot: Quat = (0.0, 0.0, 0.0, 1.0)
    scale: float = 1.0

    @staticmethod
    def from_node(node) -> "Transform":
        """From a `Transform` child node (Position, RotationQuat, Scale); identity if missing."""
        if node is None:
            return Transform()
        p = node.get("Position") or (0.0, 0.0, 0.0)
        r = node.get("RotationQuat") or (0.0, 0.0, 0.0, 1.0)
        s = node.get("Scale", 1.0)
        if isinstance(s, (list, tuple)):  # a few objects store a vec3 scale
            s = sum(s) / len(s)
        return Transform(tuple(map(float, p)), qnormalize(tuple(map(float, r))), float(s))

    def apply(self, p: Vec3) -> Vec3:
        r = qrot(self.rot, (p[0] * self.scale, p[1] * self.scale, p[2] * self.scale))
        return (r[0] + self.pos[0], r[1] + self.pos[1], r[2] + self.pos[2])

    def compose(self, child: "Transform") -> "Transform":
        """This transform applied after `child` (parent * child)."""
        return Transform(self.apply(child.pos), qnormalize(qmul(self.rot, child.rot)), self.scale * child.scale)

    def is_identity(self) -> bool:
        return (all(abs(c) < 1e-6 for c in self.pos) and abs(self.rot[3]) > 1 - 1e-7 and abs(self.scale - 1) < 1e-7)


def quat_forward(q: Quat) -> Vec3:
    """The object's local +Z axis in world space (BG3 lights and probes look along +Z)."""
    return qrot(q, (0.0, 0.0, 1.0))
