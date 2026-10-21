/*
 * Source contract for the dungeon rejoin's wiring (#384).
 *
 * The decision is proved in test_dungeon_rejoin.cpp: which members can be
 * walked back to the door, which cannot be walked at all, and how long a walk
 * may last. What only the adapter can get wrong is whether it ASKS, and WHEN.
 * A walk nobody performs is a rule with no effect, and a run that leaves
 * STAGED_INSIDE while the party is still short is a run that carries on
 * without the member it was supposed to bring home.
 *
 * WHY THIS IS PINNED AS TEXT RATHER THAN ONLY READ OFF THE REALM. The live
 * proof of a rejoin is one log line - "stranded outside map <map> - <n> went
 * back in this poll" - and the dev realm's worldserver log is rotated on a
 * schedule that keeps a short window of it, so the line is only seen in a
 * window where a member happened to die inside a run while somebody was
 * looking. A check that says the line exists, is printed only for a member
 * that actually changed map, and is printed by a walk the staged hold
 * performs on every poll is worth more than a log window that may not
 * contain the event at all.
 *
 * WHAT IS PINNED, IN THE ORDER THE CODE ASKS IT:
 *
 *   1. The census is asked first (DungeonRunAllThrough), so the run knows the
 *      party is short before it acts on it.
 *   2. The walk happens next, inside the branch that knows the party is
 *      short, so it happens on every poll of the hold rather than once.
 *   3. The ceiling is read after the walk, so a rejoin that is working
 *      restarts the clock instead of being failed out while it is working.
 *   4. The poll returns before the run can move on, so CLEARING is reached
 *      only once the census says every member is through.
 *   5. The line itself is printed only when a member actually changed map,
 *      which is the difference between a measurement and a postmark.
 *
 * IT BITES. Run against src/mod_overseer.cpp as it was before the rejoin walk
 * was merged - commit 8d6d8a1, which is the STAGED_INSIDE ceiling and nothing
 * else - every one of these checks fails, which is what a contract is for.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

bool Has(std::string const& haystack, char const* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// The line an operator greps for, as the source spells it: the map and the
// count are format placeholders here and numbers in the log.
char const* const PROOF_LINE = "stranded outside map {} - {} went back in this poll";

// THE REJOIN ITSELF: the line it prints, who it aims, and that it never
// teleports anybody.
int CheckTheRejoin(std::string const& text)
{
    std::string const rejoin = Body(text, "uint32 WalkStrandedBackIn(");
    if (rejoin.empty())
        return Fail("cannot find WalkStrandedBackIn");

    if (!Has(rejoin, PROOF_LINE))
        return Fail("the rejoin must print the line a live check greps for");
    if (!Has(rejoin, "uint32 const crossed = DungeonRunKnock(walking, leaderName, triggerId);") ||
        !Has(rejoin, "if (crossed)"))
        return Fail("the rejoin line must be printed only for members that changed map");

    if (!Has(rejoin, "OverseerDecisions::DungeonRunWrongSide(states)"))
        return Fail("the rejoin must ask which members are on the wrong side of the door");
    if (!Has(rejoin, "for (std::string const& name : stranded.walk)") ||
        !Has(rejoin, "RejoinToward(name, doorAim, arriveWithin, portal.insideMapId);"))
        return Fail("the rejoin must aim the living members the decision named");
    if (Has(rejoin, "TeleportTo("))
        return Fail("nothing on a stranded member's way back in may teleport it");

    return EXIT_SUCCESS;
}

// THE STAGED HOLD: the census, the walk, the ceiling and the return, in the
// order the phase asks them.
int CheckTheStagedHold(std::string const& text)
{
    std::size_t const staged = text.find("if (coord.phase == DungeonRunPhase::StagedInside)");
    if (staged == std::string::npos)
        return Fail("cannot find the STAGED_INSIDE branch");
    std::size_t const clearing =
        text.find("coord.phase = DungeonRunPhase::Clearing;", staged);
    if (clearing == std::string::npos)
        return Fail("cannot find the CLEARING transition");
    std::string const hold = text.substr(staged, clearing - staged);

    std::size_t const census = hold.find("if (!OverseerDecisions::DungeonRunAllThrough(states))");
    std::size_t const walk = hold.find("WalkStrandedBackIn(");
    std::size_t const ceiling = hold.find("FailSplitInside(");
    if (census == std::string::npos || walk == std::string::npos || ceiling == std::string::npos)
        return Fail("the staged hold must ask the census, walk the stranded member and keep "
                    "its ceiling");
    if (!(census < walk && walk < ceiling))
        return Fail("the walk must sit between the census and the ceiling: the census says "
                    "the party is short, the walk acts on it, the ceiling judges whether it "
                    "is getting anywhere");

    // A POLL THAT FINDS THE PARTY SHORT MUST RETURN, so the run cannot leave
    // STAGED_INSIDE for CLEARING on a poll where the census was not whole.
    // The return is read after the ceiling and before anything else the phase
    // asks, which is where it stands: the ceiling judges the walk that has
    // just been asked for, and the phase's own gate is asked only by a poll
    // that found the party whole.
    std::size_t const shortPoll = hold.find("return;", ceiling);
    if (shortPoll == std::string::npos)
        return Fail("a poll that finds the party short must return before the run can move on");
    std::size_t const gate = hold.find("LeaderClientGateHolds(", ceiling);
    if (gate != std::string::npos && shortPoll > gate)
        return Fail("the short-census poll must return before the phase asks anything else");

    // AND THE RUN SAYS SO WHEN THE CENSUS IS WHOLE, after the transition.
    std::size_t const satisfied = text.find("dungeon run STAGED_INSIDE satisfied");
    if (satisfied == std::string::npos || satisfied < clearing)
        return Fail("the run must say when the census is whole, and say it after it has "
                    "moved on to CLEARING");

    return EXIT_SUCCESS;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    if (CheckTheRejoin(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheStagedHold(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "a stranded member is walked back in, the census is asked before the walk, "
                 "and the run reaches CLEARING only when every member is through\n";
    return EXIT_SUCCESS;
}
