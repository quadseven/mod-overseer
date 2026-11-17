"""Replay guarded route marks and second-search frequency from an offline bundle."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import sys
from typing import Any, Iterable, Literal, Sequence, TypedDict


PLAYER_SIDE_MASK = 2 | 4
GUARDED_SPACING_YARDS = 30.0
GUARDED_RADIUS_YARDS = 60.0
DENSITY_REACH_PAD_YARDS = 5.0
MAP_NAMES = {0: "Eastern Kingdoms", 1: "Kalimdor"}
Scenario = Literal["today", "b_only", "a_n2", "a_n3"]
SCENARIOS: tuple[Scenario, ...] = ("today", "b_only", "a_n2", "a_n3")


class ScenarioMarks(TypedDict):
    today: tuple[bool, ...]
    b_only: tuple[bool, ...]
    a_n2: tuple[bool, ...]
    a_n3: tuple[bool, ...]


class ScenarioStats(TypedDict):
    today: RouteStats
    b_only: RouteStats
    a_n2: RouteStats
    a_n3: RouteStats


@dataclass(frozen=True)
class Node:
    id: int
    map_id: int
    x: float
    y: float
    z: float


@dataclass(frozen=True)
class Link:
    source: int
    target: int
    kind: str
    distance: float
    path: tuple[tuple[float, float, float], ...] = ()
    extra_cost: float = 0.0
    guarded: bool = False


@dataclass(frozen=True)
class FactionTemplate:
    id: int
    faction: int
    our_mask: int
    hostile_mask: int
    enemy_factions: tuple[int, ...] = ()
    friend_factions: tuple[int, ...] = ()


@dataclass(frozen=True)
class Spawn:
    map_id: int
    x: float
    y: float
    z: float
    max_level: int
    rank: int
    detection_range: float
    faction_template_id: int
    can_be_fought: bool = True
    trigger: bool = False
    civilian: bool = False
    active: bool = True


@dataclass(frozen=True)
class RouteStats:
    total_legs: int
    marked_legs: int
    planned_routes: int
    second_searches: int

    @property
    def marked_leg_share(self) -> float | None:
        if self.total_legs == 0:
            return None
        return self.marked_legs / self.total_legs

    @property
    def second_search_share(self) -> float | None:
        if self.planned_routes == 0:
            return None
        return self.second_searches / self.planned_routes


@dataclass(frozen=True)
class ReplayInput:
    nodes: tuple[Node, ...]
    links: tuple[Link, ...]
    spawns: tuple[Spawn, ...]
    factions: dict[int, FactionTemplate]
    player_templates: dict[str, int]


class CostHeap:
    """The strict-comparison heap used by WalkOnFoot in the C++ planner."""

    def __init__(self) -> None:
        self._costs: list[float] = []
        self._nodes: list[int] = []

    def __bool__(self) -> bool:
        return bool(self._nodes)

    def push(self, cost: float, node: int) -> None:
        self._costs.append(cost)
        self._nodes.append(node)
        child = len(self._nodes) - 1
        while child > 0:
            parent = (child - 1) // 2
            if self._costs[parent] <= self._costs[child]:
                break
            self._costs[parent], self._costs[child] = (
                self._costs[child],
                self._costs[parent],
            )
            self._nodes[parent], self._nodes[child] = (
                self._nodes[child],
                self._nodes[parent],
            )
            child = parent

    def pop(self) -> tuple[float, int]:
        cost, node = self._costs[0], self._nodes[0]
        self._costs[0] = self._costs[-1]
        self._nodes[0] = self._nodes[-1]
        self._costs.pop()
        self._nodes.pop()
        parent = 0
        while True:
            left = parent * 2 + 1
            right = left + 1
            smallest = parent
            if left < len(self._nodes) and self._costs[left] < self._costs[smallest]:
                smallest = left
            if right < len(self._nodes) and self._costs[right] < self._costs[smallest]:
                smallest = right
            if smallest == parent:
                break
            self._costs[parent], self._costs[smallest] = (
                self._costs[smallest],
                self._costs[parent],
            )
            self._nodes[parent], self._nodes[smallest] = (
                self._nodes[smallest],
                self._nodes[parent],
            )
            parent = smallest
        return cost, node


class SpatialIndex:
    def __init__(self, spawns: Sequence[Spawn], cell_size: float):
        self.spawns = spawns
        self.cell_size = max(cell_size, 1.0)
        self.cells: dict[tuple[int, int], list[int]] = {}
        for index, spawn in enumerate(spawns):
            key = self._cell(spawn.x, spawn.y)
            self.cells.setdefault(key, []).append(index)

    def _cell(self, x: float, y: float) -> tuple[int, int]:
        return math.floor(x / self.cell_size), math.floor(y / self.cell_size)

    def near(self, x: float, y: float, radius: float) -> Iterable[tuple[int, Spawn]]:
        center_x, center_y = self._cell(x, y)
        reach = math.ceil(radius / self.cell_size)
        radius_squared = radius * radius
        for cell_x in range(center_x - reach, center_x + reach + 1):
            for cell_y in range(center_y - reach, center_y + reach + 1):
                for index in self.cells.get((cell_x, cell_y), ()):
                    spawn = self.spawns[index]
                    dx = spawn.x - x
                    dy = spawn.y - y
                    if dx * dx + dy * dy <= radius_squared:
                        yield index, spawn


def core_hostile_to(source: FactionTemplate, target: FactionTemplate) -> bool:
    """Mirror FactionTemplateEntry::IsHostileTo from the pinned core."""
    if target.faction:
        if target.faction in source.enemy_factions:
            return True
        if target.faction in source.friend_factions:
            return False
    return (source.hostile_mask & target.our_mask) != 0


def other_side(source: FactionTemplate, player: FactionTemplate) -> bool:
    return (source.our_mask & PLAYER_SIDE_MASK & ~player.our_mask) != 0


def grey_level(player_level: int) -> int:
    if player_level <= 5:
        return 0
    if player_level <= 39:
        return player_level - 5 - player_level // 10
    if player_level <= 59:
        return player_level - 1 - player_level // 5
    return player_level - 9


def con_band(player_level: int, creature_level: int) -> str:
    if creature_level >= player_level + 10:
        return "skull"
    if creature_level > grey_level(player_level):
        return "not_grey"
    return "grey"


def mark_scenarios(
    nodes: Sequence[Node],
    links: Sequence[Link],
    spawns: Sequence[Spawn],
    factions: dict[int, FactionTemplate],
    player: FactionTemplate,
    walker_level: int,
) -> ScenarioMarks:
    """Mark each directed leg for today's rule, B, and A at N=2 or N=3."""
    nodes_by_id: dict[int, Node] = {}
    for node in nodes:
        nodes_by_id.setdefault(node.id, node)

    b_by_map: dict[int, list[Spawn]] = {}
    density_by_map: dict[int, list[Spawn]] = {}
    for spawn in spawns:
        template = factions.get(spawn.faction_template_id)
        if template is None or not core_hostile_to(template, player):
            continue
        if spawn.can_be_fought and spawn.active:
            b_by_map.setdefault(spawn.map_id, []).append(spawn)
        if spawn.can_be_fought and not spawn.trigger and not spawn.civilian:
            density_by_map.setdefault(spawn.map_id, []).append(spawn)

    output = {name: [False] * len(links) for name in SCENARIOS}
    indexes: dict[int, tuple[SpatialIndex, SpatialIndex, SpatialIndex, float]] = {}
    for map_id in set(b_by_map) | set(density_by_map):
        today_candidates: list[Spawn] = []
        b_candidates: list[Spawn] = []
        for spawn in b_by_map.get(map_id, ()):
            template = factions[spawn.faction_template_id]
            opposite = other_side(template, player)
            band = con_band(walker_level, spawn.max_level)
            today = opposite and band == "skull"
            b = today or (not opposite and spawn.rank in (1, 2) and band != "grey")
            if today:
                today_candidates.append(spawn)
            if b:
                b_candidates.append(spawn)
        density_spawns = density_by_map.get(map_id, ())
        max_reach = max(
            (
                max(spawn.detection_range, 0.0) + DENSITY_REACH_PAD_YARDS
                for spawn in density_spawns
            ),
            default=0.0,
        )
        indexes[map_id] = (
            SpatialIndex(today_candidates, GUARDED_RADIUS_YARDS),
            SpatialIndex(b_candidates, GUARDED_RADIUS_YARDS),
            SpatialIndex(density_spawns, max_reach or 1.0),
            max_reach,
        )

    for index, link in enumerate(links):
        source_node = nodes_by_id.get(link.source)
        target_node = nodes_by_id.get(link.target)
        if link.kind != "walk" or source_node is None or target_node is None:
            continue
        if source_node.map_id != target_node.map_id:
            continue
        map_id = source_node.map_id
        sampled = _guard_samples(link, target_node)
        index_rows = indexes.get(map_id)
        if index_rows is None:
            today_index = SpatialIndex((), GUARDED_RADIUS_YARDS)
            b_index = SpatialIndex((), GUARDED_RADIUS_YARDS)
            density_index = SpatialIndex((), 1.0)
            max_reach = 0.0
        else:
            today_index, b_index, density_index, max_reach = index_rows
        output["today"][index] = _has_spawn_near(sampled, today_index, 60.0)
        output["b_only"][index] = _has_spawn_near(sampled, b_index, 60.0)

        density_spawns = density_by_map.get(map_id, ())
        near_node = _density_count(
            (source_node, target_node), density_index, density_spawns, max_reach
        )
        output["a_n2"][index] = output["b_only"][index] or near_node >= 2
        output["a_n3"][index] = output["b_only"][index] or near_node >= 3

    return ScenarioMarks(**{name: tuple(values) for name, values in output.items()})


