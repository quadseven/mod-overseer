"""Run the patched grind-spot and camp pickers against a bot in a bare zone.

tests/fixtures/zone_fallback/NewRpgBaseAction.cpp holds the verbatim pin text of
NewRpgBaseAction::SelectRandomGrindPos and ::SelectRandomCampPos. The real patch
is applied to it with git apply and the result is compiled against stand-ins
for the types it touches. The same scenarios run on the unpatched text, so a
scenario the old code passes is listed in OLD_PASSES and every other one must
fail on it. No realm or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0027-a-bot-in-a-zone-with-no-spot-picks-one-from-a-neighbouring-zone.patch"
FIXTURES = HERE / "fixtures/zone_fallback"
TARGET = "src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp"

SCENARIOS = {
    "sea_bot_gets_a_grind_spot",
    "sea_bot_grind_spot_is_in_reach",
    "sea_bot_gets_a_camp",
    "own_zone_spot_still_wins",
    "zone_without_spot_borrows_the_neighbour",
    "far_spot_stays_out_of_reach",
    "other_map_stays_out",
    "capital_unchanged",
}

# What the unpatched code already gets right: the guards that keep the fix from
# reaching too far.
OLD_PASSES = {
    "own_zone_spot_still_wins",
    "far_spot_stays_out_of_reach",
    "other_map_stays_out",
    "capital_unchanged",
}


def run_scenarios(source_text, folder, label):
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise unittest.SkipTest("C++17 compiler required")
    harness = (FIXTURES / "replay.cpp").read_text()
    cpp = folder / f"replay_{label}.cpp"
    cpp.write_text(harness.replace("// RUNTIME_BODIES", source_text))
    binary = folder / f"replay_{label}"
    built = subprocess.run(  # noqa: S603 - fixed repository sources
        [compiler, "-std=c++17", "-w", str(cpp), "-o", str(binary)],
        capture_output=True,
        text=True,
        timeout=90,
    )
    if built.returncode:
        raise AssertionError(built.stdout + built.stderr)
    ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)  # noqa: S603
    results = {}
    for line in ran.stdout.splitlines():
        verdict, name = line.split(" ", 1)
        results[name] = verdict == "PASS"
    return results


class ZoneFallbackReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        original = (FIXTURES / "NewRpgBaseAction.cpp").read_text()
        tree = folder / "tree"
        (tree / TARGET).parent.mkdir(parents=True)
        (tree / TARGET).write_text(original)
        subprocess.run(["git", "init", "-q"], cwd=tree, check=True)  # noqa: S607
        subprocess.run(  # noqa: S603, S607
            ["git", "apply", "--whitespace=error", str(PATCH)], cwd=tree, check=True
        )
        patched = (tree / TARGET).read_text()
        cls.new = run_scenarios(patched, folder, "new")
        cls.old = run_scenarios(original, folder, "old")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_every_scenario_ran(self):
        self.assertEqual(SCENARIOS, set(self.new))
        self.assertEqual(SCENARIOS, set(self.old))

    def test_the_patched_code_passes_every_scenario(self):
        self.assertEqual([], sorted(n for n, ok in self.new.items() if not ok))

    def test_the_old_code_fails_the_rest(self):
        self.assertEqual(OLD_PASSES, {n for n, ok in self.old.items() if ok})


if __name__ == "__main__":
    unittest.main()
