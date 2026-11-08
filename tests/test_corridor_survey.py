import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/corridor-survey/survey.py"
MMAP_MAGIC = 0x4D4D4150
DT_NAVMESH_MAGIC = 0x444E4156


def game_to_nav(point):
    x, y, z = point
    return y, z, x


def write_tile(path, vertices, polygons, tile_x, tile_y, detail_meshes=None):
    nav_vertices = [game_to_nav(point) for point in vertices]
    poly_bytes = bytearray()
    for entry in polygons:
        if len(entry) == 2 and isinstance(entry[0], (tuple, list)):
            indices, neighbor_fields = entry
        else:
            indices, neighbor_fields = entry, ()
        padded = list(indices) + [0] * (6 - len(indices))
        neighbors = list(neighbor_fields) + [0] * (6 - len(neighbor_fields))
        poly_bytes.extend(struct.pack(
            "<I6H6HHBB", 0xFFFFFFFF, *padded, *neighbors, 1, len(indices), 0
        ))

    vertex_bytes = b"".join(struct.pack("<3f", *point) for point in nav_vertices)
    detail_meshes = detail_meshes or []
    detail_header_bytes = bytearray()
    detail_vertices = []
    detail_triangles = []
    for extra_vertices, triangles in detail_meshes:
        detail_header_bytes.extend(struct.pack(
            "<II4B", len(detail_vertices), len(detail_triangles),
            len(extra_vertices), len(triangles), 0, 0,
        ))
        detail_vertices.extend(game_to_nav(point) for point in extra_vertices)
        detail_triangles.extend(triangles)
    detail_vertex_bytes = b"".join(struct.pack("<3f", *point) for point in detail_vertices)
    detail_triangle_bytes = b"".join(struct.pack("<4B", *triangle) for triangle in detail_triangles)

    mesh_ints = (
        DT_NAVMESH_MAGIC, 7, tile_x, tile_y, 0, 0,
        len(polygons), len(vertices), 0, len(detail_meshes), len(detail_vertices),
        len(detail_triangles), 0, 0, 0,
    )
    mins = tuple(min(point[i] for point in nav_vertices) for i in range(3))
    maxs = tuple(max(point[i] for point in nav_vertices) for i in range(3))
    mesh_floats = (2.0, 0.5, 0.5, *mins, *maxs, 1.0)
    mesh_header = struct.pack("<15i10f", *mesh_ints, *mesh_floats)
    assert len(mesh_header) == 100

    payload = (mesh_header + vertex_bytes + bytes(poly_bytes) +
               bytes(detail_header_bytes) + detail_vertex_bytes + detail_triangle_bytes)
    mmap_header = struct.pack(
        "<4IB3xf4B3I4f",
        MMAP_MAGIC, 7, 20, len(payload), 1,
        45.0, 1, 1, 1, 0, 256, 128, 64,
        533.3333, 0.3, 0.2, 1.8,
    )
    assert len(mmap_header) == 56
    path.write_bytes(mmap_header + payload)


class CorridorSurveyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.maps = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_survey(self, start, door, spacing=5):
        return subprocess.run(
            [
                sys.executable, str(SCRIPT), "--maps", str(self.maps),
                "--map-id", "1", "--start", *map(str, start),
                "--door", *map(str, door), "--spacing-yards", str(spacing),
                "--json",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_finds_route_across_tiles_and_reports_grounded_polygon_points(self):
        write_tile(
            self.maps / "0013333.mmtile",
            [(0, 0, 10), (10, 0, 10), (10, 10, 10)],
            [((0, 1, 2), (0, 0x8002, 0))],
            33, 33,
        )
        write_tile(
            self.maps / "0013334.mmtile",
            [(10, 0, 10), (10, 10, 10), (20, 10, 20)],
            [((0, 1, 2), (0x8006, 0, 0))],
            33, 34,
        )

        result = self.run_survey((5, 5, 10), (15, 7, 15))

        self.assertEqual(0, result.returncode, result.stderr)
        data = json.loads(result.stdout)
        self.assertGreaterEqual(len(data["points"]), 3)
        self.assertTrue(data["legs"])
        for point in data["points"]:
            self.assertTrue(point["polygon"])
            self.assertTrue(all(isinstance(point[k], (int, float)) for k in ("x", "y", "z")))
        self.assertTrue(
            any(abs(point["z"] - 10.0) < 0.01 for point in data["points"]),
            "route points must use the shared edge's tile ground height",
        )

    def test_connects_partial_external_portals_with_detour_side_flags(self):
        write_tile(
            self.maps / "0013333.mmtile",
            [(0, 0, 10), (10, 0, 10), (10, 10, 10)],
            [((0, 1, 2), (0, 0, 0x8002))],
            33, 33,
        )
        write_tile(
            self.maps / "0013334.mmtile",
            [(10, 3, 10), (10, 7, 10), (20, 7, 10)],
            [((0, 1, 2), (0x8006, 0, 0))],
            33, 34,
        )

        result = self.run_survey((5, 5, 10), (12, 6, 10))

        self.assertEqual(0, result.returncode, result.stderr)
        self.assertTrue(json.loads(result.stdout)["legs"])

    def test_disconnected_tiles_report_named_no_path(self):
        write_tile(
            self.maps / "0013333.mmtile",
            [(0, 0, 10), (2, 0, 10), (2, 2, 10)],
            [(0, 1, 2)],
            33, 33,
        )
        write_tile(
            self.maps / "0013334.mmtile",
            [(100, 100, 10), (102, 100, 10), (102, 102, 10)],
            [(0, 1, 2)],
            33, 34,
        )

        result = self.run_survey((1, 1, 10), (101, 101, 10))

        self.assertEqual(2, result.returncode, result.stderr)
        self.assertIn("no polygon path links start and door", result.stderr)

    def test_projects_approach_points_using_core_nearest_poly_extents(self):
        write_tile(
            self.maps / "0013333.mmtile",
            [(0, 0, 10), (10, 0, 10), (0, 10, 10)],
            [(0, 1, 2)],
            33, 33,
        )

        accepted = self.run_survey((-2, 2, 10), (2, -2, 10))
        refused = self.run_survey((-4, 2, 10), (2, 2, 10))

        self.assertEqual(0, accepted.returncode, accepted.stderr)
        point = json.loads(accepted.stdout)["points"][0]
        self.assertAlmostEqual(0.0, point["x"], delta=0.01)
        self.assertAlmostEqual(2.0, point["y"], delta=0.01)
        final_point = json.loads(accepted.stdout)["points"][-1]
        self.assertAlmostEqual(2.0, final_point["x"], delta=0.01)
        self.assertAlmostEqual(0.0, final_point["y"], delta=0.01)
        self.assertEqual(2, refused.returncode)
        self.assertIn("start point is not within the core nearest-polygon extents", refused.stderr)

    def test_corrupt_tile_is_rejected_with_its_filename(self):
        (self.maps / "0013333.mmtile").write_bytes(b"bad tile")

        result = self.run_survey((0, 0, 0), (1, 1, 1))

        self.assertNotEqual(0, result.returncode)
        self.assertIn("0013333.mmtile", result.stderr)

    def test_detail_triangles_supply_the_emitted_ground_height(self):
        write_tile(
            self.maps / "0013333.mmtile",
            [(0, 0, 10), (10, 0, 10), (0, 10, 10)],
            [(0, 1, 2)],
            33, 33,
            detail_meshes=[(
                [(3, 3, 20)],
                [(0, 1, 3, 0), (1, 2, 3, 0), (2, 0, 3, 0)],
            )],
        )

        result = self.run_survey((3, 3, 12), (1, 1, 10))

        self.assertEqual(0, result.returncode, result.stderr)
        point = json.loads(result.stdout)["points"][0]
        self.assertAlmostEqual(20.0, point["z"], delta=0.01)

    def test_synthetic_tiles_replay_the_committed_corridor_rows(self):
        expected_rows = {
            "wailing": [
                (-1050.0, -3664.8, 24.36), (-927.3, -3639.4, 14.85),
                (-906.7, -3466.7, 68.27), (-864.0, -3317.3, 91.87),
                (-864.0, -3125.3, 93.91), (-864.0, -2933.3, 92.05),
                (-885.3, -2762.7, 93.96), (-840.6, -2613.8, 91.77),
                (-842.7, -2421.3, 92.21), (-842.7, -2229.3, 92.12),
                (-847.2, -2050.8, 83.75), (-705.0, -2045.0, 66.45),
                (-627.4, -2030.2, 64.96), (-577.5, -2088.4, 51.5),
                (-603.8, -2162.3, 52.61), (-665.2, -2184.3, 38.82),
                (-733.7, -2214.9, 17.3),
            ],
            "ragefire": [
                (1634.0, -4439.4, 15.76), (1664.0, -4405.3, 19.96),
                (1724.8, -4401.3, 35.43), (1737.1, -4458.7, 37.83),
                (1769.5, -4516.4, 26.63), (1834.7, -4512.3, 22.36),
                (1881.3, -4481.7, 20.5), (1889.2, -4421.7, 13.7),
                (1837.1, -4392.8, 2.1), (1795.7, -4357.3, -12.84),
                (1807.4, -4407.8, -18.43),
            ],
        }

        source = (ROOT / "src/mod_overseer.cpp").read_text()
        for keyword, expected in expected_rows.items():
            committed = self.read_committed_row(source, keyword)
            self.assertEqual(len(expected), len(committed), keyword)
            for wanted, actual in zip(expected, committed, strict=True):
                for wanted_value, actual_value in zip(wanted, actual, strict=True):
                    self.assertAlmostEqual(wanted_value, actual_value, delta=0.01, msg=keyword)

            map_id = 1 if keyword == "wailing" else 0
            tile_x, tile_y = (33, 33) if keyword == "wailing" else (29, 40)
            tile_path = self.maps / f"{map_id:03d}{tile_x:02d}{tile_y:02d}.mmtile"
            write_corridor_tile(tile_path, expected, tile_x, tile_y)
            result = self.run_map_survey(
                map_id, expected[0], expected[-1], spacing=200,
            )
            self.assertEqual(0, result.returncode, f"{keyword}: {result.stderr}")
            points = json.loads(result.stdout)["points"]
            self.assertEqual(len(expected), len(points), keyword)
            for wanted, actual in zip(expected, points, strict=True):
                for axis, wanted_value in zip(("x", "y", "z"), wanted, strict=True):
                    self.assertAlmostEqual(wanted_value, actual[axis], delta=0.05, msg=keyword)

    @staticmethod
    def read_committed_row(source, keyword):
        start = source.index('{"' + keyword + '",')
        depth = 0
        end = start
        index = start
        while index < len(source):
            if source.startswith("//", index):
                newline = source.find("\n", index)
                index = len(source) if newline < 0 else newline + 1
                continue
            if source.startswith("/*", index):
                close = source.find("*/", index + 2)
                if close < 0:
                    raise AssertionError("unterminated source comment")
                index = close + 2
                continue
            character = source[index]
            if character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    break
            index += 1
        row = source[start:end]
        triples = re.findall(
            r"\{\s*(-?\d+(?:\.\d+)?)f,\s*(-?\d+(?:\.\d+)?)f,\s*(-?\d+(?:\.\d+)?)f\s*\}",
            row,
        )
        return [tuple(map(float, triple)) for triple in triples]

    def run_map_survey(self, map_id, start, door, spacing):
        return subprocess.run(
            [
                sys.executable, str(SCRIPT), "--maps", str(self.maps),
                "--map-id", str(map_id), "--start", *map(str, start),
                "--door", *map(str, door), "--spacing-yards", str(spacing),
                "--json",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )


def write_corridor_tile(path, row, tile_x, tile_y):
    cross_sections = []
    width = 0.02
    for index, point in enumerate(row):
        previous = row[max(0, index - 1)]
        following = row[min(len(row) - 1, index + 1)]
        dx, dy = following[0] - previous[0], following[1] - previous[1]
        length = math.hypot(dx, dy)
        nx, ny = (-dy / length, dx / length) if length else (0.0, 1.0)
        cross_sections.append((
            (point[0] + nx * width, point[1] + ny * width, point[2]),
            (point[0] - nx * width, point[1] - ny * width, point[2]),
        ))

    vertices = []
    for left, right in cross_sections:
        vertices.extend((left, right))

    polygons = []
    for index in range(len(row) - 1):
        vertices_at = (2 * index, 2 * index + 1, 2 * index + 3, 2 * index + 2)
        neighbors = [0, 0, index + 2 if index + 1 < len(row) - 1 else 0, 0]
        if index:
            neighbors[0] = index
        polygons.append((vertices_at, neighbors))

    write_tile(path, vertices, polygons, tile_x, tile_y)


if __name__ == "__main__":
    unittest.main()
