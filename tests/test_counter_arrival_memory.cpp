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

LandedErrand Poll(CounterArrivalMemory& memory, char const* aim, bool hold, bool& said,
                  bool inReach = true)
{
    return CounterArrivalMemoryStep(memory, aim, hold, inReach, said);
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
    Check("hold gone at its ceiling: resumes",
          Poll(memory, "vendor", false, said) == LandedErrand::Resume);
    Check("and nothing is remembered afterwards",
          Poll(memory, "vendor", true, said) == LandedErrand::NotLanded);
}

void LeavingTheAcceptedCounterClaimsTheSameAimAgain()
{
    CounterArrivalMemory memory;
    NoteCounterArrival(memory, "vendor");
    bool said = false;
    Check("the remembered counter still accepts the leader: stands down",
          Poll(memory, "vendor", true, said) == LandedErrand::StandDown);
    Check("outside that counter's gate with the hold still in force: resumes",
          Poll(memory, "vendor", true, said, false) == LandedErrand::Resume);
    Check("leaving does not log another counter hold", !said);
    Check("the next poll treats the same aim as a normal errand",
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
    auto const note = text.find("NoteCounterArrival(arrival.memory, target)", mark);
    auto const landed = text.find("_landed.find(name)", mark);
    Check("the arrival is noted before the landing is looked for", note != std::string::npos &&
                                                                    note < landed);
    Check("the saved GUID and role do not depend on an unclaimed landing",
          text.find("arrival.counterGuid = counterGuid", mark) < landed &&
              text.find("arrival.npcFlag = npcFlag", mark) < landed);

    auto const sweep = text.find("static bool CounterInReach(");
    auto const accepted = text.find("who->GetNPCIfCanInteractWith(creature->GetGUID(), npcFlag)", sweep);
    auto const saved = text.find("*outAcceptedCounter = creature->GetGUID()", accepted);
    auto const flight = text.find("static bool FlightMasterInReach(", sweep);
    Check("arrival records a creature accepted by the core's counter gate", saved < flight);

    auto const step = text.find("OverseerDecisions::LandedErrand LandedStep(");
    auto const recheck = text.find("bot->GetNPCIfCanInteractWith(remembered.counterGuid, remembered.npcFlag) != nullptr", step);
    auto const decide = text.find("CounterArrivalMemoryStep(remembered.memory", step);
    auto const observable = text.find("columnAim && bot &&\n"
                                      "                bot->IsAlive() && !bot->IsInFlight() && !bot->IsBeingTeleported()",
                                      step);
    Check("an unobservable player state does not masquerade as counter departure", observable < recheck);
    Check("the saved counter GUID and role flag are rechecked before the arrival decision",
          recheck < decide &&
              text.find("counterHoldActive, counterInReach, sayIt)", decide) != std::string::npos);
    Check("a retained column landing also uses the gate outcome",
          text.find("it->second.atCounter, counterHoldActive && counterInReach", decide) !=
              std::string::npos);

    auto const drive = text.find("void DriveTravel()");
    auto const standDown = text.find("LandedStep(name, target, HasCounterHold(name), botAI ? bot : nullptr)", drive);
    auto const claim = text.find("AskForLeader(name, kind, owner, about", drive);
    Check("the drive checks the live gate before it can reclaim the leader", standDown < claim);
    auto const reach = text.find("CounterInReach(bot, counter, oneIsNearby, nearestYards, &acceptedCounter)", claim);
    auto const record = text.find("MarkCounterLanding(name, target, acceptedCounter,", reach);
    Check("the drive records the accepted counter with the original role flag",
          record != std::string::npos && text.find("NpcFlagForCounter(counter));", record) != std::string::npos);
}

}  // namespace

int main()
{
    TheSameCounterIsNotClaimedAgainWhileTheHoldStands();
    AnotherAimIsClaimedAsBefore();
    TheHoldEndingClaimsTheSameAimAgain();
    LeavingTheAcceptedCounterClaimsTheSameAimAgain();
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
