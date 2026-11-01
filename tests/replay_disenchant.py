"""Run the actual disenchant adapter and bound SQL against owned-item/SQLite fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from replay_run_family import function

ROOT = Path(__file__).resolve().parents[1]


class DisenchantReplay(unittest.TestCase):
    def test_owned_item_loot_and_persisted_errand(self):
        source = Path(os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")).read_text()
        start = source.index("    struct DisenchantTask\n")
        end = source.index("    // ------------------------------------- job='craft'", start)
        bodies = source[start:end]
        update = function(source, "void OnUpdate(uint32 diff)")
        self.assertIn("_disenchantQueries.ProcessReadyCallbacks()", update)
        self.assertIn("DriveDisenchant();", update)
        self.assertIn("ReadDisenchantCommands();", update)
        hook = function(source, "void OnPlayerBeforeSendLoot(Player* player, ObjectGuid lootGuid, Loot* loot)")
        self.assertIn("OverseerWorldScript::TakeDisenchantLoot", hook)
        delivery = function(source, "void DeliverPendingCommands(uint32 sincePollMs)")
        self.assertIn("kind <> 'disenchant'", delivery)
        craft = function(source, "void DriveCraft()")
        self.assertIn("HasDisenchantProof(name)", craft)
        fixture = (ROOT / "tests/fixtures/disenchant/replay.cpp").read_text()
        self.assertEqual(fixture.count("// RUNTIME_METHODS"), 1)
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 and SQLite development files required")
        with tempfile.TemporaryDirectory() as temp:
            cpp, binary = Path(temp) / "replay.cpp", Path(temp) / "replay"
            cpp.write_text(fixture.replace("// RUNTIME_METHODS", bodies))
            built = subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra",
                "-I" + str(ROOT / "tests/fixtures/disenchant"),
                "-I" + str(ROOT / "src"), str(cpp), "-lsqlite3", "-o", str(binary),
            ], capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
