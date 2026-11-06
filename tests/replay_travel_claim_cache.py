"""Replay Claim and FenceRefuses caches against changed travel columns.

The cache test itself now lives in the movement ownership module (#1312): a
cached drive is trusted only while the column still holds its aim. What the
adapter decides is what counts as a cached drive. This extracts that real
line from TravelAimBook::Claim and TravelAimBook::FenceRefuses and runs it
through the real module against the in-memory column adapter, so a stale
cache that bypasses the ownership policy fails here.
"""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/mod_overseer.cpp").read_text()
CACHED = re.compile(r"\n( {8}auto const it = _state\.find\(name\);\n {8}bool const cachedDrive = [^;]+;)")
try:
    claim = source.index("    bool Claim(std::string const& name,")
    claim_end = source.index("\n    }\n", claim)
    fence = source.index("    bool FenceRefuses(std::string const& name,")
    fence_end = source.index("\n    }\n", fence)
    claim_cached = CACHED.search(source, claim, claim_end).group(1)
    fence_cached = CACHED.search(source, fence, fence_end).group(1)
except (ValueError, AttributeError) as exc:
    raise SystemExit("Travel cache branch not found; update the replay anchors") from exc
if "_wheel.Claim(name, target, owner, cachedDrive," not in source[claim:claim_end]:
    raise SystemExit("Claim must hand its cached drive to the movement ownership module")
if "_wheel.FenceRefuses(name, target, owner, cachedDrive)" not in source[fence:fence_end]:
    raise SystemExit("FenceRefuses must hand its cached drive to the movement ownership module")

harness = r'''
#include "overseer_movement_ownership.h"
#include <iostream>
#include <map>
#include <string>
using OverseerDecisions::TravelOwner;
struct Book
{
    struct State { std::string target; };
    std::map<std::string, State> _state;
    OverseerMovement::InMemoryAimColumn column;
    OverseerMovement::MovementOwnership _wheel{column, 45};
    bool CachedClaim(std::string const& name, std::string const& target)
    {
        TravelOwner const owner = TravelOwner::TownTrip;
CLAIM_CACHED
        return _wheel.Claim(name, target, owner, cachedDrive, {}).answer ==
               OverseerMovement::ClaimAnswer::AlreadyDriving;
    }
    bool CachedFence(std::string const& name, std::string const& target)
    {
        TravelOwner const owner = TravelOwner::TownTrip;
FENCE_CACHED
        return _wheel.FenceRefuses(name, target, owner, cachedDrive);
    }
};
int main()
{
    unsigned failures = 0;
    for (std::string const target : {"vendor", "banker", "repair", "auctioneer", "at:0:4,5,6"})
    {
        Book book;
        book._state["Leader"] = {target};
        book.column.columns["Leader"] = target;
        book.column.columns["Absent"] = target;
        if (!book.CachedClaim("Leader", target) || book.CachedFence("Leader", target))
        {
            std::cerr << "unchanged route was not reusable: " << target << '\n';
            ++failures;
        }
        for (std::string const column : {"at:0:1,2,3", "banker", "guard:Elwynn Forest"})
        {
            if (column == target)
                continue;
            book.column.columns["Leader"] = column;
            if (book.CachedClaim("Leader", target) || !book.CachedFence("Leader", target))
            {
                std::cerr << "stale cache bypassed policy: " << target
                          << " over column '" << column << "'\n";
                ++failures;
            }
        }
        // A foreign errand stands for a character with no drive in flight,
        // and for a different target: neither is a cached drive.
        book.column.columns["Leader"] = "guard:Elwynn Forest";
        book.column.columns["Absent"] = "guard:Elwynn Forest";
        if (book.CachedClaim("Absent", target) || book.CachedClaim("Leader", "trainer") ||
            !book.CachedFence("Absent", target) || !book.CachedFence("Leader", "trainer"))
        {
            std::cerr << "a missing or different drive was treated as cached\n";
            ++failures;
        }
        // The column already holds the asked aim, but the drive in flight is
        // another errand: that is a new errand, not the cached one.
        book.column.columns["Leader"] = "trainer";
        if (target != "trainer" && book.CachedClaim("Leader", "trainer"))
        {
            std::cerr << "a drive for '" << target << "' was reused for 'trainer'\n";
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "claim_cache.cpp"
    binary = Path(directory) / "claim_cache"
    cpp.write_text(harness.replace("CLAIM_CACHED", claim_cached).replace("FENCE_CACHED", fence_cached))
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    subprocess.run(
        [compiler, "-std=c++17", "-Wall", "-Wextra", "-I" + str(root / "src"),
         str(root / "src/overseer_decisions.cpp"), str(cpp), "-o", str(binary)],
        check=True,
        timeout=60,
    )
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS: cached routes respect current, changed, guarded and cleared travel columns")
