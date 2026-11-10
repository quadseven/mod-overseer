/*
 * A member the finder's family lost outside comes back by the finder's own
 * teleport in, and the dungeon brain waits for the key holder (#1488).
 *
 * The rules are decisions, proved here. What only the adapter can get wrong is
 * whether it asks them, so the wiring is read from src/mod_overseer.cpp as text.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "overseer_decisions.h"

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

OverseerDecisions::FinderReturnFacts Stranded()
{
    OverseerDecisions::FinderReturnFacts facts;
    facts.groupInDungeon = true;
    facts.inGroup = true;
    facts.inWorld = true;
    facts.alive = true;
    return facts;
}
}  // namespace

int main()
{
    using namespace OverseerDecisions;

    // The finder's teleport in.
    Check("a stranded living member out of combat is due", FinderReturnDue(Stranded()));
    {
        FinderReturnFacts f = Stranded();
        f.groupInDungeon = false;
        Check("not due when the group is no finder group in its dungeon", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.inGroup = false;
        Check("not due for a member outside the group", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.inWorld = false;
        Check("not due for a member not in the world", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.alive = false;
        Check("not due for a ghost", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.inCombat = true;
        Check("not due in combat (the core refuses it)", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.beingTeleported = true;
        Check("not due mid-teleport", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.inside = true;
        Check("not due for a member already inside", !FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.sinceLastAttempt = FINDER_RETURN_RETRY_SECONDS - 1;
        Check("not due inside the retry interval", !FinderReturnDue(f));
        f.sinceLastAttempt = FINDER_RETURN_RETRY_SECONDS;
        Check("due at the retry interval", FinderReturnDue(f));
    }
    {
        FinderReturnFacts f = Stranded();
        f.attempts = FINDER_RETURN_MAX_ATTEMPTS - 1;
        Check("due on the last allowed try", FinderReturnDue(f));
        f.attempts = FINDER_RETURN_MAX_ATTEMPTS;
        Check("not due after the tries are spent", !FinderReturnDue(f));
    }

    // The key holder before the brain is on.
    Check("Blackrock Depths wants the Shadowforge Key", DungeonDoorKeyItem(230) == 11000);
    Check("a dungeon with no inner key wants none", DungeonDoorKeyItem(43) == 0);
    Check("the outdoor map wants none", DungeonDoorKeyItem(0) == 0);
    Check("held while the key holder is outside, at once", ArmingHeldForKeyHolder(true, 0));
    Check("held just under the ceiling",
          ArmingHeldForKeyHolder(true, KEY_HOLDER_ARMING_HOLD_SECONDS - 1));
    Check("not held at the ceiling", !ArmingHeldForKeyHolder(true, KEY_HOLDER_ARMING_HOLD_SECONDS));
    Check("not held with the key holder inside", !ArmingHeldForKeyHolder(false, 0));

    // The adapter asks both.
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return EXIT_FAILURE;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    std::size_t const returnAt = src.find("ReturnStrandedByFinder(leaderName, *portal, states, coord)");
    std::size_t const walkAt = src.find("WalkStrandedBackIn(leaderName, *portal, door, triggerId, states,");
    Check("the stranded branch asks the finder return", returnAt != std::string::npos);
    Check("the finder return comes before the walk back on foot",
          returnAt != std::string::npos && walkAt != std::string::npos && returnAt < walkAt);

    std::size_t const fn = src.find("bool ReturnStrandedByFinder(");
    std::size_t const fnEnd = fn == std::string::npos ? fn : src.find("\n    }\n", fn);
    std::string const body =
        fn == std::string::npos ? std::string() : src.substr(fn, fnEnd - fn);
    Check("the finder return is defined", !body.empty());
    Check("it asks the decision", Has(body, "OverseerDecisions::FinderReturnDue(facts)"));
    Check("it reads the finder group's state", Has(body, "FinderStateOf(sLFGMgr->GetState("));
    Check("it presses the finder's own teleport in", Has(body, "CMSG_LFG_TELEPORT") &&
                                                       Has(body, "HandleLfgTeleportOpcode"));
    Check("it presses teleport IN, not out", Has(body, "in << uint8(0);"));

    std::size_t const holdAt = src.find("OverseerDecisions::ArmingHeldForKeyHolder(");
    std::size_t const armAt = src.find("botAI->DoSpecificAction(\"dc on\", Event(\"dc\", raidWing, issuer), true);");
    Check("the arming drive asks the key holder hold", holdAt != std::string::npos);
    Check("the hold comes before dc on is issued",
          holdAt != std::string::npos && armAt != std::string::npos && holdAt < armAt);
    Check("the hold reads the dungeon's key item",
          Has(src, "OverseerDecisions::DungeonDoorKeyItem(bot->GetMapId())"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
