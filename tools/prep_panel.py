#!/usr/bin/env python3
"""Strip guide layers from a panel SVG before Inkscape converts it.

Panels carry construction layers — `components` holds a shape per pot, jack and
LED so the widget coordinates can be read off the drawing. Those must not ship:
Rack would render them on top of the finished panel.

Removing the group outright rather than setting `display:none` on it, for two
reasons: the shipped file gets smaller, and it does not depend on Rack's SVG
parser honouring `display` — which it does today, but that is a detail of a
vendored parser rather than a promise.

This has to run *before* Inkscape, because the layers are identified by
`inkscape:label` and `--export-plain-svg` drops the whole `inkscape:` namespace
on the way out.

    python tools/prep_panel.py [--keep-hidden] in.svg out.svg [label ...]

With no labels given, strips `components`.

It also drops groups the drawing has switched off (`display:none`), which the
named-layer list cannot cover: a layer stays in the file when you uncheck its
eye in Inkscape, and `texture` on ChaosForge is 815 tiles of hidden geometry —
4.9 MB that nothing ever draws. `--keep-hidden` leaves them in.

It also flattens `<use>` clones, for two independent reasons.

Rack's nanosvg parses `svg g path rect circle ellipse line polyline polygon`
and the gradient elements — that is the whole list. A `<use>` draws nothing at
all there, so a clone has to become real geometry somewhere or it does not ship.

Inkscape *can* do it, as part of `object-to-path`, and that is what the build
used to ask for. It does not scale: unlinking a clone rebuilds the document, so
a tiled-clone texture costs one rebuild per tile. A panel carrying 1927 tiles of
a 6 KB logo path did not finish inside a five-minute wall clock, where the same
expansion here is a dictionary lookup and a deep copy per clone — under a
second. That is also why the Inkscape action list is now text-only: text needs
font metrics and genuinely belongs to Inkscape; clones never did.

⚠ Flattening multiplies file size by the number of clones, because SVG has no
way to say "this shape again" that nanosvg understands. The `texture` layer's
815 tiles take a Forge panel from 0.32 MB to 5.2 MB, which is then parsed on
every module instantiation and shipped inside the .vcvplugin. The tool reports
the growth so it cannot happen quietly; if the number is alarming, the fix is
fewer or simpler tiles in the drawing, not a cleverer exporter.

There is also a post-Inkscape check:

    python tools/prep_panel.py --check out.svg

which reports any <text> that survived object-to-path *and would actually draw
something*. Empty text objects are ignored: Inkscape leaves them as <text>
because there is no glyph to convert, and a `grep -q "<text"` therefore cried
wolf on every build over one stray empty label in the drawing.
"""

import copy
import os
import sys
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
INKSCAPE = "http://www.inkscape.org/namespaces/inkscape"
SODIPODI = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd"
XLINK = "http://www.w3.org/1999/xlink"

LABEL = f"{{{INKSCAPE}}}label"
GROUP = f"{{{SVG}}}g"
USE = f"{{{SVG}}}use"
HREF = f"{{{XLINK}}}href"


def strip(parent, labels, removed):
    """Depth-first, so a guide layer nested inside another group is still found."""
    for child in list(parent):
        if child.tag == GROUP and child.get(LABEL) in labels:
            removed.append(child.get(LABEL))
            parent.remove(child)
            continue
        strip(child, labels, removed)


def text_content(el):
    """All character data under `el`, whitespace collapsed."""
    return "".join(el.itertext()).strip()


# ---------------------------------------------------------------------------
# Hidden layers
# ---------------------------------------------------------------------------


def _is_hidden(el):
    """True if `el` carries display:none, in either spelling.

    Inkscape writes the `style` form when you uncheck a layer's eye; the bare
    presentation attribute is equally valid SVG and Rack's parser reads either,
    so both have to be recognised here.
    """
    if el.get("display", "").strip() == "none":
        return True
    for decl in el.get("style", "").split(";"):
        name, _, value = decl.partition(":")
        if name.strip() == "display" and value.strip() == "none":
            return True
    return False


