import argparse
import json
import math
import re
from collections import Counter
from dataclasses import dataclass
from functools import cache
from itertools import pairwise
from pathlib import Path

from shapely import (
    constrained_delaunay_triangles,
    distance,
    get_coordinates,
    set_coordinates,
    set_precision,
)
from shapely.affinity import scale
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import nearest_points, polygonize, unary_union

from plans import CUTS, GUIDES, RIDGES, TEAM_PROFILES

ROOT = Path(__file__).resolve().parents[2]
SHAPES = {"COURTYARD": 0, "HALL": 1, "ROCK": 2, "BRIDGE": 3, "PIT": 4}
TRAVEL = {"NAV_WALK": 0, "NAV_JET": 1, "NAV_DROP": 2, "NAV_JUMP": 3}


@dataclass(frozen=True)
class Layout:
    name: str
    rooms: tuple
    links: tuple
    group: str


def layouts():
    found = {}
    for group in ("team", "dm", ""):
        source = (
            ROOT / ("world_layouts" + ("_" + group if group else "") + ".c")
        ).read_text()
        if group == "team":
            for name, rooms, links in re.findall(
                r'PLAN\("([^\"]+)",\s*\d+,\s*\d+,\s*\d+,\s*ROOMS\((.*?)\),\s*LINKS\((.*?)\)\)',
                source,
                re.DOTALL,
            ):
                found[name] = Layout(
                    name, rows(rooms, SHAPES), rows(links, TRAVEL), "team"
                )
        else:
            for name, symbol in re.findall(r'PLAN\("([^\"]+)",\s*(\w+),', source):
                room_match = re.search(
                    r"LayoutRoom\s+" + symbol + r"_rooms\[\]\s*=\s*\{(.*?)\n\};",
                    source,
                    re.DOTALL,
                )
                link_match = re.search(
                    r"LayoutLink\s+" + symbol + r"_links\[\]\s*=\s*\{(.*?)\n\};",
                    source,
                    re.DOTALL,
                )
                assert room_match, name
                assert link_match, name
                room_text, link_text = room_match.group(1), link_match.group(1)
                kind = (
                    "pilots"
                    if name in ("Arena2", "ctf_Ash")
                    else "team"
                    if "_" in name
                    else "dm"
                )
                found[name] = Layout(
                    name, rows(room_text, SHAPES), rows(link_text, TRAVEL), kind
                )
    assert len(found) == 99
    return found


def rows(source, enums):
    return tuple(
        tuple(
            enums[value.strip()] if value.strip() in enums else float(value)
            for value in row.split(",")
        )
        for row in re.findall(r"\{([^{}]+)\}", source)
    )


