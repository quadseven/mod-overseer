/*
 * A Blackrock Depths run that carries the Shadowforge Key goes on into the
 * Upper City (mod-overseer#1410).
 *
 * The dungeon brain splits map 230 into the Detention Block (brd-db, bits 0-8)
 * and the Upper City (brd-uc, bits 9-18), and a bare `dc on` latches brd-db.
 * Nothing named brd-uc, so every run stopped at Bael'Gar and stood until the
 * clearing watchdog ended it. The rules in src/overseer_brd_wing.h chain a key
 * carrier's (or a lockpicker's) run to the Upper City and finish a keyless one at once; the
 * adapter's wiring is pinned in the source because it needs a worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_wing.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
}

BrdWingFacts Facts(std::uint32_t brain, std::uint32_t credited, bool key, unsigned tries = 0,
                   std::time_t lastTryAt = 0, std::time_t now = 1000)
{
    BrdWingFacts f;
    f.mapId = BRD_WING_MAP_ID;
    f.brainMask = brain;
    f.credited = credited;
    f.doorsOpen = key;
    f.tries = tries;
    f.lastTryAt = lastTryAt;
    f.now = now;
    return f;
}

void TheMasks()
{
    Expect(BRD_DETENTION_BLOCK_MASK == 0x1FFu, "the Detention Block is bits 0 to 8");
    Expect(BRD_UPPER_CITY_MASK == 0x7FE00u, "the Upper City is bits 9 to 18");
    Expect((BRD_DETENTION_BLOCK_MASK | BRD_UPPER_CITY_MASK) == 0x7FFFFu &&
               (BRD_DETENTION_BLOCK_MASK & BRD_UPPER_CITY_MASK) == 0,
           "the two wings are the whole map's 19 bits, and share none");
    Expect(std::string(BRD_UPPER_CITY_WING) == "brd-uc", "the dungeon brain's Upper City token");
    Expect(BRD_SHADOWFORGE_KEY == 11000, "the Shadowforge Key");
    Expect(BrdOpensUpperCity(1, 0), "the key opens the Upper City");
    Expect(BrdOpensUpperCity(0, 250) && BrdOpensUpperCity(0, 300),
           "so does lockpicking at lock 680's 250, as a rogue picks it");
    Expect(!BrdOpensUpperCity(0, 249) && !BrdOpensUpperCity(0, 0),
           "lower lockpicking and no key do not");
}

void TheChain()
{
    std::uint32_t const db = BRD_DETENTION_BLOCK_MASK;
    Expect(BrdWingNext(Facts(db, db, true)) == BrdWingStep::ChainUpperCity,
           "the Detention Block credited with a key carrier chains to the Upper City");
    Expect(BrdWingNext(Facts(db, db, false)) == BrdWingStep::FinishDetentionBlock,
           "with no key the run is finished at Bael'Gar, not after the stall");
    Expect(BrdWingNext(Facts(db, db & ~(1u << 8), true)) == BrdWingStep::Nothing,
           "Bael'Gar still up: nothing yet");
    Expect(BrdWingNext(Facts(db, db & ~(1u << 3), false)) == BrdWingStep::Nothing,
           "the Ring of Law not credited: nothing yet");

    Expect(BrdWingNext(Facts(BRD_UPPER_CITY_MASK, db, true)) == BrdWingStep::Nothing,
           "the brain already on the Upper City: the whole map's mask decides");
    Expect(BrdWingNext(Facts(BRD_UPPER_CITY_MASK, db | (1u << 9), false)) == BrdWingStep::Nothing,
           "a key carrier who died out of the party mid-Upper City does not end the run");
    Expect(BrdWingNext(Facts(0, db, true)) == BrdWingStep::Nothing,
           "an unread brain is left to the ordinary completion check");
    Expect(BrdWingNext(Facts(0x7FFFFu, db, true)) == BrdWingStep::Nothing,
           "a brain armed for the whole map needs no chain");

    BrdWingFacts other = Facts(db, db, true);
    other.mapId = 229;
    Expect(BrdWingNext(other) == BrdWingStep::Nothing, "no other map");
}

void TheRetries()
{
    std::uint32_t const db = BRD_DETENTION_BLOCK_MASK;
    Expect(BrdWingNext(Facts(db, db, true, 1, 1000, 1000 + BRD_UPPER_CITY_RETRY_SECONDS - 1)) ==
               BrdWingStep::WaitChain,
           "a chain just sent is given time to take");
    Expect(BrdWingNext(Facts(db, db, true, 1, 1000, 1000 + BRD_UPPER_CITY_RETRY_SECONDS)) ==
               BrdWingStep::ChainUpperCity,
           "a chain that did not take is sent again after the retry");
    Expect(BrdWingNext(Facts(db, db, true, BRD_UPPER_CITY_TRIES, 1000,
                             1000 + BRD_UPPER_CITY_RETRY_SECONDS)) ==
               BrdWingStep::FinishDetentionBlock,
           "past the tries the run ends as a Detention Block clear");
    Expect(BRD_UPPER_CITY_TRIES >= 2 && BRD_UPPER_CITY_TRIES <= 5, "a bounded handful of tries");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(src.find("#include \"overseer_brd_wing.h\"") != std::string::npos,
           "the adapter includes the rules");
    std::size_t const step = src.find("bool StepBrdUpperCity(DungeonRunCoordinatorState& coord,");
    Expect(step != std::string::npos, "the Upper City step exists");
    std::size_t const end = src.find("    void DriveDungeonRunFor(", step);
    auto within = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, step);
        return at != std::string::npos && at < end;
    };
    Expect(within("D::BrdWingNext(facts)"), "it asks the rules");
    Expect(within("DungeonBrainExpectedEncounterMask(leader)") &&
               within("CompletedEncounters(leader)"),
           "it reads the brain's wing and the instance's credit");
    Expect(within("p->GetItemCount(D::BRD_SHADOWFORGE_KEY)") &&
               within("p->GetSkillValue(SKILL_LOCKPICKING)") && within("D::BrdOpensUpperCity("),
           "it reads the Shadowforge Key and lockpicking off each member in the instance");
    Expect(within("DoSpecificAction(\"dc on\", Event(\"dc\", D::BRD_UPPER_CITY_WING, issuer)"),
           "the chain is `dc on brd-uc` with an authorized issuer");
    Expect(within("coord.provedComplete = true;") && within("DungeonRunPhase::Exiting"),
           "a keyless Detention Block clear ends the run as complete");

    std::size_t const shrine = src.find("StepBrdShrine(coord, members, leader, clearingNow)");
    std::size_t const hook =
        src.find("StepBrdUpperCity(coord, members, leader, *portal, clearingNow)");
    std::size_t const done =
        src.find("OverseerDecisions::DungeonRunCompletion(expected, credited)", hook);
    Expect(shrine != std::string::npos && hook != std::string::npos && done != std::string::npos &&
               shrine < hook && hook < done,
           "CLEARING asks the Upper City after the Shrine and before the completion check");
}

}  // namespace

int main()
{
    TheMasks();
    TheChain();
    TheRetries();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Blackrock Depths wing check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_wing: ok\n");
    return 0;
}
