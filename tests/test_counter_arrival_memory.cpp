/*
 * A leader that has arrived at a counter and taken the counter hold is not
 * re-claimed for the same counter while the hold stands (#1708).
 *
 * The shape this pins: the travel drive reports "reached ... errand done,
 * releasing", takes the counter hold, and the release CLEARS the column (the
 * book claimed the aim), so no landing is remembered. The writer then puts the
 * same counter up again; the drive reads a new errand, asks for the leader
 * ("errand by travel column (he was free)") and takes the hold down ("sent
 * somewhere else"). About five seconds per cycle, with no quiet hold in which a
 * sale can happen.
 *
 * The raid staging walk of #1703 is a different caller and is pinned in
 * test_raid_run.cpp; there is no shared seam, because that one is decided by a
 * fact (the head is inside the assemble radius) and this one by a memory.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::CounterArrivalMemory;
using OverseerDecisions::CounterArrivalMemoryStep;
using OverseerDecisions::LandedErrand;
using OverseerDecisions::NoteCounterArrival;

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

LandedErrand Poll(CounterArrivalMemory& memory, char const* aim, bool hold, bool& said)
{
    return CounterArrivalMemoryStep(memory, aim, hold, said);
}

void TheSameCounterIsNotClaimedAgainWhileTheHoldStands()
{
    CounterArrivalMemory memory;
    NoteCounterArrival(memory, "vendor");
    bool said = false;
    int stoodDown = 0;
    int saidCount = 0;
    for (int poll = 0; poll < 100; ++poll)
    {
        if (Poll(memory, "vendor", true, said) == LandedErrand::StandDown)
            ++stoodDown;
        if (said)
            ++saidCount;
    }
    Check("a hundred polls of the same aim under the hold all stand down", stoodDown == 100);
    Check("and the stand-down is said once per arrival", saidCount == 1);
}

void AnotherAimIsClaimedAsBefore()
{
    char const* const others[] = {"banker", "repair", "at:0:1.5,2.5,3.5", "2084"};
    for (char const* other : others)
    {
        CounterArrivalMemory memory;
        NoteCounterArrival(memory, "vendor");
        bool said = false;
        Check(other, Poll(memory, other, true, said) == LandedErrand::Resume);
        Check("and the arrival is forgotten", memory.aim.empty());
    }
}

void TheHoldEndingClaimsTheSameAimAgain()
{
    CounterArrivalMemory memory;
    NoteCounterArrival(memory, "vendor");
    bool said = false;
    Check("held: stands down", Poll(memory, "vendor", true, said) == LandedErrand::StandDown);
    Check("hold gone (ceiling or left): resumes",
          Poll(memory, "vendor", false, said) == LandedErrand::Resume);
    Check("and nothing is remembered afterwards",
          Poll(memory, "vendor", true, said) == LandedErrand::NotLanded);
}

void ANewArrivalIsSaidAgain()
{
    CounterArrivalMemory memory;
    bool said = false;
    NoteCounterArrival(memory, "vendor");
    Poll(memory, "vendor", true, said);
    Check("first arrival said", said);
    Poll(memory, "vendor", true, said);
    Check("not said twice", !said);
    NoteCounterArrival(memory, "vendor");
    Poll(memory, "vendor", true, said);
    Check("a later arrival at the same counter is said again", said);
}

void NothingRememberedIsAnOrdinaryErrand()
{
    CounterArrivalMemory memory;
    bool said = true;
    Check("no arrival", Poll(memory, "vendor", true, said) == LandedErrand::NotLanded);
    Check("and nothing is said", !said);
}

// The wiring lives in the adapter, which this binary cannot compile: read it.
void TheAdapterRemembersTheArrivalEvenWhenTheColumnIsCleared()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    auto const mark = text.find("void MarkCounterLanding(");
    Check("MarkCounterLanding exists", mark != std::string::npos);
    auto const note = text.find("NoteCounterArrival(_counterArrivals[name], target)", mark);
    auto const landed = text.find("_landed.find(name)", mark);
    Check("the arrival is noted before the landing is looked for", note != std::string::npos &&
                                                                    note < landed);
    Check("the drive asks the arrival memory",
          text.find("CounterArrivalMemoryStep(arrival->second") != std::string::npos);
}

}  // namespace

int main()
{
    TheSameCounterIsNotClaimedAgainWhileTheHoldStands();
    AnotherAimIsClaimedAsBefore();
    TheHoldEndingClaimsTheSameAimAgain();
    ANewArrivalIsSaidAgain();
    NothingRememberedIsAnOrdinaryErrand();
    TheAdapterRemembersTheArrivalEvenWhenTheColumnIsCleared();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("counter arrival memory: all passed\n");
    return EXIT_SUCCESS;
}