def _guard_samples(link: Link, target_node: Node) -> tuple[tuple[float, float], ...]:
    points = link.path or ((target_node.x, target_node.y, target_node.z),)
    samples: list[tuple[float, float]] = []
    carried = GUARDED_SPACING_YARDS
    last_x = 0.0
    last_y = 0.0
    for index, point in enumerate(points):
        x, y = point[0], point[1]
        if index:
            dx = x - last_x
            dy = y - last_y
            carried += math.sqrt(dx * dx + dy * dy)
        last_x, last_y = x, y
        if carried < GUARDED_SPACING_YARDS and index + 1 != len(points):
            continue
        carried = 0.0
        samples.append((x, y))
    return tuple(samples)


def _has_spawn_near(
    points: Sequence[tuple[float, float]], index: SpatialIndex, radius: float
) -> bool:
    return any(next(index.near(x, y, radius), None) is not None for x, y in points)


def _density_count(
    nodes: Sequence[Node],
    index: SpatialIndex,
    spawns: Sequence[Spawn],
    max_reach: float,
) -> int:
    if not spawns:
        return 0
    found: set[int] = set()
    for node in nodes:
        for spawn_index, spawn in index.near(node.x, node.y, max_reach):
            reach = max(spawn.detection_range, 0.0) + DENSITY_REACH_PAD_YARDS
            dx = spawn.x - node.x
            dy = spawn.y - node.y
            if dx * dx + dy * dy <= reach * reach:
                found.add(spawn_index)
    return len(found)


