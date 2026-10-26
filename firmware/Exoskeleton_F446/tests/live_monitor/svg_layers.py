"""Move every matplotlib line2d group into one SVG layer.

matplotlib writes each curve into its own <g id="line2d_N">. Editors then show
one layer per curve, which makes the plot awkward to restyle. This helper
collects them into a single <g id="curves"> layer that is appended last, so the
curves still draw on top of the shaded phase bands.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

SVG_NS = "http://www.w3.org/2000/svg"
INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape"


def single_curve_layer(path: Path) -> int:
    ET.register_namespace("", SVG_NS)
    ET.register_namespace("inkscape", INKSCAPE_NS)
    ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")

    tree = ET.parse(path)
    root = tree.getroot()
    parents = {child: parent for parent in root.iter() for child in parent}

    curves = [element for element in root.iter()
              if (element.get("id") or "").startswith("line2d_")]
    if not curves:
        return 0
    for element in curves:
        parent = parents.get(element)
        if parent is not None:
            parent.remove(element)

    layer = ET.Element(f"{{{SVG_NS}}}g",
                       {"id": "curves",
                        f"{{{INKSCAPE_NS}}}groupmode": "layer",
                        f"{{{INKSCAPE_NS}}}label": "curves"})
    for element in curves:
        for child in list(element):
            layer.append(child)
    root.append(layer)
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return len(curves)
