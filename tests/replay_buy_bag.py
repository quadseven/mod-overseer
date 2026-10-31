"""Execute the actual buy preflight, packet dispatch and inventory readback."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuyBagReplayTest(unittest.TestCase):
    def test_empty_bag_position_is_a_purchase_destination(self):
        source = Path(
            os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")
        ).read_text()
        start = source.index("static char const* DoBuy(Player* buyer,")
        brace = source.index("{", start)
        depth, end = 1, brace + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        start = source.index("        if (request.capped && price", brace)
        body = source[start : end - 1]
        harness = (ROOT / "tests/fixtures/buy_bag/replay.cpp").read_text()
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cpp, binary = folder / "replay.cpp", folder / "replay"
            cpp.write_text(harness.replace("// PURCHASE_BODY", body))
            built = subprocess.run(  # noqa: S603 - fixed repository fixture
                [compiler, "-std=c++17", str(cpp), "-o", str(binary)],
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            replay = subprocess.run(  # noqa: S603 - compiled fixture
                [str(binary)], capture_output=True, text=True, timeout=10
            )
            self.assertEqual(0, replay.returncode, replay.stdout + replay.stderr)


if __name__ == "__main__":
    unittest.main()