def route_stats(
    nodes: Sequence[Node],
    links: Sequence[Link],
    map_id: int,
    min_gain_yards: float = 200.0,
    allow_round: bool = True,
) -> RouteStats:
    marks = tuple(link.guarded for link in links)
    return route_stats_multi(
        nodes,
        links,
        map_id,
        {name: marks for name in SCENARIOS},
        min_gain_yards=min_gain_yards,
        allow_round=allow_round,
    )["today"]


def route_stats_multi(
    nodes: Sequence[Node],
    links: Sequence[Link],
    map_id: int,
    marks: ScenarioMarks,
    min_gain_yards: float = 200.0,
    allow_round: bool = True,
) -> ScenarioStats:
    """Measure the C++ planner's second-search gate on all reachable node pairs."""
    local_nodes: list[Node] = []
    local_index: dict[int, int] = {}
    for node in sorted(nodes, key=lambda row: row.id):
        if node.map_id == map_id and node.id not in local_index:
            local_index[node.id] = len(local_nodes)
            local_nodes.append(node)

    names = SCENARIOS
    if any(len(values) != len(links) for values in marks.values()):
        raise ValueError("each scenario must have one mark per input link")
    if not local_nodes:
        return ScenarioStats(**{name: RouteStats(0, 0, 0, 0) for name in names})

    adjacency: list[list[tuple[int, float, int]]] = [[] for _ in local_nodes]
    eligible: list[tuple[int, int, int, float]] = []
    for input_index, link in enumerate(links):
        if link.kind != "walk":
            continue
        source = local_index.get(link.source)
        target = local_index.get(link.target)
        if source is None or target is None:
            continue
        cost = _f32(_f32(link.distance) + _f32(link.extra_cost))
        if not math.isfinite(cost) or cost < 0.0:
            continue
        adjacency[source].append((target, cost, input_index))
        eligible.append((source, target, input_index, cost))

    totals = {name: [0, 0] for name in names}
    marked = {
        name: sum(bool(marks[name][input_index]) for _, _, input_index, _ in eligible)
        for name in names
    }
    total_legs = len(eligible)
    for start, start_node in enumerate(local_nodes):
        best: list[float | None] = [None] * len(local_nodes)
        came: list[int | None] = [None] * len(local_nodes)
        came_edge: list[int | None] = [None] * len(local_nodes)
        reached = [False] * len(local_nodes)
        order: list[int] = []
        open_nodes = CostHeap()
        best[start] = 0.0
        came[start] = start
        open_nodes.push(0.0, start)
        while open_nodes:
            cost, at = open_nodes.pop()
            if reached[at]:
                continue
            reached[at] = True
            order.append(at)
            for target, edge_cost, input_index in adjacency[at]:
                if reached[target]:
                    continue
                through = _f32(cost + edge_cost)
                if best[target] is None or through < best[target]:
                    best[target] = through
                    came[target] = at
                    came_edge[target] = input_index
                    open_nodes.push(through, target)

        path_guarded = {name: [False] * len(local_nodes) for name in names}
        for at in order:
            if at == start:
                continue
            parent = came[at]
            edge = came_edge[at]
            if parent is None or edge is None:
                continue
            for name in names:
                path_guarded[name][at] = path_guarded[name][parent] or bool(
                    marks[name][edge]
                )

        for target, target_node in enumerate(local_nodes):
            if target == start or not reached[target]:
                continue
            distance = _plane_distance(
                start_node.x, start_node.y, target_node.x, target_node.y
            )
            if distance < min_gain_yards:
                continue
            for name in names:
                totals[name][0] += 1
                if allow_round and path_guarded[name][target]:
                    totals[name][1] += 1

    return ScenarioStats(
        **{
            name: RouteStats(total_legs, marked[name], totals[name][0], totals[name][1])
            for name in names
        }
    )


