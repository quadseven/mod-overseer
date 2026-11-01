"""Replay Claim and FenceRefuses caches against changed travel columns."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/mod_overseer.cpp").read_text()
try:
    claim = source.index("    bool Claim(std::string const& name,")
    start = source.index("        auto const it = _state.find(name);", claim)
    end = source.index("        // TWO FENCES", start)
    fence = source.index("    bool FenceRefuses(std::string const& name,")
    fence_start = source.index("        auto const it = _state.find(name);", fence)
    fence_end = source.index("        OverseerDecisions::TravelClaimFacts facts(owner);", fence_start)
except ValueError as exc:
    raise SystemExit("Travel cache branch not found; update the replay anchors") from exc

# Fallthrough means Claim must consult its normal ownership policy. Extracting
# the real branch matters: that policy already refused a foreign mailbox aim,
# but the adapter returned true before it ever asked the policy.
prefix = r'''
#include <iostream>
#include <map>
#include <string>
#include <vector>
struct Book
{
    struct State { std::string target; };
    std::map<std::string, State> _state;
    std::string column;
    std::string CurrentTravelNpc(std::string const&) const { return column; }
    bool CachedClaim(std::string const& name, std::string const& target)
    {
'''
between = r'''
        return false;
    }
    bool CachedFence(std::string const& name, std::string const& target)
    {
'''
suffix = r'''
        return true;
    }
};
int main()
{
    unsigned failures = 0;
    for (std::string const target : {"vendor", "banker", "repair", "auctioneer", "at:0:4,5,6"})
    {
        Book book;
        book._state["Leader"] = {target};
        book.column = target;
        if (!book.CachedClaim("Leader", target) || book.CachedFence("Leader", target))
        {
            std::cerr << "unchanged route was not reusable: " << target << '\n';
            ++failures;
        }
        for (std::string const column : {"at:0:1,2,3", "banker", "guard:Elwynn Forest", ""})
        {
            if (column == target)
                continue;
            book.column = column;
            if (book.CachedClaim("Leader", target) || !book.CachedFence("Leader", target))
            {
                std::cerr << "stale cache bypassed policy: " << target
                          << " over column '" << column << "'\n";
                ++failures;
            }
        }
        book.column = target;
        if (book.CachedClaim("Absent", target) || book.CachedClaim("Leader", "trainer") ||
            !book.CachedFence("Absent", target) || !book.CachedFence("Leader", "trainer"))
        {
            std::cerr << "a missing or different drive was treated as cached\n";
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "claim_cache.cpp"
    binary = Path(directory) / "claim_cache"
    cpp.write_text(
        prefix + source[start:end] + between + source[fence_start:fence_end] + suffix
    )
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(binary)],
        check=True,
        timeout=30,
    )
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS: cached routes respect current, changed, guarded and cleared travel columns")
