"""Replay consecutive sends through the adapter's actual hold lifecycle."""

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


class MailBatchReplayTest(unittest.TestCase):
    def test_one_visit_supports_consecutive_sends(self):
        source = Path(
            os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")
        ).read_text()
        mail = function(source, "static char const* DoMail(")
        start = mail.index('            describe("sent", "",')
        end = mail.index("            // The story of a notable item", start)
        fragments = {
            "RELEASE": function(source, "static void ReleaseHold("),
            "ARRIVAL": function(source, "static void HoldAtTheMailbox("),
            "EXPIRY": function(source, "void ReleaseExpiredHolds()"),
            "SENT": mail[start:end],
        }
        harness = (ROOT / "tests/fixtures/mail_batch/replay.cpp").read_text()
        for name, fragment in fragments.items():
            marker = f'#error "Inject {name} from the production adapter"'
            self.assertEqual(1, harness.count(marker))
            harness = harness.replace(marker, fragment)
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        with tempfile.TemporaryDirectory() as temp:
            cpp, binary = Path(temp) / "replay.cpp", Path(temp) / "replay"
            cpp.write_text(harness)
            built = subprocess.run(  # noqa: S603 - fixed repository fixture
                [compiler, "-std=c++17", "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)],
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
