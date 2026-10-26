"""LSX (XML Larian resource) reader, decoding to the same Node tree as lsf.py.

  <save><version .../><region id="R"><node id="N">
      <attribute id="A" type="T" value="V"/>   (TranslatedString: handle="..." version="...")
      <children><node ...>...</node></children>
  </node></region></save>

`type` is a name ("FixedString", "fvec3", "guid"...) or, in old files, a numeric id.
"""
from __future__ import annotations

import xml.etree.ElementTree as ET

from .lsf import TYPE_NAMES
from .resource import Node, Resource

_NAME_TO_ID = {v.lower(): k for k, v in TYPE_NAMES.items()}
_INT = {1, 2, 3, 4, 5, 24, 26, 27, 32}
_FLOAT = {6, 7}
_IVEC = {8, 9, 10}
_FVEC = {11, 12, 13, 14, 15, 16, 17, 18}


def _value(tid: int, el: ET.Element):
    v = el.get("value")
    if tid in (28, 33):
        return {"handle": el.get("handle", ""), "version": int(el.get("version", "0") or 0)}
    if v is None:
        return None
    if tid in _INT:
        return int(v)
    if tid in _FLOAT:
        return float(v)
    if tid == 19:
        return v.strip().lower() in ("true", "1")
    if tid in _IVEC:
        return [int(x) for x in v.split()]
    if tid in _FVEC:
        return [float(x) for x in v.split()]
    return v


def _node(el: ET.Element, parent: Node | None) -> Node:
    n = Node(el.get("id", ""), parent)
    for a in el.findall("attribute"):
        t = a.get("type", "")
        tid = int(t) if t.isdigit() else _NAME_TO_ID.get(t.lower(), 20)
        n.attrs[a.get("id", "")] = _value(tid, a)
    for ch in el.findall("children"):
        for c in ch.findall("node"):
            n.children.append(_node(c, n))
    return n


def parse(data: bytes | str) -> Resource:
    root = ET.fromstring(data)
    ver = root.find("version")
    major = int(ver.get("major", "0")) if ver is not None else 0
    regions = []
    for reg in root.findall("region"):
        for top in reg.findall("node"):
            n = _node(top, None)
            if n.name != reg.get("id"):
                wrapper = Node(reg.get("id", ""))
                n.parent = wrapper
                wrapper.children.append(n)
                n = wrapper
            regions.append(n)
    return Resource(regions, major)
