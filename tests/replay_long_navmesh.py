"""Replay the long-route buffer refusal through the actual walking adapters."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class LongNavmeshReplayTest(unittest.TestCase):
    def test_bounded_legs_and_physical_door_readiness(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        source = Path(
            os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")
        ).read_text()
        decisions = Path(
            os.environ.get(
                "OVERSEER_REPLAY_DECISIONS", ROOT / "src/overseer_decisions.cpp"
            )
        )
        signatures = ["static bool NavmeshRoutes("]
        for signature in (
            "static bool PlanNavmeshRoute(",
            "static bool NavmeshWalkingLeg(",
        ):
            if signature in source:
                signatures.append(signature)
        signatures.extend(
            [
                "static bool GroundedStep(",
                "static OverseerDecisions::AreaTriggerShape TriggerShapeOf(",
                "static std::vector<OverseerDecisions::DungeonRunEntryState> "
                "DungeonRunCensus(",
                "uint32 DungeonRunKnock(",
            ]
        )
        bodies = "\n\n".join(function(source, s) for s in signatures)
        harness = (ROOT / "tests/fixtures/long_navmesh/replay.cpp").read_text()
        harness = harness.replace("// RUNTIME_BODIES", bodies)
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cpp, binary = folder / "replay.cpp", folder / "replay"
            cpp.write_text(harness)
            built = subprocess.run(  # noqa: S603 - fixed repository fixture
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-I",
                    str(ROOT / "src"),
                    str(cpp),
                    str(decisions),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=90,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            replay = subprocess.run(  # noqa: S603 - compiled fixture
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(0, replay.returncode, replay.stdout + replay.stderr)


if __name__ == "__main__":
    unittest.main()
