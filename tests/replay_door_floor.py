"""Compile actual door-height and route adapters against fixed navigation readings."""

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


class DoorFloorReplayTest(unittest.TestCase):
    def test_native_door_floor_and_route_adapters(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        source = (ROOT / "src/mod_overseer.cpp").read_text()
        bodies = "\n\n".join(
            function(source, signature)
            for signature in (
                "static bool NavmeshRoutes(",
                "static OverseerDecisions::AreaTriggerShape TriggerShapeOf(",
                "static OverseerDecisions::DoorAimHeight DoorAimHeightFor(",
            )
        )
        harness = (ROOT / "tests/fixtures/door_floor/replay.cpp").read_text()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cpp = folder / "replay.cpp"
            cpp.write_text(harness.replace("// RUNTIME_BODIES", bodies))
            binary = folder / "replay"
            built = subprocess.run(  # noqa: S603 - fixed repository sources
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-I",
                    str(ROOT / "src"),
                    str(cpp),
                    str(ROOT / "src/overseer_decisions.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=90,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            replay = subprocess.run(  # noqa: S603 - binary compiled above
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(0, replay.returncode, replay.stdout + replay.stderr)


if __name__ == "__main__":
    unittest.main()
