"""Offline route-cost replay tests for issue #502."""

import pathlib
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "route_cost_replay"))

from replay_routes import (  # noqa: E402
    FactionTemplate,
    Link,
    Node,
    Spawn,
    core_hostile_to,
    mark_scenarios,
    route_stats,
    run_report,
)


class CoreFactionHostility(unittest.TestCase):
    def test_explicit_enemy_and_friend_relations_precede_masks(self):
        target = FactionTemplate(1, faction=10, our_mask=3, hostile_mask=0)
        enemy = FactionTemplate(
            2,
            faction=20,
            our_mask=8,
            hostile_mask=0,
            enemy_factions=(10,),
            friend_factions=(10,),
        )
        friend = FactionTemplate(
            3,
            faction=30,
            our_mask=8,
            hostile_mask=3,
            friend_factions=(10,),
        )

        self.assertTrue(core_hostile_to(enemy, target))
        self.assertFalse(core_hostile_to(friend, target))

    def test_hostile_mask_uses_the_target_our_mask(self):
        source = FactionTemplate(1, faction=20, our_mask=8, hostile_mask=2)
        alliance = FactionTemplate(2, faction=10, our_mask=3, hostile_mask=0)
        horde = FactionTemplate(3, faction=11, our_mask=5, hostile_mask=0)

        self.assertTrue(core_hostile_to(source, alliance))
        self.assertFalse(core_hostile_to(source, horde))


class MarkedLegScenarios(unittest.TestCase):
    def setUp(self):
        self.nodes = [
            Node(1, 0, 0, 0, 0),
            Node(2, 0, 100, 0, 0),
            Node(3, 0, 200, 0, 0),
            Node(4, 0, 400, 0, 0),
        ]
        self.links = [
            Link(1, 2, "walk", 100, ((0, 0, 0), (50, 0, 0), (100, 0, 0))),
            Link(2, 3, "walk", 100, ((100, 0, 0), (150, 0, 0), (200, 0, 0))),
            Link(3, 4, "walk", 100, ((270, 0, 0), (330, 0, 0), (400, 0, 0))),
        ]
        self.factions = {
            1: FactionTemplate(1, faction=10, our_mask=3, hostile_mask=0),
            2: FactionTemplate(2, faction=20, our_mask=5, hostile_mask=2),
            3: FactionTemplate(3, faction=30, our_mask=8, hostile_mask=1),
        }
        self.spawns = [
            Spawn(0, 5, 0, 0, 70, 0, 20, 2),
            Spawn(0, 150, 0, 0, 60, 1, 20, 3),
            Spawn(0, 5, 0, 0, 20, 0, 20, 3),
            Spawn(0, 10, 0, 0, 20, 0, 20, 3),
            Spawn(0, 45, 0, 0, 20, 0, 20, 3),
            Spawn(0, 205, 0, 0, 20, 0, 20, 3),
            Spawn(0, 210, 0, 0, 20, 0, 20, 3),
            Spawn(0, 245, 0, 0, 20, 0, 20, 3),
        ]

    def test_today_b_and_density_thresholds_are_separate(self):
        alliance = self.factions[1]
        marks = mark_scenarios(
            self.nodes,
            self.links,
            self.spawns,
            self.factions,
            alliance,
            walker_level=60,
        )

        self.assertEqual(marks["today"], (True, False, False))
        self.assertEqual(marks["b_only"], (True, True, False))
        self.assertEqual(marks["a_n2"], (True, True, True))
        self.assertEqual(marks["a_n3"], (True, True, False))

    def test_non_walk_links_are_not_marked(self):
        non_walk = [Link(1, 2, "flight", 100, ((0, 0, 0), (100, 0, 0)))]
        marks = mark_scenarios(
            self.nodes,
            non_walk,
            self.spawns,
            self.factions,
            self.factions[1],
            walker_level=60,
        )

        self.assertEqual(marks["today"], (False,))
        self.assertEqual(marks["b_only"], (False,))


class RouteWorkload(unittest.TestCase):
    def test_counts_reachable_ordered_pairs_and_guarded_first_paths(self):
        nodes = [
            Node(1, 0, 0, 0, 0),
            Node(2, 0, 100, 0, 0),
            Node(3, 0, 200, 0, 0),
            Node(4, 0, 300, 0, 0),
        ]
        links = [
            Link(1, 2, "walk", 100, guarded=True),
            Link(2, 3, "walk", 100),
            Link(1, 4, "walk", 300),
            Link(4, 3, "walk", 100),
        ]

        stats = route_stats(nodes, links, map_id=0, min_gain_yards=25)

        self.assertEqual((stats.total_legs, stats.marked_legs), (4, 1))
        self.assertEqual((stats.planned_routes, stats.second_searches), (5, 2))
        self.assertAlmostEqual(stats.marked_leg_share, 0.25)
        self.assertAlmostEqual(stats.second_search_share, 0.4)

    def test_empty_graph_reports_no_measurement(self):
        stats = route_stats([], [], map_id=0)

        self.assertEqual(stats.planned_routes, 0)
        self.assertIsNone(stats.second_search_share)
        self.assertIsNone(stats.marked_leg_share)


class EmptyInputReporting(unittest.TestCase):
    def test_missing_export_is_not_reported_as_zero_cost(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "empty.json"
            path.write_text("{}", encoding="utf-8")
            output = StringIO()
            with redirect_stdout(output):
                result = run_report(path, walker_level=60, min_gain_yards=200)

        self.assertEqual(result, 2)
        self.assertIn("NOT MEASURED", output.getvalue())
        self.assertNotIn("0.00%", output.getvalue())


if __name__ == "__main__":
    unittest.main()
