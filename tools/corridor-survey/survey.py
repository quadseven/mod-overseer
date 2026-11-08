#!/usr/bin/env python3
"""Survey a grounded corridor from AzerothCore Detour mmap tiles."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import heapq
import json
import math
from pathlib import Path
import re
import struct
import sys


MMAP_MAGIC = 0x4D4D4150
MMAP_VERSION = 20
DETOUR_MAGIC = 0x444E4156
DETOUR_VERSION = 7
MMAP_HEADER_SIZE = 56
MESH_HEADER_SIZE = 100
POLY_SIZE = 32
LINK_SIZE = 12
DETAIL_SIZE = 12
DETAIL_VERTEX_SIZE = 12
DETAIL_TRIANGLE_SIZE = 4
BV_NODE_SIZE = 16
OFFMESH_SIZE = 36
MAX_POLY_VERTS = 6
EXTERNAL_LINK = 0x8000
TILE_RE = re.compile(r"^(\d{3})(\d{2})(\d{2})\.mmtile$")


class SurveyError(Exception):
    """A malformed input or a route the input cannot establish."""


@dataclass(frozen=True)
class Vertex:
    x: float
    y: float
    z: float


@dataclass(frozen=True)
class Portal:
    left: Vertex
    right: Vertex


@dataclass
class Polygon:
    node: int
    tile: Tile
    index: int
    vertices: tuple[int, ...]
    neighbors: tuple[int, ...]
    flags: int
    area_type: int

    @property
    def identifier(self) -> str:
        return f"{self.tile.map_id:03d}{self.tile.x:02d}{self.tile.y:02d}#{self.index}"

    @property
    def ground(self) -> bool:
        return self.area_type >> 6 == 0 and self.flags != 0


class Tile:
    def __init__(self, path: Path, map_id: int, tile_x: int, tile_y: int):
        self.path = path
        self.map_id = map_id
        self.x = tile_x
        self.y = tile_y
        self.walkable_climb = 0.0
        self.vertices: list[Vertex] = []
        self.polygons: list[Polygon] = []
        self.details: list[tuple[int, int, int, int]] = []
        self.detail_vertices: list[Vertex] = []
        self.detail_triangles: list[tuple[int, int, int, int]] = []
        self._read()

    def _read(self) -> None:
        try:
            data = self.path.read_bytes()
        except OSError as exc:
            raise SurveyError(f"{self.path.name}: cannot read tile: {exc}") from exc
        if len(data) < MMAP_HEADER_SIZE:
            raise SurveyError(f"{self.path.name}: truncated MMap tile header")

        magic, detour_version, mmap_version, payload_size = struct.unpack_from("<4I", data)
        if magic != MMAP_MAGIC:
            raise SurveyError(f"{self.path.name}: invalid MMap magic 0x{magic:08x}")
        if detour_version != DETOUR_VERSION or mmap_version != MMAP_VERSION:
            raise SurveyError(
                f"{self.path.name}: unsupported tile versions Detour={detour_version}, "
                f"MMap={mmap_version}; expected {DETOUR_VERSION}/{MMAP_VERSION}"
            )
        if len(data) != MMAP_HEADER_SIZE + payload_size:
            raise SurveyError(f"{self.path.name}: declared payload size does not match file size")

        payload = data[MMAP_HEADER_SIZE:]
        if len(payload) < MESH_HEADER_SIZE:
            raise SurveyError(f"{self.path.name}: truncated Detour mesh header")
        fields = struct.unpack_from("<15i10f", payload)
        (detour_magic, version, tile_x, tile_y, _layer, _user_id,
         poly_count, vertex_count, link_count, detail_count, detail_vertex_count,
         detail_triangle_count, bv_count, offmesh_count, _offmesh_base) = fields[:15]
        self.walkable_climb = fields[17]
        if detour_magic != DETOUR_MAGIC or version != DETOUR_VERSION:
            raise SurveyError(f"{self.path.name}: invalid Detour mesh magic or version")
        if (tile_x, tile_y) != (self.x, self.y):
            raise SurveyError(
                f"{self.path.name}: filename tile {self.x}/{self.y} differs from "
                f"mesh tile {tile_x}/{tile_y}"
            )
        counts = (poly_count, vertex_count, link_count, detail_count,
                  detail_vertex_count, detail_triangle_count, bv_count, offmesh_count)
        if any(count < 0 for count in counts):
            raise SurveyError(f"{self.path.name}: negative array count in Detour header")
        if poly_count > 1_000_000 or vertex_count > 6_000_000:
            raise SurveyError(f"{self.path.name}: unreasonable Detour array count")

        offsets = self._array_offsets(counts)
        expected_size = offsets[-1] + offmesh_count * OFFMESH_SIZE
        if expected_size != len(payload):
            raise SurveyError(
                f"{self.path.name}: Detour arrays end at {expected_size} bytes, "
                f"payload has {len(payload)}"
            )

        vert_offset, poly_offset, _link_offset, detail_offset, detail_vert_offset, detail_tri_offset, _bv_offset, _offmesh_offset = offsets
        for index in range(vertex_count):
            nav_x, nav_y, nav_z = struct.unpack_from("<3f", payload, vert_offset + index * 12)
            point = Vertex(nav_z, nav_x, nav_y)
            if not all(math.isfinite(value) for value in (point.x, point.y, point.z)):
                raise SurveyError(f"{self.path.name}: non-finite vertex {index}")
            self.vertices.append(point)

        for index in range(poly_count):
            record = struct.unpack_from("<I6H6HHBB", payload, poly_offset + index * POLY_SIZE)
            vertex_indices = tuple(record[1:1 + MAX_POLY_VERTS])
            neighbor_fields = tuple(record[1 + MAX_POLY_VERTS:1 + MAX_POLY_VERTS * 2])
            flags, vertex_count_in_poly, area_type = record[-3:]
            if not 3 <= vertex_count_in_poly <= MAX_POLY_VERTS:
                raise SurveyError(f"{self.path.name}: polygon {index} has invalid vertex count")
            used_vertices = vertex_indices[:vertex_count_in_poly]
            if any(vertex >= vertex_count for vertex in used_vertices):
                raise SurveyError(f"{self.path.name}: polygon {index} references a missing vertex")
            node = len(self.polygons)
            self.polygons.append(Polygon(
                node=node,
                tile=self,
                index=index,
                vertices=used_vertices,
                neighbors=neighbor_fields[:vertex_count_in_poly],
                flags=flags,
                area_type=area_type,
            ))

        for index in range(detail_count):
            vert_base, tri_base, vert_count, tri_count, _pad0, _pad1 = struct.unpack_from(
                "<II4B", payload, detail_offset + index * DETAIL_SIZE
            )
            if vert_base + vert_count > detail_vertex_count or tri_base + tri_count > detail_triangle_count:
                raise SurveyError(f"{self.path.name}: detail mesh {index} exceeds its arrays")
            self.details.append((vert_base, tri_base, vert_count, tri_count))

        for index in range(detail_vertex_count):
            nav_x, nav_y, nav_z = struct.unpack_from(
                "<3f", payload, detail_vert_offset + index * DETAIL_VERTEX_SIZE
            )
            self.detail_vertices.append(Vertex(nav_z, nav_x, nav_y))

        for index in range(detail_triangle_count):
            self.detail_triangles.append(struct.unpack_from(
                "<4B", payload, detail_tri_offset + index * DETAIL_TRIANGLE_SIZE
            ))

        if detail_count > poly_count:
            raise SurveyError(f"{self.path.name}: more detail meshes than polygons")
        for poly_index, detail in enumerate(self.details):
            if poly_index >= poly_count:
                break
            poly = self.polygons[poly_index]
            vert_base, tri_base, _vert_count, tri_count = detail
            for tri_index in range(tri_base, tri_base + tri_count):
                triangle = self.detail_triangles[tri_index]
                for local_index in triangle[:3]:
                    if local_index >= len(poly.vertices) and (
                        vert_base + local_index - len(poly.vertices) >= detail_vertex_count
                    ):
                        raise SurveyError(
                            f"{self.path.name}: detail mesh {poly_index} references a missing vertex"
                        )

    @staticmethod
    def _array_offsets(counts: tuple[int, ...]) -> tuple[int, ...]:
        poly_count, vertex_count, link_count, detail_count, detail_vertex_count, detail_triangle_count, bv_count, _offmesh_count = counts
        offsets = [MESH_HEADER_SIZE]
        offsets.append(offsets[-1] + vertex_count * 12)
        offsets.append(offsets[-1] + poly_count * POLY_SIZE)
        offsets.append(offsets[-1] + link_count * LINK_SIZE)
        offsets.append(offsets[-1] + detail_count * DETAIL_SIZE)
        offsets.append(offsets[-1] + detail_vertex_count * DETAIL_VERTEX_SIZE)
        offsets.append(offsets[-1] + detail_triangle_count * DETAIL_TRIANGLE_SIZE)
        offsets.append(offsets[-1] + bv_count * BV_NODE_SIZE)
        return tuple(offsets)

    def vertex(self, index: int) -> Vertex:
        return self.vertices[index]

    def poly_vertices(self, poly: Polygon) -> tuple[Vertex, ...]:
        return tuple(self.vertices[index] for index in poly.vertices)

    def ground_height(self, poly: Polygon, x: float, y: float) -> float | None:
        if poly.index < len(self.details):
            vert_base, tri_base, _vert_count, tri_count = self.details[poly.index]
            for local_tri in range(tri_count):
                a, b, c, _flags = self.detail_triangles[tri_base + local_tri]
                triangle = tuple(self._detail_vertex(poly, vert_base, vertex) for vertex in (a, b, c))
                height = _triangle_height(triangle, x, y)
                if height is not None:
                    return height

        vertices = self.poly_vertices(poly)
        for index in range(1, len(vertices) - 1):
            height = _triangle_height((vertices[0], vertices[index], vertices[index + 1]), x, y)
            if height is not None:
                return height
        return None

    def _detail_vertex(self, poly: Polygon, detail_base: int, local_index: int) -> Vertex:
        if local_index < len(poly.vertices):
            return self.vertex(poly.vertices[local_index])
        return self.detail_vertices[detail_base + local_index - len(poly.vertices)]


def _triangle_height(triangle: tuple[Vertex, Vertex, Vertex], x: float, y: float) -> float | None:
    a, b, c = triangle
    denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y)
    if abs(denominator) < 1e-9:
        return None
    wa = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denominator
    wb = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denominator
    wc = 1.0 - wa - wb
    if min(wa, wb, wc) < -1e-5:
        return None
    return wa * a.z + wb * b.z + wc * c.z


def _distance(a: Vertex, b: Vertex) -> float:
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def _centroid(poly: Polygon) -> Vertex:
    vertices = poly.tile.poly_vertices(poly)
    count = len(vertices)
    return Vertex(
        sum(point.x for point in vertices) / count,
        sum(point.y for point in vertices) / count,
        sum(point.z for point in vertices) / count,
    )


def _contains_xy(poly: Polygon, x: float, y: float) -> bool:
    vertices = poly.tile.poly_vertices(poly)
    inside = False
    previous = vertices[-1]
    for current in vertices:
        cross = (x - previous.x) * (current.y - previous.y) - (y - previous.y) * (current.x - previous.x)
        if abs(cross) < 1e-5 and min(previous.x, current.x) - 1e-5 <= x <= max(previous.x, current.x) + 1e-5 and min(previous.y, current.y) - 1e-5 <= y <= max(previous.y, current.y) + 1e-5:
            return True
        if (current.y > y) != (previous.y > y):
            crossing_x = (previous.x - current.x) * (y - current.y) / (previous.y - current.y) + current.x
            if x < crossing_x:
                inside = not inside
        previous = current
    return inside


def _closest_xy(poly: Polygon, x: float, y: float) -> tuple[float, float]:
    vertices = poly.tile.poly_vertices(poly)
    if _contains_xy(poly, x, y):
        return x, y

    best_point = (vertices[0].x, vertices[0].y)
    best_distance = math.inf
    for index, first in enumerate(vertices):
        second = vertices[(index + 1) % len(vertices)]
        dx = second.x - first.x
        dy = second.y - first.y
        length_squared = dx * dx + dy * dy
        fraction = 0.0 if length_squared == 0 else (
            (x - first.x) * dx + (y - first.y) * dy
        ) / length_squared
        fraction = min(1.0, max(0.0, fraction))
        candidate = (first.x + fraction * dx, first.y + fraction * dy)
        distance = (candidate[0] - x) ** 2 + (candidate[1] - y) ** 2
        if distance < best_distance:
            best_point = candidate
            best_distance = distance
    return best_point


def _find_polygon(polygons: list[Polygon], point: Vertex,
                  max_horizontal: float = 3.0,
                  max_vertical: float = 50.0) -> tuple[Polygon, Vertex] | None:
    candidates: list[tuple[float, Polygon, Vertex]] = []
    for poly in polygons:
        if not poly.ground:
            continue
        x, y = _closest_xy(poly, point.x, point.y)
        if abs(x - point.x) > max_horizontal or abs(y - point.y) > max_horizontal:
            continue
        height = poly.tile.ground_height(poly, x, y)
        if height is None or abs(height - point.z) > max_vertical:
            continue
        projected = Vertex(x, y, height)
        distance = _distance(point, projected)
        candidates.append((distance, poly, projected))
    if not candidates:
        return None
    _distance_squared, poly, projected = min(candidates, key=lambda item: item[0])
    return poly, projected


def _edge_key(a: Vertex, b: Vertex) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    one = (round(a.x * 100), round(a.y * 100), round(a.z * 100))
    two = (round(b.x * 100), round(b.y * 100), round(b.z * 100))
    return tuple(sorted((one, two)))  # type: ignore[return-value]


def _external_portal(one: tuple[Vertex, Vertex], two: tuple[Vertex, Vertex],
                     side: int, climb: float) -> Portal | None:
    """Match the overlapping, climbable part of a Detour external edge."""
    if side in (0, 4):
        fixed_axis = "y"
        along_axis = "x"
    elif side in (2, 6):
        fixed_axis = "x"
        along_axis = "y"
    else:
        return None

    if abs(getattr(one[0], fixed_axis) - getattr(two[0], fixed_axis)) > 0.01:
        return None

    def slab(edge: tuple[Vertex, Vertex]) -> tuple[float, float, float, float] | None:
        start, end = edge
        start_along = getattr(start, along_axis)
        end_along = getattr(end, along_axis)
        if abs(end_along - start_along) < 1e-9:
            return None
        if start_along > end_along:
            start, end = end, start
            start_along, end_along = end_along, start_along
        return start_along, end_along, start.z, end.z

    first = slab(one)
    second = slab(two)
    if first is None or second is None:
        return None

    low = max(first[0] + 0.01, second[0] + 0.01)
    high = min(first[1] - 0.01, second[1] - 0.01)
    if low > high:
        return None

    def height_at(edge: tuple[float, float, float, float], along: float) -> float:
        fraction = (along - edge[0]) / (edge[1] - edge[0])
        return edge[2] + fraction * (edge[3] - edge[2])

    difference_low = height_at(first, low) - height_at(second, low)
    difference_high = height_at(first, high) - height_at(second, high)
    if difference_low * difference_high >= 0 and min(abs(difference_low), abs(difference_high)) > 2 * climb:
        return None

    portal_low = max(first[0], second[0])
    portal_high = min(first[1], second[1])

    def point_at(along: float) -> Vertex:
        x = along if along_axis == "x" else one[0].x
        y = along if along_axis == "y" else one[0].y
        return Vertex(x, y, height_at(first, along))

    return Portal(point_at(portal_low), point_at(portal_high))


def _portal(a: Vertex, b: Vertex) -> Vertex:
    return Vertex((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2)


def _load_tiles(directory: Path, map_id: int) -> tuple[list[Tile], list[Polygon]]:
    if not directory.is_dir():
        raise SurveyError(f"maps directory does not exist: {directory}")
    candidates: list[tuple[Path, int, int]] = []
    for path in sorted(directory.iterdir()):
        match = TILE_RE.match(path.name)
        if not match or int(match.group(1)) != map_id:
            continue
        candidates.append((path, int(match.group(2)), int(match.group(3))))
    if not candidates:
        raise SurveyError(f"no mmap tiles found for map {map_id:03d} in {directory}")

    tiles = [Tile(path, map_id, x, y) for path, x, y in candidates]
    polygons = []
    for tile in tiles:
        for poly in tile.polygons:
            poly.node = len(polygons)
            if poly.ground:
                polygons.append(poly)
    if not polygons:
        raise SurveyError(f"map {map_id:03d} has no ground polygons in its mmap tiles")
    return tiles, polygons


def _build_graph(tiles: list[Tile]) -> tuple[dict[int, dict[int, Portal]], dict[int, Polygon]]:
    polygons = {poly.node: poly for tile in tiles for poly in tile.polygons if poly.ground}
    graph: dict[int, dict[int, Portal]] = {node: {} for node in polygons}

    def connect(one: Polygon, two: Polygon, portal: Portal) -> None:
        if one.node == two.node or not one.ground or not two.ground:
            return
        graph[one.node][two.node] = portal
        graph[two.node][one.node] = portal

    for tile in tiles:
        for poly in tile.polygons:
            if not poly.ground:
                continue
            vertices = tile.poly_vertices(poly)
            for edge_index, neighbor_field in enumerate(poly.neighbors):
                if neighbor_field == 0 or neighbor_field & EXTERNAL_LINK:
                    continue
                neighbor_index = (neighbor_field & 0x7FFF) - 1
                if not 0 <= neighbor_index < len(tile.polygons):
                    raise SurveyError(f"{tile.path.name}: polygon {poly.index} has invalid neighbor index")
                neighbor = tile.polygons[neighbor_index]
                edge = _edge_key(vertices[edge_index], vertices[(edge_index + 1) % len(vertices)])
                other_vertices = tile.poly_vertices(neighbor)
                other_portal = None
                for other_index, first in enumerate(other_vertices):
                    second = other_vertices[(other_index + 1) % len(other_vertices)]
                    if _edge_key(first, second) == edge:
                        other_portal = Portal(first, second)
                        break
                if other_portal is not None:
                    connect(poly, neighbor, other_portal)

    external_edges: list[tuple[Polygon, int, Vertex, Vertex]] = []
    for tile in tiles:
        for poly in tile.polygons:
            if not poly.ground:
                continue
            vertices = tile.poly_vertices(poly)
            for index, first in enumerate(vertices):
                neighbor_field = poly.neighbors[index]
                if not neighbor_field & EXTERNAL_LINK:
                    continue
                second = vertices[(index + 1) % len(vertices)]
                external_edges.append((poly, neighbor_field & 0xFF, first, second))
    tile_deltas = {0: (1, 0), 2: (0, 1), 4: (-1, 0), 6: (0, -1)}
    opposite_sides = {0: 4, 2: 6, 4: 0, 6: 2}
    for one, side, first, second in external_edges:
        delta = tile_deltas.get(side)
        opposite = opposite_sides.get(side)
        if delta is None or opposite is None:
            continue
        for two, other_side, other_first, other_second in external_edges:
            if other_side != opposite:
                continue
            if (two.tile.x, two.tile.y) != (one.tile.x + delta[0], one.tile.y + delta[1]):
                continue
            portal = _external_portal(
                (first, second), (other_first, other_second), side,
                max(one.tile.walkable_climb, two.tile.walkable_climb),
            )
            if portal is not None:
                connect(one, two, portal)
    return graph, polygons


def _shortest_path(start: int, goal: int, graph: dict[int, dict[int, Portal]], polygons: dict[int, Polygon]) -> list[int]:
    goal_center = _centroid(polygons[goal])
    queue: list[tuple[float, float, int]] = [(0.0, 0.0, start)]
    cost = {start: 0.0}
    parent: dict[int, int] = {}
    while queue:
        _priority, current_cost, current = heapq.heappop(queue)
        if current_cost != cost.get(current):
            continue
        if current == goal:
            path = [goal]
            while path[-1] != start:
                path.append(parent[path[-1]])
            return list(reversed(path))
        current_center = _centroid(polygons[current])
        for neighbor in graph[current]:
            next_cost = current_cost + _distance(current_center, _centroid(polygons[neighbor]))
            if next_cost >= cost.get(neighbor, math.inf):
                continue
            cost[neighbor] = next_cost
            parent[neighbor] = current
            estimate = next_cost + _distance(_centroid(polygons[neighbor]), goal_center)
            heapq.heappush(queue, (estimate, next_cost, neighbor))
    raise SurveyError("no polygon path links start and door")


def _area(a: Vertex, b: Vertex, c: Vertex) -> float:
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)


def _same_xy(a: Vertex, b: Vertex) -> bool:
    return abs(a.x - b.x) < 1e-6 and abs(a.y - b.y) < 1e-6


def _point_on_poly(poly: Polygon, x: float, y: float) -> Vertex:
    height = poly.tile.ground_height(poly, x, y)
    if height is None:
        raise SurveyError(f"{poly.tile.path.name}: cannot resolve ground height on polygon {poly.index}")
    return Vertex(x, y, height)


def _oriented_portal(one: Polygon, two: Polygon, portal: Portal) -> Portal:
    start = _centroid(one)
    end = _centroid(two)
    if _area(start, end, portal.left) >= _area(start, end, portal.right):
        return portal
    return Portal(portal.right, portal.left)


def _funnel(start: Vertex, door: Vertex, portals: list[Portal]) -> list[Vertex]:
    """String-pull a polygon corridor, retaining only turns and its endpoints."""
    sequence = [Portal(start, start), *portals, Portal(door, door)]
    result = [start]
    apex = start
    left = start
    right = start
    apex_index = left_index = right_index = 0
    index = 1
    while index < len(sequence):
        new_left = sequence[index].left
        new_right = sequence[index].right

        if _area(apex, right, new_right) <= 0.0:
            if _same_xy(apex, right) or _area(apex, left, new_right) > 0.0:
                right = new_right
                right_index = index
            else:
                result.append(left)
                apex = left
                apex_index = left_index
                left = right = apex
                left_index = right_index = apex_index
                index = apex_index + 1
                continue

        if _area(apex, left, new_left) >= 0.0:
            if _same_xy(apex, left) or _area(apex, right, new_left) < 0.0:
                left = new_left
                left_index = index
            else:
                result.append(right)
                apex = right
                apex_index = right_index
                left = right = apex
                left_index = right_index = apex_index
                index = apex_index + 1
                continue
        index += 1

    if not _same_xy(result[-1], door):
        result.append(door)
    return result


def _point_on_path(polygons: list[Polygon], x: float, y: float, height_hint: float) -> tuple[Vertex, Polygon]:
    candidates: list[tuple[float, Vertex, Polygon]] = []
    for poly in polygons:
        if not _contains_xy(poly, x, y):
            continue
        point = _point_on_poly(poly, x, y)
        vertical = abs(point.z - height_hint)
        if vertical <= 50.0:
            candidates.append((vertical, point, poly))
    if not candidates:
        raise SurveyError(f"surveyed path leaves its polygon corridor near ({x:.2f}, {y:.2f})")
    _difference, point, poly = min(candidates, key=lambda candidate: candidate[0])
    return point, poly


def _survey(tiles: list[Tile], polygons: list[Polygon], start: Vertex, door: Vertex, spacing: float) -> dict[str, list[dict[str, float | str]]]:
    start_match = _find_polygon(polygons, start)
    if start_match is None:
        raise SurveyError("start point is not within the core nearest-polygon extents")
    start_poly, start_on_mesh = start_match
    door_match = _find_polygon(polygons, door)
    if door_match is None:
        raise SurveyError("door point is not within the core nearest-polygon extents")
    door_poly, door_on_mesh = door_match

    graph, by_node = _build_graph(tiles)
    path = _shortest_path(start_poly.node, door_poly.node, graph, by_node)
    portals = []
    for current, following in zip(path, path[1:], strict=False):
        portals.append(_oriented_portal(by_node[current], by_node[following], graph[current][following]))
    route = _funnel(start_on_mesh, door_on_mesh, portals)
    path_polygons = [by_node[node] for node in path]

    result: list[dict[str, float | str]] = []
    for first, second in zip(route, route[1:], strict=False):
        horizontal = math.hypot(second.x - first.x, second.y - first.y)
        pieces = max(1, math.ceil(horizontal / spacing))
        for step in range(pieces):
            fraction = step / pieces
            x = first.x + (second.x - first.x) * fraction
            y = first.y + (second.y - first.y) * fraction
            hint = first.z + (second.z - first.z) * fraction
            point, owner = _point_on_path(path_polygons, x, y, hint)
            result.append({"x": point.x, "y": point.y, "z": point.z, "polygon": owner.identifier})
    final_point, final_owner = _point_on_path(
        path_polygons, door_on_mesh.x, door_on_mesh.y, door_on_mesh.z,
    )
    result.append({"x": final_point.x, "y": final_point.y, "z": final_point.z,
                   "polygon": final_owner.identifier})

    legs = []
    for index, (one, two) in enumerate(zip(result, result[1:], strict=False)):
        first_point = Vertex(float(one["x"]), float(one["y"]), float(one["z"]))
        second_point = Vertex(float(two["x"]), float(two["y"]), float(two["z"]))
        legs.append({
            "from": index,
            "to": index + 1,
            "horizontal_yards": math.hypot(second_point.x - first_point.x, second_point.y - first_point.y),
            "walk_yards": _distance(first_point, second_point),
        })
    return {"points": result, "legs": legs}


def _human_output(data: dict[str, list[dict[str, float | str]]]) -> None:
    print("RoutePoint corridor[] = {")
    for point in data["points"]:
        print(f"    {{{float(point['x']):.4f}f, {float(point['y']):.4f}f, {float(point['z']):.4f}f}}, // {point['polygon']}")
    print("};")
    print("\nleg  horizontal_yards  walk_yards")
    for leg in data["legs"]:
        print(f"{leg['from']:>3}  {float(leg['horizontal_yards']):>16.2f}  {float(leg['walk_yards']):>10.2f}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--maps", required=True, type=Path, help="directory with extracted .mmtile files")
    parser.add_argument("--map-id", required=True, type=int)
    parser.add_argument("--start", required=True, nargs=3, type=float, metavar=("X", "Y", "Z"))
    parser.add_argument("--door", required=True, nargs=3, type=float, metavar=("X", "Y", "Z"))
    parser.add_argument("--spacing-yards", type=float, default=192.0)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args(argv)
    numeric = [*args.start, *args.door, args.spacing_yards]
    if not all(math.isfinite(value) for value in numeric):
        parser.error("coordinates and spacing must be finite")
    if args.map_id < 0 or args.map_id > 999:
        parser.error("map id must be between 0 and 999")
    if args.spacing_yards <= 0:
        parser.error("spacing must be positive")

    try:
        tiles, polygons = _load_tiles(args.maps, args.map_id)
        data = _survey(
            tiles, polygons,
            Vertex(*args.start), Vertex(*args.door), args.spacing_yards,
        )
    except SurveyError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except (OSError, struct.error, OverflowError) as exc:
        print(f"survey input error: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(data, sort_keys=True))
    else:
        _human_output(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
