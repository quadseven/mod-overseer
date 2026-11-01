"""Replay the death-breaker adapter with real claim, refusal and release methods."""

import os
from pathlib import Path
import subprocess
import tempfile

from replay_run_family import function

ROOT = Path(__file__).resolve().parents[1]
source = Path(os.environ.get("OVERSEER_REPLAY_SOURCE", ROOT / "src/mod_overseer.cpp")).read_text()
start = source.index("                OverseerDecisions::ErrandDeathToll toll;")
end = source.index("                // THE SECOND REASON", start)
methods = [
    "    bool RunOwns(std::string const& name,",
    "    bool ClaimedBy(std::string const& name,",
    "    void Refuse(std::string const& name,",
    "    int64_t SecondsSinceRefused(std::string const& name,",
    "    void ReleaseImpl(std::string const& name,",
    "    void Release(std::string const& name,",
]
types = ["    struct Refusal\n", "    struct ErrandEnd\n", "    struct Landing\n"]
harness = (ROOT / "tests/fixtures/errand_death_owner/replay.cpp").read_text()
harness = harness.replace("// BOOK_TYPES", "\n".join(function(source, t) + ";" for t in types))
harness = harness.replace("// BOOK_METHODS", "\n".join(function(source, m) for m in methods))
harness = harness.replace("// TOLL_ASSEMBLY", source[start:end])
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "replay.cpp"
    binary = Path(directory) / "replay"
    cpp.write_text(harness)
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-I" + str(ROOT / "src"),
         str(ROOT / "src/overseer_decisions.cpp"), str(cpp), "-o", str(binary)],
        check=True, timeout=60,
    )
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS actual death-breaker ownership, release and refused reissue adapter")