def _plane_distance(ax: float, ay: float, bx: float, by: float) -> float:
    dx = _f32(_f32(ax) - _f32(bx))
    dy = _f32(_f32(ay) - _f32(by))
    return _f32(math.sqrt(_f32(_f32(dx * dx) + _f32(dy * dy))))


def _f32(value: float) -> float:
    import struct

    return struct.unpack("<f", struct.pack("<f", value))[0]


def _load_data(path: Path) -> ReplayInput:
    raw: Any = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("the JSON root must be an object")
    nodes = [Node(**row) for row in raw.get("nodes", [])]
    links = [
        Link(
            source=row["source"],
            target=row["target"],
            kind=row["kind"],
            distance=row["distance"],
            path=tuple(tuple(point) for point in row.get("path", [])),
            extra_cost=row.get("extra_cost", 0.0),
        )
        for row in raw.get("links", [])
    ]
    faction_rows = raw.get("factions", [])
    for row in faction_rows:
        if not isinstance(row, dict):
            raise ValueError("each faction row must be an object")
        if (
            len(row.get("enemy_factions", ())) != 4
            or len(row.get("friend_factions", ())) != 4
        ):
            raise ValueError("each faction needs all four enemy and friend slots")
    factions = {
        row["id"]: FactionTemplate(
            id=row["id"],
            faction=row["faction"],
            our_mask=row["our_mask"],
            hostile_mask=row["hostile_mask"],
            enemy_factions=tuple(row.get("enemy_factions", [])),
            friend_factions=tuple(row.get("friend_factions", [])),
        )
        for row in raw.get("factions", [])
    }
    spawn_rows = raw.get("spawns", [])
    required_spawn_fields = {
        "map_id",
        "x",
        "y",
        "z",
        "max_level",
        "rank",
        "detection_range",
        "faction_template_id",
        "can_be_fought",
        "trigger",
        "civilian",
        "active",
    }
    for row in spawn_rows:
        if not isinstance(row, dict) or not required_spawn_fields.issubset(row):
            raise ValueError("each spawn needs the complete core-derived flag set")
    spawns = [Spawn(**row) for row in spawn_rows]
    player_rows = raw.get("player_templates", {})
    if not isinstance(player_rows, dict):
        raise ValueError("player_templates must name the two player faction templates")
    players = {
        side: int(player_rows[side])
        for side in ("alliance", "horde")
        if side in player_rows
    }
    return ReplayInput(tuple(nodes), tuple(links), tuple(spawns), factions, players)


