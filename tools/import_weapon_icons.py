"""Convert installed CS2 HUD SVG silhouettes into a local ImGui icon font.

Reads installed VPKs without modifying them. The resulting Valve artwork stays
under ignored logs/ and is not bundled in Git. Requires fonttools 4.59.1 and skia-pathops 0.9.2 (tested).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

from fontTools.fontBuilder import FontBuilder
from fontTools.misc.transform import Transform
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.pointInsidePen import PointInsidePen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import SVGPath
import pathops

from material_vpk_probe import entries

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game")
EXTRA = {
    43: "flashbang", 44: "hegrenade", 45: "smokegrenade", 46: "molotov",
    47: "decoy", 48: "incgrenade", 49: "c4", 65534: "planted_c4",
}


def item_assets():
    text = (ROOT / "src/skin_catalog_data.inc").read_text(encoding="utf-8")
    result = {int(number): name.removeprefix("weapon_") for number, name in re.findall(
        r'\{(\d+), "([^"]+)", [^\n]*ItemKind::(?:Weapon|Knife),', text)}
    result.update(EXTRA)
    if len(result) != 66:
        raise ValueError(f"Installed item catalog changed: expected 66 icon entries, got {len(result)}")
    return result


def extract(index, entry):
    name, archive, offset, length, head, start = entry
    path = index if archive == 0x7FFF else index.with_name(index.name.replace("_dir.vpk", f"_{archive:03}.vpk"))
    with path.open("rb") as stream:
        stream.seek(offset + (start if archive == 0x7FFF else 0))
        content = head + stream.read(length)
    if len(content) != len(head) + length:
        raise ValueError(f"Truncated installed VPK asset: {name}")
    return content


def resource_svg(content):
    if len(content) < 16:
        raise ValueError("Truncated resource header")
    declared, header_version, resource_version, directory, count = struct.unpack_from("<IHHII", content)
    if declared != len(content) or header_version != 12 or resource_version != 2 or count > 32:
        raise ValueError("Unrecognized VSVG resource header")
    blocks = []
    for i in range(count):
        descriptor = 8 + directory + 12 * i
        if descriptor + 12 > len(content):
            raise ValueError("Truncated resource block directory")
        tag, relative, size = struct.unpack_from("<4sII", content, descriptor)
        start = descriptor + 4 + relative
        if start + size > len(content):
            raise ValueError("Resource block leaves file")
        if tag == b"DATA":
            blocks.append(content[start:start + size])
    if len(blocks) != 1:
        raise ValueError("Expected one VSVG DATA block")
    block = blocks[0]
    at = block.find(b"<?xml")
    if at < 0:
        at = block.find(b"<svg")
    if at < 0 or at > 64:
        raise ValueError("VSVG DATA does not contain plain SVG XML")
    return block[at:]


def contours(recording):
    result = []
    current = None
    for operation, arguments in recording.value:
        if operation == "moveTo":
            if current is not None:
                raise ValueError("Unclosed SVG contour")
            current = RecordingPen()
        if current is None:
            raise ValueError("SVG command outside contour")
        getattr(current, operation)(*arguments)
        if operation in ("closePath", "endPath"):
            if operation != "closePath":
                raise ValueError("Open SVG stroke cannot become a filled glyph")
            result.append(current)
            current = None
    if current is not None:
        raise ValueError("Unclosed SVG contour")
    return result


def inside(contour, point, even_odd):
    pen = PointInsidePen(None, point, evenOdd=even_odd)
    contour.replay(pen)
    return pen.getResult()


def artwork(svg):
    root = ET.fromstring(svg)
    parents = {child: parent for parent in root.iter() for child in parent}
    def inherited(element, name, default):
        while element is not None:
            if name in element.attrib:
                return element.attrib[name]
            element = parents.get(element)
        return default
    paths = []
    for element in root.iter():
        tag = element.tag.rsplit("}", 1)[-1]
        if tag not in {"svg", "g", "path", "polygon", "rect", "title", "desc"}:
            raise ValueError(f"Unsupported SVG element: {tag}")
        # The installed equipment assets have no transforms, CSS, masks, strokes
        # or masks. Reject future variants instead of silently
        # dropping ancestor transforms or drawing stroked geometry as filled.
        if any(key in element.attrib for key in (
                "transform", "style", "class", "stroke", "clip-path", "mask", "opacity")):
            raise ValueError(f"Unsupported SVG styling/transform on {tag}")
        if tag in {"svg", "g"}:
            continue
        if tag in {"title", "desc"}:
            continue
        fill = inherited(element, "fill", "black").lower()
        if fill == "none":
            continue
        if fill not in {"white", "#fff", "#ffffff"}:
            raise ValueError("Expected a white HUD silhouette")
        rule = inherited(element, "fill-rule", "nonzero")
        if rule not in {"nonzero", "evenodd"}:
            raise ValueError("Unsupported SVG fill rule")
        if tag == "rect" and any(element.get(key, "0") != "0" for key in ("rx", "ry")):
            raise ValueError("Rounded SVG rectangles need an explicit conversion")
        wrapper = ET.Element("svg", xmlns="http://www.w3.org/2000/svg")
        wrapper.append(element)
        recorded = RecordingPen()
        SVGPath.fromstring(ET.tostring(wrapper)).draw(recorded)
        parts = contours(recorded)
        if not parts:
            raise ValueError("Empty SVG path")
        opacity = float(inherited(element, "fill-opacity", "1"))
        if opacity not in {1.0, .4, .6, .851}:
            raise ValueError(f"Unsupported HUD opacity {opacity}")
        paths.append((recorded, parts, rule == "evenodd", opacity))
    if not paths:
        raise ValueError("No SVG geometry")
    bounds = BoundsPen(None)
    for recorded, _, _, _ in paths:
        recorded.replay(bounds)
    if not bounds.bounds or not all(math.isfinite(value) for value in bounds.bounds):
        raise ValueError("Invalid SVG bounds")
    left, top, right, bottom = bounds.bounds
    if right <= left or bottom <= top:
        raise ValueError("Degenerate SVG bounds")
    scale = 800 / (bottom - top)
    # Flip SVG's downward Y into font coordinates, with the actual ink at 0..800.
    transform = Transform(scale, 0, 0, -scale, -left * scale, bottom * scale)
    def normalize(selected):
        combined = pathops.Path()
        for recorded, _, even_odd, _ in selected:
            native = pathops.Path()
            recorded.replay(native.getPen())
            native.fillType = pathops.FillType.EVEN_ODD if even_odd else pathops.FillType.WINDING
            # Boolean simplification resolves intersecting contours and overlapping
            # holes; depth-only contour reversal is insufficient for e.g. Glock.
            combined.addPath(pathops.simplify(native, clockwise=True))
        output = RecordingPen()
        # Distinct SVG elements form a union, not cancellation where overlapping.
        pathops.simplify(combined, clockwise=True).draw(TransformPen(output, transform))
        return output
    output = normalize(paths)
    normalized_paths = sum(even_odd for _, _, even_odd, _ in paths)
    layers = {alpha: normalize([path for path in paths if path[3] == alpha])
              for alpha in sorted({path[3] for path in paths})}
    # Compare non-zero font winding with original per-element SVG fill rules.
    # Samples away from edges exercise internal holes and separate components.
    for ix in range(23):
        for iy in range(13):
            point = (left + (ix + .37) / 23 * (right - left), top + (iy + .41) / 13 * (bottom - top))
            original = any(inside(recorded, point, rule) for recorded, _, rule, _ in paths)
            converted = inside(output, transform.transformPoint(point), False)
            if original != converted:
                raise ValueError(f"Converted contour winding differs from original SVG fill at {point}")
    return output, bounds.bounds, normalized_paths, layers


def generate(game, output):
    index = game / "csgo/pak01_dir.vpk"
    installed = {entry[0]: entry for entry in entries(index)}
    assets = item_assets()
    builder = FontBuilder(1000, isTTF=True)
    order = [".notdef"] + [f"icon{definition}" for definition in sorted(assets)]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (1000, 0)}
    cmap = {}
    provenance = []
    for definition, asset in sorted(assets.items()):
        resource = f"panorama/images/icons/equipment/{asset}.vsvg_c"
        if resource not in installed:
            raise ValueError(f"Installed HUD artwork missing: {resource}")
        content = extract(index, installed[resource])
        svg = resource_svg(content)
        try:
            outlines, bounds, normalized, layers = artwork(svg)
        except ValueError as error:
            raise ValueError(f"{resource}: {error}") from error
        name = f"icon{definition}"
        advance = round((bounds[2] - bounds[0]) / (bounds[3] - bounds[1]) * 800)
        def add_glyph(glyph_name, recording, codepoint):
            pen = TTGlyphPen(None)
            recording.replay(Cu2QuPen(pen, max_err=.3))
            glyphs[glyph_name] = pen.glyph()
            metrics[glyph_name] = (advance, 0)
            cmap[codepoint] = glyph_name
        add_glyph(name, outlines, 0xEFFF if definition == 65534 else 0xE000 + definition)
        partial = {43: .4, 46: .6, 47: .851}.get(definition)
        expected_alphas = {1.0} if partial is None else {1.0, partial}
        if not set(layers).issubset(expected_alphas) or (partial is not None and partial not in layers):
            raise ValueError(f"Unexpected alpha layers in {resource}: {list(layers)}")
        if partial is not None:
            # The main glyph measures the complete original silhouette. Separate
            # glyphs retain original translucent and opaque SVG layer coverage.
            for alpha, base, suffix in ((1.0, 0xE400, "opaque"), (partial, 0xE800, "partial")):
                extra_name = f"{name}_{suffix}"
                order.append(extra_name)
                add_glyph(extra_name, layers.get(alpha, RecordingPen()), base + definition)
        provenance.append({"definition": definition, "resource": resource,
            "vsvg_sha256": hashlib.sha256(content).hexdigest(), "svg_sha256": hashlib.sha256(svg).hexdigest(),
            "ink_bounds": bounds, "contours": glyphs[name].numberOfContours, "normalized_evenodd_paths": normalized, "opacity_layers": sorted(layers)})
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=1000, descent=0)
    builder.setupNameTable({"familyName": "Local CS2 HUD icons", "styleName": "Regular",
        "uniqueFontIdentifier": "LocalCS2HUDIcons-v1", "fullName": "Local CS2 HUD icons",
        "psName": "LocalCS2HUDIcons-Regular", "version": "Version 1.0",
        "copyright": "Valve CS2 HUD artwork, converted from the user's installed game for local use."})
    builder.setupOS2(sTypoAscender=1000, sTypoDescender=0, usWinAscent=1000, usWinDescent=0)
    builder.setupPost()
    builder.setupMaxp()
    # Deterministic local rebuilds; do not bake execution timestamps into artwork.
    builder.font["head"].created = builder.font["head"].modified = 3800000000
    builder.font.recalcTimestamp = False
    output.parent.mkdir(parents=True, exist_ok=True)
    builder.save(output)
    report = {"game": str(game), "font": str(output), "glyph_count": len(assets),
        "font_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "conversion": "Installed VSVG DATA SVG; exact Bezier geometry, even-odd Boolean fill normalized by Skia PathOps; quadratic error <=0.3/1000 em",
        "assets": provenance}
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Imported {len(assets)} installed CS2 HUD silhouettes ({len(glyphs)-1} font glyphs): {output}")
    print(f"Source hashes and glyph bounds: {output.with_suffix('.json')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--output", type=Path, default=ROOT / "logs/weapon-icons/cs2-hud-icons.ttf")
    args = parser.parse_args()
    generate(args.game, args.output)


if __name__ == "__main__":
    main()
