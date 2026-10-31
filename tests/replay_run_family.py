"""Replay actual run-ledger adapters against SQLite storage, not query mocks."""

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


class RunFamilyReplayTest(unittest.TestCase):
    def test_independent_family_run_lifecycles(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(
            compiler, "C++17 compiler and SQLite development files required"
        )
        source = Path(
            os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")
        ).read_text()
        signatures = [
            "static bool ActiveRunOnMap(",
            "static uint32 ActiveRunIdOnMap(",
            "static uint32 OpenOrTouchRun(",
        ]
        if "static bool RunFamilyColumnsPresent(" in source:
            signatures.insert(0, "static bool RunFamilyColumnsPresent(")
        bodies = "\n\n".join(function(source, signature) for signature in signatures)
        scoped = "std::string const& family" in function(source, signatures[-2])
        harness = (ROOT / "tests/fixtures/run_family/replay.cpp").read_text()
        harness = harness.replace("// RUNTIME_BODIES", bodies)
        harness = harness.replace(
            "FAMILY_LOOKUP",
            "ActiveRunIdOnMap(map, family)" if scoped else "ActiveRunIdOnMap(map)",
        )
        harness = harness.replace(
            "MEMBER_LOOKUP",
            "ActiveRunOnMap(map, member)" if scoped else "ActiveRunOnMap(map)",
        )
        harness = harness.replace("SCOPED_SCHEMA", "true" if scoped else "false")
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cpp, binary = folder / "replay.cpp", folder / "replay"
            cpp.write_text(harness)
            built = subprocess.run(  # noqa: S603 - fixed repository fixture
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    str(cpp),
                    "-lsqlite3",
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            for arguments in ([], ["--legacy"]):
                with self.subTest(arguments=arguments):
                    replay = subprocess.run(  # noqa: S603 - fixture compiled above
                        [str(binary), *arguments],
                        capture_output=True,
                        text=True,
                        timeout=10,
                    )
                    self.assertEqual(
                        0, replay.returncode, replay.stdout + replay.stderr
                    )


if __name__ == "__main__":
    unittest.main()