def drop_hidden(parent, dropped):
    """Remove groups the drawing has switched off (`display:none`).

    Distinct from `strip`, which has to know a layer's name in advance. This
    asks the same question from the other side — what is the artwork not
    showing today — and catches what the fixed list cannot. ChaosForge's
    `texture` is the case in hand: hidden in the drawing, 815 tiles once
    flattened, 4.9 MB that Rack parses on every module instantiation and never
    draws.

    ⚠ Runs AFTER `expand_uses`, not before. A tiled clone set keeps the
    original it tiles inside the layer doing the tiling, so dropping the layer
    first leaves every <use> in the document pointing at an id that is no
    longer there — 815 "not in this document" warnings and a texture that is
    blank rather than absent. Expanding first pays for a temporary copy of
    geometry about to be discarded, which is well under a second.
    """
    for child in list(parent):
        if child.tag == GROUP and _is_hidden(child):
            dropped.append(child.get(LABEL) or child.get("id") or "?")
            parent.remove(child)
            continue
        drop_hidden(child, dropped)


# ---------------------------------------------------------------------------
# <use> flattening
# ---------------------------------------------------------------------------

# Attributes that belong to the <use> element itself rather than to the content
# it generates. Everything else — style, fill, opacity, class — is carried onto
# the wrapper group, which is where SVG puts it: the referenced content inherits
# from the <use>, so a property the target sets for itself still wins.
_USE_OWN = {"x", "y", "width", "height", "transform", "id", "href", HREF}


def _editor_cruft(el):
    """Drop inkscape:/sodipodi: attributes from a copy.

    `export-plain-svg` would drop them at the end anyway, but a tiled clone set
    carries them on every tile — the tile-cx/cy/w/h bookkeeping is ~400 bytes a
    copy — and paying that 815 times makes the file Inkscape has to *parse*
    much larger for no effect on the output.
    """
    for e in el.iter():
        for k in [k for k in e.attrib if k.startswith(("{" + INKSCAPE, "{" + SODIPODI))]:
            del e.attrib[k]


def _instantiate(use, target):
    """The group a <use> stands for: transform, then x/y, wrapping a copy."""
    g = ET.Element(GROUP)

    # Order matters and is the spec's: the element's own transform applies
    # first, and the x/y offset is appended *after* it, i.e. innermore.
    parts = []
    if use.get("transform"):
        parts.append(use.get("transform"))
    x, y = use.get("x", "0"), use.get("y", "0")
    try:
        if float(x) or float(y):
            parts.append(f"translate({x},{y})")
    except ValueError:
        parts.append(f"translate({x},{y})")  # not a plain number; let it through
    if parts:
        g.set("transform", " ".join(parts))

    for k, v in use.attrib.items():
        if k not in _USE_OWN:
            g.set(k, v)

    clone = copy.deepcopy(target)
    # The copy must not carry the original's id: every tile would repeat it, and
    # a duplicate id makes any url(#…) reference in the document ambiguous.
    for e in clone.iter():
        e.attrib.pop("id", None)
    _editor_cruft(clone)
    g.append(clone)
    return g