def run_report(path: Path, walker_level: int, min_gain_yards: float) -> int:
    data = _load_data(path)
    nodes, links, spawns = data.nodes, data.links, data.spawns
    factions, players = data.factions, data.player_templates
    if not nodes or not links:
        print("NOT MEASURED: the input has no travel nodes or links")
        return 2
    if not spawns or not factions:
        print("NOT MEASURED: the input has no complete spawn or faction data")
        return 2
    if any(side not in players for side in ("alliance", "horde")):
        print("NOT MEASURED: the input has no alliance or horde player template")
        return 2
    missing_templates = {
        spawn.faction_template_id
        for spawn in spawns
        if spawn.faction_template_id and spawn.faction_template_id not in factions
    }
    if missing_templates:
        print("NOT MEASURED: a spawn refers to an absent faction template")
        return 2
    measured = False
    for map_id, map_name in MAP_NAMES.items():
        map_nodes = [node for node in nodes if node.map_id == map_id]
        if not map_nodes:
            print(f"NOT MEASURED: map {map_id} ({map_name}) has no travel nodes")
            continue
        if not any(spawn.map_id == map_id for spawn in spawns):
            print(f"NOT MEASURED: {map_name} has no creature spawn rows")
            continue
        for side in ("alliance", "horde"):
            template_id = players.get(side)
            player = factions.get(template_id)
            if player is None:
                print(f"NOT MEASURED: no {side} player faction template in input")
                continue
            marks = mark_scenarios(
                nodes, links, spawns, factions, player, walker_level=walker_level
            )
            stats = route_stats_multi(
                nodes,
                links,
                map_id,
                marks,
                min_gain_yards=min_gain_yards,
            )
            for scenario in SCENARIOS:
                row = stats[scenario]
                if not row.total_legs or not row.planned_routes:
                    print(
                        f"NOT MEASURED: {map_name} {side} {scenario} has no "
                        "eligible walk legs or planned routes"
                    )
                    continue
                measured = True
                print(
                    f"{map_name} side={side} level={walker_level} "
                    f"scenario={scenario} "
                    f"marked_legs={row.marked_legs}/{row.total_legs} "
                    f"({100.0 * row.marked_leg_share:.2f}%) "
                    f"second_searches={row.second_searches}/{row.planned_routes} "
                    f"({100.0 * row.second_search_share:.2f}%)"
                )
    return 0 if measured else 2


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", type=Path, required=True, help="normalized JSON export"
    )
    parser.add_argument("--level", type=int, default=60, help="solo walker level")
    parser.add_argument(
        "--minimum-gain",
        type=float,
        default=200.0,
        help="PlanFootRoute minimum aim progress in yards",
    )
    args = parser.parse_args(argv)
    try:
        return run_report(args.input, args.level, args.minimum_gain)
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"NOT MEASURED: invalid input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
