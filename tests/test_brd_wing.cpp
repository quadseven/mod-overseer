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
                             1000 + BRD_UPPER_CITY_RETRY_SECONDS)) == BrdWingStep::Nothing,
           "past the tries a key carrier's run is NOT finished: the whole map's mask decides "
           "and the watchdog ends a stuck run 'stalled' (mod-overseer#1490)");
    Expect(BRD_UPPER_CITY_TRIES >= 2 && BRD_UPPER_CITY_TRIES <= 5, "a bounded handful of tries");
}

// mod-overseer#1490: a run with 17 of 19 encounters credited was called
// complete after a restart because the chain counter counted every chain ever
// sent, and a keyless finish ignored an Upper City already under way.
void NoFalseComplete()
{
    std::uint32_t const db = BRD_DETENTION_BLOCK_MASK;
    std::uint32_t const seventeen = 0x1FFFFu;  // 9 Detention Block and 8 Upper City bosses
    Expect(BrdBrainOnUpperCity(BRD_UPPER_CITY_MASK) && BrdBrainOnUpperCity(0x7FFFFu),
           "a brain on the Upper City, alone or with the Detention Block, is on it");
    Expect(!BrdBrainOnUpperCity(db) && !BrdBrainOnUpperCity(0),
           "the Detention Block alone, or an unread brain, is not");
    Expect(BrdTriesAfterPoll(BRD_UPPER_CITY_TRIES, BRD_UPPER_CITY_MASK) == 0,
           "a brain seen on the Upper City has taken the chain: the count starts over");
    Expect(BrdTriesAfterPoll(2, db) == 2 && BrdTriesAfterPoll(2, 0) == 2,
           "a brain still on the Detention Block, or unread, keeps the count");

    // The measured run: three accepted chains, then a bare `dc on` put the
    // brain back on brd-db with 17 encounters credited and the doors openable.
    for (unsigned tries = 0; tries <= BRD_UPPER_CITY_TRIES; ++tries)
    {
        BrdWingStep const step =
            BrdWingNext(Facts(db, seventeen, true, tries, 1000, 1000 + BRD_UPPER_CITY_RETRY_SECONDS));
        Expect(step != BrdWingStep::FinishDetentionBlock,
               "17 of 19 credited with the doors openable is never finished as a Detention Block clear");
    }
    Expect(BrdWingNext(Facts(db, seventeen, false)) == BrdWingStep::Nothing,
           "a keyless party past the doors (the key holder fell out of the count) is not finished");
    Expect(BrdWingNext(Facts(db, db, false)) == BrdWingStep::FinishDetentionBlock,
           "a keyless party with nothing past Bael'Gar still finishes there");
    Expect(!BrdUpperCityStarted(db) && BrdUpperCityStarted(db | (1u << 9)),
           "the Upper City has started once bit 9 or above is credited");
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
    Expect(within("coord.brdUpperCityTries = D::BrdTriesAfterPoll(coord.brdUpperCityTries, "
                  "facts.brainMask);") &&
               src.find("coord.brdUpperCityTries = D::BrdTriesAfterPoll(", step) <
                   src.find("D::BrdWingNext(facts)", step),
           "the chain count is reset off the brain's mask before the rules are asked");

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
    NoFalseComplete();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Blackrock Depths wing check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_wing: ok\n");
    return 0;
}
