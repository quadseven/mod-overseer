/*
 * Source contract for the wipe loop's wiring on the family and raid paths
 * (#1201).
 *
 * The decisions are proved in test_dungeon_wipe_loop_family.cpp. What only the
 * adapter can get wrong is whether it ASKS them and acts on the answer:
 *
 *   1. EndRunAndDecide books every 'wipe' on the leader's book and, when the
 *      book says the attempt ends, returns to IDLE before the re-arm into
 *      REPAIRING (which is the walk back in).
 *   2. IDLE asks DungeonWipeHoldsRunStart before RESETTING is set, so a held
 *      attempt is never reset or walked to the door.
 *   3. The raid release carries the wipe book into the fresh state, and a
 *      release that did not finish marks the attempt as ending, which the next
 *      poll's facts carry.
 *   4. (#1223) IDLE's adoption of a run a member is already inside asks the
 *      same book before the coordinator state is replaced, so a ghost revived
 *      inside during the hold does not resume the run at the same pull, and
 *      the refusal still says the hold line.
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

int CheckTheFamilyEnding(std::string const& text)
{
    std::string const end = Body(text, "void EndRunAndDecide(");
    if (end.empty())
        return Fail("cannot find EndRunAndDecide");
    std::size_t const booked = end.find("OverseerDecisions::DungeonWipeBookEndsAttempt(");
    if (booked == std::string::npos)
        return Fail("EndRunAndDecide must book the wipe on the leader's book");
    if (end.find("std::string(outcome) == \"wipe\"") == std::string::npos)
        return Fail("only a run that ended 'wipe' goes on the book");
    std::size_t const ends = end.find("if (wipeEndsAttempt)");
    std::size_t const rearm = end.find("coord.phase = DungeonRunPhase::Repairing;");
    if (ends == std::string::npos || rearm == std::string::npos)
        return Fail("EndRunAndDecide must ask the book's answer and still re-arm otherwise");
    if (!(booked < ends && ends < rearm))
        return Fail("the attempt must be ended before the re-arm into REPAIRING");
    std::size_t const back = end.find("return;", ends);
    if (back == std::string::npos || back > rearm)
        return Fail("an ended attempt must return before the re-arm");
    return EXIT_SUCCESS;
}

int CheckTheIdleHold(std::string const& text)
{
    std::size_t const hold = text.find("OverseerDecisions::DungeonWipeHoldsRunStart(");
    if (hold == std::string::npos)
        return Fail("IDLE must ask DungeonWipeHoldsRunStart");
    std::size_t const reset = text.find("coord.phase = DungeonRunPhase::Resetting;", hold);
    if (reset == std::string::npos)
        return Fail("the hold must stand before IDLE sets RESETTING");
    std::size_t const back = text.find("return;", hold);
    if (back == std::string::npos || back > reset)
        return Fail("a held attempt must return before RESETTING");
    if (text.find("RESURRECTION_SICKNESS_AURA", hold - 2000) > hold)
        return Fail("the hold must read the members' Resurrection Sickness");
    return EXIT_SUCCESS;
}

int CheckTheAdoptionHold(std::string const& text)
{
    std::size_t const adoptAt = text.find("OverseerDecisions::DungeonRunAdoptAt(");
    if (adoptAt == std::string::npos)
        return Fail("cannot find IDLE's adoption");
    std::size_t const adopted = text.find("coord = DungeonRunCoordinatorState();", adoptAt);
    if (adopted == std::string::npos)
        return Fail("cannot find where adoption replaces the coordinator state");
    std::size_t const hold = text.find("OverseerDecisions::DungeonWipeHoldsAdoption(", adoptAt);
    if (hold == std::string::npos || hold > adopted)
        return Fail("adoption must ask the wipe book before it adopts the run");
    if (text.find("_dungeonWipeBooks.find(leaderName)", adoptAt) > hold)
        return Fail("adoption must read the leader's wipe book");
    if (text.find("RESURRECTION_SICKNESS_AURA", adoptAt) > hold)
        return Fail("adoption must read the members' Resurrection Sickness");
    // Its own once-per-ending flag, not the run-start line's: that one may
    // already be set for this ending and would silence the refusal.
    std::size_t const said = text.find("book->second.adoptionHoldSaid = true;", hold);
    if (said == std::string::npos || said > adopted ||
        text.find("if (!book->second.adoptionHoldSaid)", hold) > said)
        return Fail("a refused adoption must say its line once per ending, on its own flag");
    std::size_t const line = text.find("waits - the last attempt there ended on a second", hold);
    if (said == std::string::npos || line == std::string::npos || line > adopted)
        return Fail("a refused adoption must still say the hold line");
    std::size_t const back = text.find("return;", hold);
    if (back == std::string::npos || back > adopted)
        return Fail("a held adoption must return before the run is adopted");
    return EXIT_SUCCESS;
}

int CheckTheRaidRelease(std::string const& text)
{
    std::string const drive = Body(text, "void DriveRaidRun()");
    if (drive.empty())
        return Fail("cannot find DriveRaidRun");
    if (drive.find("facts.attemptEnding = run.attemptEnding;") == std::string::npos)
        return Fail("the raid facts must carry an attempt that is still ending");
    std::size_t const release = drive.find("if (ReleaseRaidRun(roster.family, run, door))");
    if (release == std::string::npos)
        return Fail("cannot find the raid release");
    std::size_t const fresh = drive.find("run = RaidRunState();", release);
    std::size_t const carried = drive.find("run.wipes = wipes;", release);
    if (fresh == std::string::npos || carried == std::string::npos || carried < fresh)
        return Fail("the wipe book must be carried into the fresh state after the release");
    if (drive.find("run.lastWipeAt = lastWipeAt;", release) == std::string::npos ||
        drive.find("run.lastWipeMap = lastWipeMap;", release) == std::string::npos)
        return Fail("the last wipe's time and map must be carried too");
    std::size_t const failed = drive.find("else if (!run.attemptEnding)", release);
    std::size_t const marked = drive.find("run.attemptEnding = true;", release);
    if (failed == std::string::npos || marked == std::string::npos || marked < failed)
        return Fail("a release that did not finish must mark the attempt as ending");
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

    if (CheckTheFamilyEnding(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheIdleHold(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheAdoptionHold(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (CheckTheRaidRelease(text) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "a repeat family wipe ends the attempt and IDLE holds the next one; the raid "
                 "keeps its wipe book across a release and an unfinished release stays ended\n";
    return EXIT_SUCCESS;
}
