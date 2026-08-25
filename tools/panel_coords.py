#!/usr/bin/env python3
"""Read true component positions out of a panel SVG, in millimetres.

Rack positions widgets from code, so the panel drawing and the widget
coordinates have to be kept in agreement by hand. Transcribing them is where it
goes wrong, and it went wrong twice here:

  * The viewBox may not be 1 user unit per mm. These panels are
    `width="70.76mm" viewBox="0 0 7076 12850"`, i.e. 1 uu = 0.01 mm.
  * Inkscape puts a `transform` on layers. The `components` layer carried
    `translate(120.35222,-1592.0584)`, so raw cx/cy were 1.20 mm left and
    15.92 mm below where the guides actually sat — the widgets rendered low
    and misaligned while the numbers looked plausible.

So compute it instead of reading it: accumulate every ancestor transform and
scale by the viewBox, then print mm ready to paste into PanelLayout.h.

    python tools/panel_coords.py panel-src/AlloyPlatform.svg [layer]

`layer` defaults to `components`. Shapes are identified by their
`inkscape:label`, so name each guide after what it is (pot1, led3, out_l).
"""

import re
import sys
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
INKSCAPE = "http://www.inkscape.org/namespaces/inkscape"
LABEL = f"{{{INKSCAPE}}}label"


def parse_transform(s):
    """Return (a, b, c, d, e, f) for the SVG transform list in `s`.

    Only the forms Inkscape actually emits on groups: translate, scale, matrix.
    A rotate would need trig and has never appeared here; it raises rather than
    silently producing plausible-but-wrong numbers, which is the failure mode
    this whole script exists to prevent.
    """
    m = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    if not s:
        return m
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s):
        v = [float(x) for x in re.split(r"[,\s]+", args.strip()) if x]
        if name == "translate":
            t = (1.0, 0.0, 0.0, 1.0, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == "scale":
            sx = v[0]
            sy = v[1] if len(v) > 1 else sx
            t = (sx, 0.0, 0.0, sy, 0.0, 0.0)
        elif name == "matrix":
            t = tuple(v[:6])
        else:
            raise SystemExit(f"unsupported transform '{name}' — extend this script")
        m = compose(m, t)
    return m


def compose(m, n):
    """m then n, as SVG applies them (parent outermost)."""
    a1, b1, c1, d1, e1, f1 = m
    a2, b2, c2, d2, e2, f2 = n
    return (
        a1 * a2 + c1 * b2,
        b1 * a2 + d1 * b2,
        a1 * c2 + c1 * d2,
        b1 * c2 + d1 * d2,
        a1 * e2 + c1 * f2 + e1,
        b1 * e2 + d1 * f2 + f1,
    )


def apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


def centre(el):
    """Centre of a shape in its own coordinate system, or None."""
    tag = el.tag.split("}")[-1]
    g = el.get
    if tag in ("circle", "ellipse"):
        return float(g("cx", 0)), float(g("cy", 0))
    if tag == "rect":
        return (
            float(g("x", 0)) + float(g("width", 0)) / 2.0,
            float(g("y", 0)) + float(g("height", 0)) / 2.0,
        )
    return None


def walk(el, m, out):
    m = compose(m, parse_transform(el.get("transform")))
    c = centre(el)
    if c is not None:
        out.append((el.get(LABEL) or el.get("id") or "?", *apply(m, *c)))
    for child in el:
        walk(child, m, out)


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    src = argv[1]
    want = argv[2] if len(argv) > 2 else "components"

    tree = ET.parse(src)
    root = tree.getroot()

    # mm per user unit, from width vs viewBox. Without this the numbers are out
    # by whatever Inkscape happened to pick for the document scale.
    wmm = float(re.sub(r"[^\d.]", "", root.get("width", "0")) or 0)
    vb = [float(x) for x in re.split(r"[,\s]+", root.get("viewBox", "").strip()) if x]
    if not wmm or len(vb) != 4 or not vb[2]:
        raise SystemExit("need both a width in mm and a viewBox to derive the scale")
    mm_per_uu = wmm / vb[2]

    # Every match, not the first. Inkscape happily allows two layers with the
    # same label, and picking the first silently answers from whichever the
    # document happens to list earlier — which is how a stale 21-shape guide
    # layer left over from before the LEDs existed came to shadow the real
    # 28-shape one. A wrong coordinate table that looks right is the single
    # most expensive failure this script can have, so this is fatal.
    layers = [g for g in root.iter(f"{{{SVG}}}g") if g.get(LABEL) == want]
    if not layers:
        raise SystemExit(f"no layer labelled '{want}' in {src}")
    if len(layers) > 1:
        ids = ", ".join(repr(g.get("id")) for g in layers)
        counts = ", ".join(str(len(list(g))) for g in layers)
        raise SystemExit(
            f"{src}: {len(layers)} layers are labelled '{want}' ({ids}; "
            f"{counts} shapes each).\n"
            f"Delete the stale one in Inkscape — this script cannot know which "
            f"you meant, and guessing would produce a plausible wrong answer."
        )
    layer = layers[0]

    # Duplicate guide names inside the layer are the same trap one level down.
    seen = {}
    for el in layer.iter():
        name = el.get(LABEL)
        if name and centre(el) is not None:
            seen[name] = seen.get(name, 0) + 1
    dupes = sorted(n for n, c in seen.items() if c > 1)
    if dupes:
        raise SystemExit(
            f"{src}: duplicate guide label(s) in '{want}': {', '.join(dupes)}.\n"
            f"Each guide names one position; two shapes sharing a name means "
            f"one of them is unnamed by accident."
        )

    # Start from the layer's own transform: walk() composes it in, and anything
    # above a top-level Inkscape layer is the root, which has none.
    found = []
    walk(layer, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0), found)

    print(f"# {src}  layer '{want}'")
    print(f"# width {wmm} mm / viewBox {vb[2]} uu  ->  1 uu = {mm_per_uu:g} mm")
    print(f"# {len(found)} shapes\n")
    for name, x, y in sorted(found, key=lambda r: r[0]):
        print(f"    {{{x * mm_per_uu:8.3f}f, {y * mm_per_uu:8.3f}f}},  // {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
