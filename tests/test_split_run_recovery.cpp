/*
 * A run that hearths its family out does not call the family back.
 *
 * THE READING THIS EXISTS FOR, measured on the dev realm 2026-11-01. Run
 * 1238102 stopped 394 yards from the Uldaman door and its walk out failed. Four
 * members hearthed to Stormwind. The fifth, bound in Ratchet, had its stone
 * withheld to keep the family on one continent, so it stayed inside alone. Ten
 * seconds later IDLE adopted the run at STAGED_INSIDE with 1 of 5 inside and
 * sent the four straight back to the door, and the run closed `split_failed`
 * with whoever arrived last still outside. Every `split_failed` the family took
 * after the bag fix came from this.
 *
 * Pinned here: the withheld stone is released once the family it was protecting
 * has gone, a run that is over is walked out rather than staged into, and a
 * member too far away to arrive does not hold the party for the full backstop.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonCompletion;
using OverseerDecisions::DungeonRunAdoptAt;
using OverseerDecisions::DungeonRunAdoptPhase;
using OverseerDecisions::DungeonRunEntryBlockers;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::ExitFailureHearthStep;
using OverseerDecisions::ExitHearthFacts;
using OverseerDecisions::ExitHearthStep;
using OverseerDecisions::OffContinentHearthLetThrough;
using OverseerDecisions::StagedInsideBackstopSeconds;

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

constexpr time_t FULL_BACKSTOP = 15 * 60;

// The fifth member as run 1238102 left it: alive, standing, out of combat, stone
// ready, bound on map 1 behind a door that lets out on map 0, and the walk out
// failed for the first time a moment ago.
ExitHearthFacts WithheldStone()
{
    ExitHearthFacts facts;
    facts.inWorld = true;
    facts.onInsideMap = true;
    facts.alive = true;
    facts.carriesStone = true;
    facts.doorMapKnown = true;
    facts.doorMapId = 0;
    facts.bindMapId = 1;
    facts.failedWalkSeconds = 0;
    return facts;
}

void TheWithheldStoneIsReleasedOnceTheFamilyHasGone()
{
    ExitHearthFacts facts = WithheldStone();
    Check("the family still inside keeps the stone withheld",
          ExitFailureHearthStep(facts) == ExitHearthStep::Impossible);

    facts.familyHearthedOut = true;
    Check("the family gone leaves nothing for the withhold to protect",
          OffContinentHearthLetThrough(facts));
    Check("so the last member hearths out with them",
          ExitFailureHearthStep(facts) == ExitHearthStep::Cast);

    ExitHearthFacts cooling = WithheldStone();
    cooling.familyHearthedOut = true;
    cooling.onCooldown = true;
    Check("a stone on cooldown is still no way out",
          ExitFailureHearthStep(cooling) == ExitHearthStep::Impossible);

    ExitHearthFacts homeside = WithheldStone();
    homeside.bindMapId = 0;
    homeside.familyHearthedOut = true;
    Check("a stone bound on the door's own map was never withheld",
          !OffContinentHearthLetThrough(homeside));
}

void ARunThatIsOverIsWalkedOutNotStagedInto()
{
    Check("one member left inside an unfinished, finished-with run walks out",
          DungeonRunAdoptAt(false, DungeonCompletion::NotYet, true) ==
              DungeonRunAdoptPhase::Exiting);
    Check("the whole party inside a finished-with run walks out",
          DungeonRunAdoptAt(true, DungeonCompletion::NotYet, true) ==
              DungeonRunAdoptPhase::Exiting);
    Check("a run nobody closed is still staged",
          DungeonRunAdoptAt(false, DungeonCompletion::NotYet) ==
              DungeonRunAdoptPhase::StagedInside);
    Check("and one with everybody inside is still cleared",
          DungeonRunAdoptAt(true, DungeonCompletion::NotYet) ==
              DungeonRunAdoptPhase::Clearing);
}

DungeonRunEntryState Inside()
{
    DungeonRunEntryState state;
    state.seen = true;
    state.alive = true;
    state.through = true;
    return state;
}

DungeonRunEntryState Outside(float yards)
{
    DungeonRunEntryState state;
    state.seen = true;
    state.alive = true;
    state.distanceFromDoor = yards;
    state.insideDoor = false;
    return state;
}

void AMemberTooFarToArriveEndsTheHoldEarly()
{
    std::vector<DungeonRunEntryState> party(4, Inside());
    party.push_back(Outside(126.f));
    Check("a member within a walk of the door gets the full backstop",
          StagedInsideBackstopSeconds(party, FULL_BACKSTOP) == FULL_BACKSTOP);

    party.back() = Outside(9076.f);
    time_t const far = StagedInsideBackstopSeconds(party, FULL_BACKSTOP);
    Check("a member 9076 yards out cannot arrive, so the run ends inside 5 minutes",
          far > 0 && far <= 5 * 60);

    party.back() = DungeonRunEntryState();
    party.back().seen = true;
    party.back().alive = true;
    Check("a member on another map cannot arrive either",
          StagedInsideBackstopSeconds(party, FULL_BACKSTOP) <= 5 * 60);

    party.back() = DungeonRunEntryState();
    Check("a member out of the world is not measured, so it keeps the backstop",
          StagedInsideBackstopSeconds(party, FULL_BACKSTOP) == FULL_BACKSTOP);

    party.back() = Inside();
    party.back().through = false;
    party.back().otherCopy = true;
    Check("a member in another copy is walked out by its own rule",
          StagedInsideBackstopSeconds(party, FULL_BACKSTOP) == FULL_BACKSTOP);
}

void TheReasonNamesWhyTheMemberIsNotComing()
{
    std::vector<DungeonRunEntryState> party(1, Outside(9076.f));
    party[0].name = "Member";
    std::string const far = DungeonRunEntryBlockers(party, 8.f);
    Check("a far member is named with its distance",
          far.find("9076y from the door") != std::string::npos);

    party[0].distanceFromDoor = 126.f;
    Check("a near member keeps the old word",
          DungeonRunEntryBlockers(party, 8.f) == "Member (outside the trigger)");
}

}  // namespace

int main()
{
    TheWithheldStoneIsReleasedOnceTheFamilyHasGone();
    ARunThatIsOverIsWalkedOutNotStagedInto();
    AMemberTooFarToArriveEndsTheHoldEarly();
    TheReasonNamesWhyTheMemberIsNotComing();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_split_run_recovery: all passed\n");
    return 0;
}