def expand_uses(root, max_passes=8):
    """Replace every <use> with the geometry it clones. Returns the count.

    Iterative because a clone of a clone is legal: each pass resolves the
    references that exist at its start, and a document that is still not clean
    after `max_passes` is either very deeply nested or contains a reference
    cycle — either way, stopping beats spinning.
    """
    total = 0
    for _ in range(max_passes):
        parents = {c: p for p in root.iter() for c in p}
        uses = [e for e in root.iter(USE)]
        if not uses:
            break
        by_id = {e.get("id"): e for e in root.iter() if e.get("id")}
        done = 0
        for use in uses:
            href = use.get(HREF) or use.get("href") or ""
            target = by_id.get(href[1:]) if href.startswith("#") else None
            parent = parents.get(use)
            if parent is None:
                continue
            idx = list(parent).index(use)
            if target is None:
                # An unresolvable or external reference draws nothing in Rack
                # either way; dropping it keeps the output valid.
                print(f"  WARNING: <use> id={use.get('id')!r} references "
                      f"{href!r}, which is not in this document - dropped.")
                parent.remove(use)
                continue
            if target is use or use in target.iter():
                print(f"  WARNING: <use> id={use.get('id')!r} is self-"
                      f"referential - dropped.")
                parent.remove(use)
                continue
            parent[idx] = _instantiate(use, target)
            done += 1
        total += done
        if done == 0:
            break
    if any(True for _ in root.iter(USE)):
        print("  WARNING: <use> elements remain after expansion - Rack's SVG "
              "parser will not draw them.")
    return total


def check(path):
    """Warn about <text> that will not render in Rack. 0 if the panel is clean."""
    tree = ET.parse(path)
    bad = [
        el
        for el in tree.getroot().iter(f"{{{SVG}}}text")
        if text_content(el)
    ]
    if not bad:
        return 0
    print(f"  WARNING: {path} still contains {len(bad)} <text> element(s) with "
          f"content - Rack will not render them.")
    for el in bad[:5]:
        print(f"           id={el.get('id')!r}  {text_content(el)[:40]!r}")
    print("           Check for text inside a clip path or a <defs> block.")
    return 0  # advisory: a panel that still builds should still install


def main(argv):
    if len(argv) >= 3 and argv[1] == "--check":
        return check(argv[2])

    argv = list(argv)
    keep_hidden = "--keep-hidden" in argv
    if keep_hidden:
        argv.remove("--keep-hidden")

    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    src, dst = argv[1], argv[2]
    labels = set(argv[3:]) or {"components"}

    # Registered so the output keeps readable prefixes instead of ns0:, ns1:.
    # Inkscape would cope either way, but a human may open the temp file.
    ET.register_namespace("", SVG)
    ET.register_namespace("inkscape", INKSCAPE)
    ET.register_namespace("sodipodi", SODIPODI)
    ET.register_namespace("xlink", XLINK)

    tree = ET.parse(src)
    removed = []
    # Strip first: there is no point expanding clones that live in a layer
    # about to be deleted.
    strip(tree.getroot(), labels, removed)
    expanded = expand_uses(tree.getroot())
    # And hidden groups last — see drop_hidden() for why it cannot go earlier.
    hidden = []
    if not keep_hidden:
        drop_hidden(tree.getroot(), hidden)
    tree.write(dst, encoding="utf-8", xml_declaration=True)

    if removed:
        print(f"  stripped guide layer(s): {', '.join(sorted(set(removed)))}")
    else:
        # Not an error — a panel need not have guide layers. Said out loud
        # because a typo'd label would otherwise look like it worked.
        print(f"  no layer matched {sorted(labels)} (nothing stripped)")

    if hidden:
        names = sorted(set(hidden))
        shown = ", ".join(names[:6]) + (", ..." if len(names) > 6 else "")
        print(f"  dropped {len(hidden)} hidden group(s): {shown}")

    if expanded:
        # Reported every time, because the cost is invisible in the drawing:
        # a tiled clone is one path in Inkscape and N copies of it in the file
        # Rack has to parse, on every module instantiation.
        before, after = os.path.getsize(src), os.path.getsize(dst)
        print(f"  expanded {expanded} <use> clone(s): "
              f"{before / 1e6:.2f} MB -> {after / 1e6:.2f} MB")
        if after > 4e6:
            print(f"  WARNING: {after / 1e6:.1f} MB of geometry. SVG has no "
                  f"repeat nanosvg understands, so each tile is a full copy;")
            print( "           this is parsed per module instance and ships "
                   "inside the .vcvplugin. Consider fewer or simpler tiles.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
