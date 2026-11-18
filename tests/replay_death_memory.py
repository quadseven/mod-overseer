"""Run the patched grind-spot picker against a bot with a record of dying.

tests/fixtures/death_memory/ holds verbatim pin text (with patch 0027 applied)
of NewRpgBaseAction::SelectRandomGrindPos, the head of NewRpgInfo.h and
ReleaseSpiritAction::IncrementDeathCount. The real patch is applied to them
with git apply and the picker is compiled against stand-ins for the types it
touches. The same scenarios run on the unpatched picker (with the patched
header, so the scenarios compile), and every scenario that is about dying must
fail there. No realm or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0028-a-grind-spot-that-keeps-killing-a-bot-is-not-picked-again.patch"
FIXTURES = HERE / "fixtures/death_memory"
FILES = {
    "NewRpgBaseAction.cpp": "src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp",
    "NewRpgInfo.h": "src/Ai/World/Rpg/NewRpgInfo.h",
    "ReleaseSpiritAction.cpp": "src/Ai/Base/Actions/ReleaseSpiritAction.cpp",
}

SCENARIOS = {
    "record_drops_expired_and_caps",
    "one_death_changes_nothing",
    "two_deaths_retire_the_spot",
    "old_deaths_are_forgotten",
    "hot_ground_on_the_way_is_avoided",
    "a_bot_on_hot_ground_can_leave",
    "never_strands_a_bot",
    "other_map_deaths_are_ignored",
    "no_brain_no_memory",
}

# What the old picker already gets right: it ignores deaths, so only the
# scenarios where ignoring them is the right answer pass on it.
OLD_PASSES = {
    "record_drops_expired_and_caps",
    "one_death_changes_nothing",
    "old_deaths_are_forgotten",
    "never_strands_a_bot",
    "other_map_deaths_are_ignored",
    "no_brain_no_memory",
}


def run_scenarios(header_text, body_text, folder, label):
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if compiler is None:
        raise unittest.SkipTest("C++17 compiler required")
    stubs = folder / "stubs"
    stubs.mkdir(exist_ok=True)
    for name in ("Define.h", "ObjectGuid.h", "ObjectMgr.h", "QuestDef.h", "Strategy.h", "Timer.h", "TravelMgr.h"):
        (stubs / name).write_text("")
    harness = (FIXTURES / "replay.cpp").read_text()
    cpp = folder / f"replay_{label}.cpp"
    cpp.write_text(harness.replace("// RUNTIME_HEADER", header_text).replace("// RUNTIME_BODIES", body_text))
    binary = folder / f"replay_{label}"
    built = subprocess.run(  # noqa: S603 - fixed repository sources
        [compiler, "-std=c++17", "-w", "-I", str(stubs), str(cpp), "-o", str(binary)],
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


class DeathMemoryReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        tree = folder / "tree"
        for name, target in FILES.items():
            (tree / target).parent.mkdir(parents=True, exist_ok=True)
            (tree / target).write_bytes((FIXTURES / name).read_bytes())
        subprocess.run(["git", "init", "-q"], cwd=tree, check=True)  # noqa: S607
        subprocess.run(  # noqa: S603, S607
            ["git", "apply", "--whitespace=error", str(PATCH)], cwd=tree, check=True
        )
        header = (tree / FILES["NewRpgInfo.h"]).read_text()
        cls.release = (tree / FILES["ReleaseSpiritAction.cpp"]).read_text()
        cls.new = run_scenarios(header, (tree / FILES["NewRpgBaseAction.cpp"]).read_text(), folder, "new")
        cls.old = run_scenarios(header, (FIXTURES / "NewRpgBaseAction.cpp").read_text(), folder, "old")

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

    def test_a_release_records_where_the_bot_died(self):
        """The death is written after the battleground return and before the count."""
        text = self.release
        record = text.index("botAI->rpgInfo.deaths.Record(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY()")
        self.assertGreater(record, text.index("if (bot->InBattleground())"))
        self.assertLess(record, text.index("Death Count to prevent skeleton piles"))


if __name__ == "__main__":
    unittest.main()