def portal(a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    t = min(
        a[3] * 0.5 / abs(dx) if dx else math.inf,
        a[4] * 0.5 / abs(dz) if dz else math.inf,
    )
    if a[6] == 2:
        t = min(
            t, (a[3] * 0.5 + a[4] * 0.5 - min(a[3], a[4]) * 0.16) / (abs(dx) + abs(dz))
        )
    return a[0] + dx * t, a[1] + dz * t


def ribbon(a, b, ya, yb, dip=0):
    dx, dz = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dz * dz

    def elevation(x, z):
        t = max(0, min(1, ((x - a[0]) * dx + (z - a[1]) * dz) / length2))
        return ya + (yb - ya) * t - 4 * dip * t * (1 - t)

    return elevation


def build(layout):
    name, rooms, links = layout.name, layout.rooms, layout.links
    profile = {
        p[0]: tuple(map(float, p[2:]))
        for p in map(str.split, TEAM_PROFILES.strip().splitlines())
    }
    if name in profile:
        bank, rise, sky, dip = profile[name]
    elif name == "ctf_Ash":
        bank, rise, sky, dip = 76, 112, 420, 32
    elif name == "Arena2":
        bank, rise, sky, dip = 64, 100, 400, 36
    else:
        bank = max(64, min(r[3] + r[4] for r in rooms) * 0.3)
        rise = max(72, (max(r[2] for r in rooms) - min(r[2] for r in rooms)) * 0.6)
        sky, dip = 380, 36
    anchor_shapes, anchor_values, priorities, land, constraints = [], [], [], [], []
    guide_regions = []
    clearances = []
    abutments = []
    exact_constraints = []
    bridges = {int(a) for a, b, _, _ in links if rooms[int(b)][6] == 3} | {
        int(b) for a, b, _, _ in links if rooms[int(a)][6] == 3
    }
    ground = []
    for i, (x, z, y, w, d, _roof, kind) in enumerate(rooms):
        adjacent = [
            rooms[int(b if int(a) == i else a)][2]
            for a, b, _, _ in links
            if i in (int(a), int(b)) and rooms[int(b if int(a) == i else a)][6] != 3
        ]
        level = min([*adjacent, y]) - dip if kind == 3 else y
        if kind == 3 and layout.group == "pilots":
            level = -24 if name == "Arena2" else -8
        elif kind == 3 and layout.group == "dm":
            level = min(r[2] for r in rooms) - 12
        ground.append(level)
        rectangle = box(x - w * 0.5, z - d * 0.5, x + w * 0.5, z + d * 0.5)
        land.append(rectangle.buffer(bank * 0.45, quad_segs=4))
        if kind in (1, 3) or i in bridges:
            padding = 32 if name == "ctf_Run" and i < 2 else 6
            shape = rectangle.buffer(padding, join_style="mitre")
        else:
            shape = scale(
                Point(x, z).buffer(1, quad_segs=4), max(50, w * 0.30), max(50, d * 0.30)
            )
        anchor_shapes.append(shape)
        anchor_values.append(lambda _x, _z, level=level: level)
        priorities.append(0)
    for start_room, end_room, mode, width in links:
        a, b = int(start_room), int(end_room)
        ra, rb = rooms[a], rooms[b]
        start, end = portal(ra, rb), portal(rb, ra)
        ground_start, ground_end = start, end
        if start == end:
            assert mode == 0
            assert ra[6] == 3 or rb[6] == 3
            ground_start, ground_end = (ra[0], ra[1]), (rb[0], rb[1])
        dx, dz = rb[0] - ra[0], rb[1] - ra[1]
        length = math.hypot(dx, dz)
        path = LineString([(ra[0], ra[1]), (rb[0], rb[1])])
        land.append(path.buffer(width * 0.5 + bank * 0.55, quad_segs=4))
        corridor = path.buffer(width * 0.5 + 6, quad_segs=4)
        height = ribbon(
            ground_start, ground_end, ground[a], ground[b], dip if mode != 0 else 0
        )
        if start != end and (ra[6] == 3 or rb[6] == 3) and mode == 0:
            footprint = LineString([start, end]).buffer(
                width * 0.5 + 3, cap_style="flat"
            )
            clearances.append((footprint, ribbon(start, end, ra[2], rb[2])))
            constraints.append(footprint)
            for room, point in ((ra, start), (rb, end)):
                if room[6] == 3:
                    continue
                section = LineString(
                    [
                        (
                            point[0] - dz / length * (width * 0.5 + 3),
                            point[1] + dx / length * (width * 0.5 + 3),
                        ),
                        (
                            point[0] + dz / length * (width * 0.5 + 3),
                            point[1] - dx / length * (width * 0.5 + 3),
                        ),
                    ]
                )
                abutments.append((section, ribbon(start, end, ra[2], rb[2]), footprint))
        elif mode != 0:
            footprint = path.buffer(width * 0.5 + 3, quad_segs=4)
            ceiling = max(ra[2], rb[2]) + 8
            clearances.append((footprint, lambda _x, _z, ceiling=ceiling: ceiling))
            constraints.append(footprint)
        for room, point, level in ((ra, start, ground[a]), (rb, end, ground[b])):
            if room[6] == 3:
                continue
            inward = math.hypot(room[0] - point[0], room[1] - point[1])
            landing = (
                point[0] + 12 * (room[0] - point[0]) / inward,
                point[1] + 12 * (room[1] - point[1]) / inward,
            )
            anchor_shapes.append(Point(*landing).buffer(17, quad_segs=4))
            anchor_values.append(lambda _x, _z, level=level: level)
            priorities.append(0)
        if mode == 0 and ra[6] != 3 and rb[6] != 3:
            anchor_shapes.append(corridor)
            anchor_values.append(height)
            priorities.append(1)
        else:
            guide_regions.append((path, height))
        cross_sections = (
            (start, end)
            if mode == 0
            else tuple(
                (start[0] + (end[0] - start[0]) * f, start[1] + (end[1] - start[1]) * f)
                for f in (0, 0.25, 0.5, 0.75, 1)
            )
        )
        for x, z in cross_sections:
            cross = LineString(
                [
                    (
                        x - dz / length * (width + bank),
                        z + dx / length * (width + bank),
                    ),
                    (
                        x + dz / length * (width + bank),
                        z - dx / length * (width + bank),
                    ),
                ]
            )
            if mode == 0 and (ra[6] == 3 or rb[6] == 3):
                exact_constraints.append(cross.intersection(corridor))
            else:
                constraints.append(cross.intersection(corridor))
    supports = []
    for section, height, own in abutments:
        for footprint, ceiling in clearances:
            if footprint is own:
                continue
            overlap = section.intersection(footprint.buffer(bank * 0.25))
            if overlap.length and any(
                height(x, z) > ceiling(x, z) for x, z in get_coordinates(overlap)
            ):
                support = section.buffer(3, cap_style="flat")
                supports.append((support, height))
                constraints.append(support)
                break
    materials = (ROOT / "world_materials.inc").read_text()
    material = re.search(
        r'MATERIAL_PLAN\("' + name + r'",\s*MATERIALS\((.*?)\)\)', materials, re.DOTALL
    )
    if material:
        for row in rows(material.group(1), {}):
            index, x, z, width, depth, _, _, _ = row
            room = rooms[int(index)]
            if room[6] == 3:
                continue
            center = (room[0] + x, room[1] + z)
            anchor_shapes.append(
                box(
                    center[0] - width * 0.5 - 3,
                    center[1] - depth * 0.5 - 3,
                    center[0] + width * 0.5 + 3,
                    center[1] + depth * 0.5 + 3,
                )
            )
            anchor_values.append(lambda _x, _z, level=room[2]: level)
            priorities.append(0)
    bridge_regions = {i for i, room in enumerate(rooms) if room[6] == 3}
    for i in bridge_regions:
        ground_cores = unary_union(
            [
                shape
                for j, shape in enumerate(anchor_shapes)
                if priorities[j] == 0
                and j not in bridge_regions
                and anchor_values[j](0, 0) != ground[i]
            ]
        )
        if anchor_shapes[i].intersection(ground_cores).area:
            anchor_shapes[i] = anchor_shapes[i].difference(
                ground_cores.buffer(bank * 0.5, quad_segs=4)
            )
    for guide in GUIDES.get(name, []):
        line = LineString([(p[0], p[1]) for p in guide])
        land.append(line.buffer(bank * 0.65, quad_segs=4))
        for a, b in pairwise(guide):
            guide_regions.append((LineString([a[:2], b[:2]]), ribbon(a, b, a[2], b[2])))
    inner = unary_union(land)
    boundary = Polygon(inner.exterior)
    if name in CUTS:
        boundary = boundary.difference(
            unary_union([Polygon(cut) for cut in CUTS[name]])
        )
    radius = min(bank * 0.25, min(link[3] for link in links) * 0.35)
    protected = unary_union(anchor_shapes)
    boundary = (
        boundary.buffer(-radius, quad_segs=4)
        .buffer(radius, quad_segs=4)
        .union(protected)
    )
    boundary = set_precision(boundary, 0.25)
    assert boundary.geom_type == "Polygon", (name, boundary.geom_type)
    boundary = Polygon(boundary.exterior)
    hard = unary_union(anchor_shapes)
    constraints.extend(anchor_shapes)
    constraints.extend(
        hard.buffer(bank * fraction, quad_segs=4).boundary.intersection(boundary)
        for fraction in (0.25, 0.5, 1)
    )
    constraints.append(boundary.buffer(-bank * 0.35, quad_segs=4).boundary)
    high = max(r[2] + r[5] for r in rooms)
    ridge = RIDGES.get(name, (100, 80, 120, 90))
    xmin, zmin, xmax, zmax = boundary.bounds

    @cache
    def elevation(x, z):
        point = Point(x, z)
        distances = distance(anchor_shapes, point)
        covered = [
            anchor_values[i](x, z)
            for i, d in enumerate(distances)
            if priorities[i] == 0 and d < 1e-7
        ]
        if covered:
            y = min(covered)
        else:
            core_distance = min(
                d
                for d, priority in zip(distances, priorities, strict=True)
                if priority == 0
            )
            weights = [
                (core_distance / (d if priority == 0 else math.hypot(d, bank / 8))) ** 4
                for d, priority in zip(distances, priorities, strict=True)
            ]
            values = [fn(x, z) for fn in anchor_values]
            for line, fn in guide_regions:
                d = line.distance(point)
                weights.append((core_distance / (d + bank * 0.35)) ** 2)
                values.append(fn(x, z))
            base = sum(w * v for w, v in zip(weights, values, strict=True)) / sum(
                weights
            )
            wx = (x - xmin) / (xmax - xmin)
            wz = (z - zmin) / (zmax - zmin)
            strength = (
                (1 - wx) * ridge[0]
                + wx * ridge[1]
                + (1 - wz) * ridge[2]
                + wz * ridge[3]
            ) / 200
            y = base + rise * strength * (1 - math.exp(-((min(distances) / bank) ** 2)))
        for footprint, ceiling in clearances:
            t = min(1, footprint.distance(point) / (bank * 0.25))
            influence = 1 - t * t * (3 - 2 * t)
            y += min(0, ceiling(x, z) - y) * influence
        for support, height in supports:
            y = max(y, height(x, z) - support.distance(point) ** 2 / 3)
        edge = boundary.exterior.distance(point)
        blend = min(1, edge / bank)
        blend = blend * blend * (3 - 2 * blend)
        crest = y * (1 - blend) + (high + sky) * blend
        return y, max(y, crest)

    vertices, triangles = triangulate(
        boundary, constraints, elevation, bank / 4, exact_constraints
    )
    return vertices, triangles, boundary


def triangulate(boundary, constraints, elevation, resolution, exact_constraints):
    linework = [
        shape.boundary if shape.geom_type in ("Polygon", "MultiPolygon") else shape
        for shape in constraints
    ]
    clipped = set_precision(unary_union(linework), 0.25).intersection(boundary)
    if exact_constraints:
        exact = unary_union(exact_constraints)
        coordinates = []
        for x, z in get_coordinates(clipped):
            point = Point(x, z)
            if point.distance(exact) <= 0.25:
                point = nearest_points(point, exact)[1]
            coordinates.append((point.x, point.y))
        clipped = set_coordinates(clipped, coordinates)
    clipped = set_precision(
        unary_union([clipped, *exact_constraints]), 0.000001
    ).intersection(boundary)
    faces = [
        face
        for face in polygonize(unary_union([boundary.boundary, clipped]))
        if boundary.covers(face.representative_point())
    ]
    vertices = []
    indices = {}

    def vertex(x, z):
        key = (round(x, 6), round(z, 6))
        if key not in indices:
            y, crest = elevation(*key)
            indices[key] = len(vertices)
            vertices.append((*key, round(y, 6), round(crest, 6)))
        return indices[key]

    triangles = []
    for face in faces:
        triangles.extend(
            tuple(vertex(x, z) for x, z in list(triangle.exterior.coords)[:3])
            for triangle in constrained_delaunay_triangles(face).geoms
        )
    while True:
        areas2 = [
            abs(
                (vertices[b][0] - vertices[a][0]) * (vertices[c][1] - vertices[a][1])
                - (vertices[c][0] - vertices[a][0]) * (vertices[b][1] - vertices[a][1])
            )
            for a, b, c in triangles
        ]
        smallest = min(areas2)
        if smallest > 0.01:
            break
        tiny = triangles[areas2.index(smallest)]
        edges = Counter(
            tuple(sorted((tri[i], tri[(i + 1) % 3])))
            for tri in triangles
            for i in range(3)
        )
        boundary_vertices = {
            vertex for edge, count in edges.items() if count == 1 for vertex in edge
        }
        candidates = sorted(
            (
                math.hypot(
                    vertices[a][0] - vertices[b][0], vertices[a][1] - vertices[b][1]
                ),
                a,
                b,
            )
            for a, b in ((tiny[0], tiny[1]), (tiny[1], tiny[2]), (tiny[2], tiny[0]))
        )
        collapsed = None
        for span, a, b in candidates:
            if span > math.sqrt(2) * 0.25:
                continue
            for keep, remove in ((a, b), (b, a)):
                if remove in boundary_vertices:
                    continue
                kept_point, removed_point = (
                    Point(*vertices[keep][:2]),
                    Point(*vertices[remove][:2]),
                )
                if any(
                    line.distance(removed_point) < 1e-6
                    and line.distance(kept_point) >= 1e-6
                    for line in exact_constraints
                ):
                    continue
                replacement = []
                valid = True
                for triangle in triangles:
                    changed = tuple(keep if i == remove else i for i in triangle)
                    if len(set(changed)) < 3:
                        continue
                    x, y, z = (vertices[i] for i in triangle)
                    old = (y[0] - x[0]) * (z[1] - x[1]) - (z[0] - x[0]) * (y[1] - x[1])
                    x, y, z = (vertices[i] for i in changed)
                    new = (y[0] - x[0]) * (z[1] - x[1]) - (z[0] - x[0]) * (y[1] - x[1])
                    if old * new <= 0:
                        valid = False
                        break
                    replacement.append(changed)
                if valid:
                    collapsed = replacement
                    break
            if collapsed is not None:
                break
        assert collapsed is not None, (
            "unresolved authoring sliver",
            smallest,
            [vertices[i] for i in tiny],
        )
        triangles = collapsed
    while True:
        split = set()
        for tri in triangles:
            points = [vertices[i] for i in tri]
            x, z = sum(p[0] for p in points) / 3, sum(p[1] for p in points) / 3
            error = abs(elevation(x, z)[0] - sum(p[2] for p in points) / 3)
            candidates = []
            for i in range(3):
                a, b = sorted((tri[i], tri[(i + 1) % 3]))
                pa, pb = vertices[a], vertices[b]
                span = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
                if span > resolution:
                    candidates.append((span, a, b))
                    mid = elevation((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)[0]
                    error = max(error, abs(mid - (pa[2] + pb[2]) / 2))
            area2 = abs(
                (points[1][0] - points[0][0]) * (points[2][1] - points[0][1])
                - (points[2][0] - points[0][0]) * (points[1][1] - points[0][1])
            )
            if error > 2 and candidates and area2 > 0.04:
                _, a, b = max(candidates)
                split.add((a, b))
        if not split:
            break
        mids = {
            edge: vertex(
                (vertices[edge[0]][0] + vertices[edge[1]][0]) / 2,
                (vertices[edge[0]][1] + vertices[edge[1]][1]) / 2,
            )
            for edge in split
        }
        refined = []
        for a, b, c in triangles:
            ab, bc, ca = (
                mids.get(tuple(sorted(edge))) for edge in ((a, b), (b, c), (c, a))
            )
            count = sum(mid is not None for mid in (ab, bc, ca))
            if count == 0:
                refined.append((a, b, c))
            elif count == 3:
                refined.extend(((a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)))
            elif count == 1:
                if ab is not None:
                    refined.extend(((a, ab, c), (ab, b, c)))
                elif bc is not None:
                    refined.extend(((b, bc, a), (bc, c, a)))
                else:
                    refined.extend(((c, ca, b), (ca, a, b)))
            elif ab is None:
                refined.extend(((a, b, bc), (a, bc, ca), (ca, bc, c)))
            elif bc is None:
                refined.extend(((b, c, ca), (b, ca, ab), (ab, ca, a)))
            else:
                refined.extend(((c, a, ab), (c, ab, bc), (bc, ab, b)))
        assert all(len(set(tri)) == 3 for tri in refined)
        assert len({tuple(sorted(tri)) for tri in refined}) == len(refined)
        assert all(
            abs(
                (vertices[b][0] - vertices[a][0]) * (vertices[c][1] - vertices[a][1])
                - (vertices[c][0] - vertices[a][0]) * (vertices[b][1] - vertices[a][1])
            )
            > 1e-8
            for a, b, c in refined
        )
        triangles = refined
    edges = Counter(
        tuple(sorted((tri[i], tri[(i + 1) % 3]))) for tri in triangles for i in range(3)
    )
    assert all(count in (1, 2) for count in edges.values())
    areas = [
        abs(
            (vertices[b][0] - vertices[a][0]) * (vertices[c][1] - vertices[a][1])
            - (vertices[c][0] - vertices[a][0]) * (vertices[b][1] - vertices[a][1])
        )
        * 0.5
        for a, b, c in triangles
    ]
    assert min(areas) > 0.005, (
        min(areas),
        len(triangles),
        [vertices[i] for i in triangles[areas.index(min(areas))]],
    )
    assert abs(sum(areas) - boundary.area) < max(1, boundary.area) * 1e-6
    used = sorted({index for tri in triangles for index in tri})
    remap = {old: new for new, old in enumerate(used)}
    return [vertices[index] for index in used], [
        tuple(remap[index] for index in tri) for tri in triangles
    ]


def arrays(symbol, vertices, triangles):
    output = []
    for kind, values, type_name in [
        ("vertices", vertices, "LayoutTerrainVertex"),
        ("triangles", triangles, "LayoutTerrainTriangle"),
    ]:
        output.append(f"static const {type_name} {symbol}_{kind}[] = {{")
        width = 3 if kind == "vertices" else 6
        for i in range(0, len(values), width):
            encoded = []
            for row in values[i : i + width]:
                fields = (
                    [
                        f"{value:.6f}".rstrip("0").rstrip(".") if value else "0"
                        for value in row
                    ]
                    if kind == "vertices"
                    else list(map(str, row))
                )
                encoded.append("{" + ",".join(fields) + "}")
            output.append("    " + ",".join(encoded) + ",")
        output.append("};")
    return "\n".join(output) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("maps", nargs="*")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    catalog = layouts()
    selected = args.maps or list(catalog)
    args.output.mkdir(parents=True, exist_ok=True)
    summary = []
    providers = {
        group: (ROOT / ("world_terrain_" + group + ".c")).read_text()
        for group in {catalog[name].group for name in selected}
    }
    replacements = {group: {} for group in providers}
    for name in selected:
        layout = catalog[name]
        vertices, triangles, boundary = build(layout)
        symbol = re.sub(r"\W", "_", name).lower()
        (args.output / (name + ".c")).write_text(arrays(symbol, vertices, triangles))
        if args.apply:
            source = providers[layout.group]
            match = (
                re.search(r'\{"' + name + r'",\s*\{(\w+)_vertices', source)
                if layout.group != "dm"
                else re.search(r'TERRAIN\("' + name + r'",(\w+)\)', source)
            )
            assert match, name
            old_symbol = match.group(1)
            replacements[layout.group][old_symbol] = arrays(
                old_symbol, vertices, triangles
            ).rstrip()
        row = {
            "name": name,
            "vertices": len(vertices),
            "triangles": len(triangles),
            "area": boundary.area,
            "convexity": boundary.area / boundary.convex_hull.area,
        }
        summary.append(row)
        print(json.dumps(row), flush=True)
    if args.apply:
        staged = {}
        pattern = (
            r"static const LayoutTerrainVertex (\w+)_vertices\[\] = \{.*?"
            r"static const LayoutTerrainTriangle \1_triangles\[\] = \{.*?\n\};"
        )
        for group, source in providers.items():
            pending = replacements[group]
            staged[group] = re.sub(
                pattern,
                lambda match, pending=pending: pending.pop(
                    match.group(1), match.group()
                ),
                source,
                flags=re.DOTALL,
            )
            assert not pending, (group, tuple(pending))
        for group, source in staged.items():
            (ROOT / ("world_terrain_" + group + ".c")).write_text(source)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
