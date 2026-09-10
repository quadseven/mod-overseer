/*
 * Whether a character walked to a counter is still standing at it when the row
 * that transacts there arrives.
 *
 * The live failure this pins is the whole town trip. Counted off the module's
 * own command table on the dev realm, all time:
 *
 *     sell     23025 error    1608 delivered     6.5%
 *     repair      52 error      15 delivered      22%
 *     buy         20 error       6 delivered      23%
 *     bank        66 error       0 delivered    never
 *
 * and one detail dominates every one of them - "vendor not in range" is 17200
 * of the 23025, "repairer not in range" 47 of 52, "banker not in range" 52 of
 * 66. The rest of the sell errors are about the seller rather than the counter:
 * not online, item not carried, dead, in flight, count exceeds stack.
 *
 * Two things were wrong and they compounded.
 *
 * The travel drive released the errand the poll it read as arrived, and that
 * release is the signal the pass outside the worldserver waits for before it
 * writes any rows at all. So the transaction was always asked for strictly
 * after this module stopped holding on to the character, and by then upstream
 * had ended the walk: an arrived aimed wander calls ChangeToIdle and the next
 * AI tick rolls RPG_IDLE into a randomly chosen status, two of which walk
 * somewhere. That is the same race measured at a dungeon door and then at an
 * inn, and the same answer applies - a hold is not a competition and a
 * re-issued walk is.
 *
 * And "arrived" was twelve yards, measured against a spawn row, while every one
 * of sell, repair and bank is gated on the core's own GetNPCIfCanInteractWith,
 * which is five. A character eleven yards from a vendor's spawn had "completed"
 * its errand and could not sell anything.
 *
 * So the arrival question for a counter has three answers rather than two, and
 * the input that makes the hold safe is the core's own gate rather than a
 * distance: a hold is a promise that standing still is what completes the
 * errand, and it is a false promise the moment the creature is not reachable
 * from where the character stands.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CounterArrival;
using OverseerDecisions::CounterArrivalStep;
using OverseerDecisions::CounterRole;
using OverseerDecisions::CounterRoleForAim;
using OverseerDecisions::IsMaintenanceErrand;

namespace
{

int failures = 0;

char const* Name(CounterArrival arrival)
{
    switch (arrival)
    {
        case CounterArrival::Done:          return "done";
        case CounterArrival::CloseTheGap:   return "close the gap";
        case CounterArrival::StandAndTrade: return "stand and trade";
    }
    return "unknown";
}

char const* Name(CounterRole role)
{
    switch (role)
    {
        case CounterRole::None:     return "none";
        case CounterRole::Vendor:   return "vendor";
        case CounterRole::Banker:   return "banker";
        case CounterRole::Repairer: return "repairer";
        case CounterRole::Auctioneer: return "auctioneer";
    }
    return "unknown";
}

void CheckArrival(char const* what, CounterArrival got, CounterArrival want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckRole(char const* aim, CounterRole got, CounterRole want)
{
    if (got == want)
        return;
    std::printf("FAIL role of '%s': got %s, wanted %s\n", aim, Name(got), Name(want));
    ++failures;
}

// THE ONE THE ISSUE IS ABOUT. A character the core will let interact is stood
// still, so the row that arrives a moment later finds it at the counter rather
// than wherever the next rolled status took it.
void ACharacterAtTheCounterIsHeldThere()
{
    CheckArrival("at the vendor, gate accepts",
                 CounterArrivalStep(CounterRole::Vendor, true, true),
                 CounterArrival::StandAndTrade);
    CheckArrival("at the banker, gate accepts",
                 CounterArrivalStep(CounterRole::Banker, true, true),
                 CounterArrival::StandAndTrade);
    CheckArrival("at the repairer, gate accepts",
                 CounterArrivalStep(CounterRole::Repairer, true, true),
                 CounterArrival::StandAndTrade);
}

// THE SECOND HALF OF THE DEFECT, AND IT IS A SEPARATE ANSWER. Twelve yards from
// a spawn row is inside the arrival radius and outside the interact gate. The
// old branch called that "errand done, releasing" and the sale that followed
// was refused "vendor not in range". Not releasing is what lets upstream's own
// aimed wander close the last few yards.
void ACounterNearbyAndOutOfReachIsNotAnErrandThatIsOver()
{
    CheckArrival("vendor eight yards off its row",
                 CounterArrivalStep(CounterRole::Vendor, false, true),
                 CounterArrival::CloseTheGap);
    CheckArrival("banker nearby, gate refuses",
                 CounterArrivalStep(CounterRole::Banker, false, true),
                 CounterArrival::CloseTheGap);
}

// AND AN EMPTY SPAWN IS A THIRD ANSWER, WHICH IS WHY THERE ARE THREE. Waiting
// on ground where nothing of that role stands is the pin the inn hold already
// refused to place, and it would cost more here: the errand's own backstop is
// twenty minutes and the run waiting for this trip is bounded by the same
// number. Releasing hands the problem back to whatever wrote the aim.
void ACounterAimWithNothingThereIsReleased()
{
    CheckArrival("nothing of the role in the sweep",
                 CounterArrivalStep(CounterRole::Vendor, false, false),
                 CounterArrival::Done);
}

// EVERY OTHER CREATURE ERRAND IS UNCHANGED, BYTE FOR BYTE. A trainer, a flight
// master, a stable master and a tabard designer all still arrive and release,
// and this is the test that says a new reason to hold did not quietly become a
// reason to hold everybody.
void ANonCounterErrandStillJustArrives()
{
    for (int reach = 0; reach < 2; ++reach)
        for (int nearby = 0; nearby < 2; ++nearby)
            CheckArrival("not a counter aim",
                         CounterArrivalStep(CounterRole::None, reach != 0, nearby != 0),
                         CounterArrival::Done);
}

// THE WHOLE TRUTH TABLE FOR ONE ROLE, WRITTEN OUT. Two booleans is four rows and
// there is no reason to leave any of them to inference.
void EveryCombinationIsWhatItSays()
{
    struct Row { bool reach; bool nearby; CounterArrival want; char const* what; };
    Row const rows[] = {
        {false, false, CounterArrival::Done,          "empty spawn"},
        {false, true,  CounterArrival::CloseTheGap,   "nearby, out of reach - the defect"},
        {true,  false, CounterArrival::StandAndTrade, "in reach, sweep disagreed"},
        {true,  true,  CounterArrival::StandAndTrade, "in reach - held"},
    };
    for (Row const& row : rows)
        CheckArrival(row.what, CounterArrivalStep(CounterRole::Vendor, row.reach,
                                                  row.nearby),
                     row.want);
}

// A HOLD IS NEVER TAKEN WHERE THE GATE REFUSES, whatever else is true. This is
// the property the whole safety argument rests on, so it is asserted as a
// property rather than read out of the table above: an unfriendly vendor is
// turned down however close a character stands, and pinning a character in
// front of one would be this fix removing the only way it could get lucky.
void NothingOutOfReachIsEverHeld()
{
    for (int role = 0; role < 4; ++role)
        for (int nearby = 0; nearby < 2; ++nearby)
        {
            CounterRole const which = static_cast<CounterRole>(role);
            if (CounterArrivalStep(which, false, nearby != 0) == CounterArrival::StandAndTrade)
            {
                std::printf("FAIL held out of reach (role=%s nearby=%d)\n", Name(which),
                            nearby);
                ++failures;
            }
        }
}

// AND NOTHING IS EVER LEFT WALKING AT AN AIM THAT IS NOT A COUNTER, which is the
// other half: CloseTheGap declines to release an errand, and declining that for
// a trainer would leave a character standing in front of one for twenty minutes
// with the whole quest drive waiting on the handback.
void OnlyACounterEverDeclinesToRelease()
{
    for (int reach = 0; reach < 2; ++reach)
        for (int nearby = 0; nearby < 2; ++nearby)
            if (CounterArrivalStep(CounterRole::None, reach != 0, nearby != 0)
                != CounterArrival::Done)
            {
                std::printf("FAIL a non-counter aim did not simply arrive "
                            "(reach=%d nearby=%d)\n", reach, nearby);
                ++failures;
            }
}

// THE VOCABULARY, WHICH IS THE OTHER THING THIS CHANGE OWNS. `travel_npc` also
// carries `at:` and `trigger:` aims and nine other role keywords, and reading any
// of those as a counter would put a hold on a character that has to keep walking.
//
// AND IT IS FOUR KEYWORDS NOW, NOT THREE (#402). `auctioneer` was asserted here
// as NOT a counter, and that assertion was wrong rather than the code being
// right: it is one of the keywords TravelRoles() resolves, so a character could
// always be sent to one and did arrive, and answering None meant
// CounterArrivalStep said Done on its first line, no hold was taken, and the
// errand released before the auction rows could find the character still
// standing at the counter. This test was pinning the defect in place. The header
// note below about a FOURTH economy errand needing to reach both readers is
// exactly this case arriving.
void OnlyTheFourEconomyKeywordsAreCounters()
{
    CheckRole("vendor", CounterRoleForAim("vendor"), CounterRole::Vendor);
    CheckRole("banker", CounterRoleForAim("banker"), CounterRole::Banker);
    CheckRole("repair", CounterRoleForAim("repair"), CounterRole::Repairer);
    CheckRole("auctioneer", CounterRoleForAim("auctioneer"), CounterRole::Auctioneer);

    char const* const notCounters[] = {
        "",
        "trainer",
        "class trainer",
        "profession trainer",
        "guild banker",
        "petitioner",
        "tabard designer",
        "innkeeper",
        "flight master",
        "stable master",
        "at:1:-720.5,-2226.1,17.0",
        "trigger:78",
        "1234",
    };
    for (char const* aim : notCounters)
        CheckRole(aim, CounterRoleForAim(aim), CounterRole::None);
}

// AND THE TWO READERS OF THAT VOCABULARY AGREE, BY CONSTRUCTION RATHER THAN BY
// PROMISE. The run gate asks IsMaintenanceErrand about `travel_npc` and the
// travel drive's arrival branch asks CounterRoleForAim about the same column. A
// fourth economy errand added to one and not the other would be a run opening on
// top of a trip, which is the defect the run gate exists to stop.
void TheRunGateAndTheArrivalBranchAgree()
{
    char const* const everyAim[] = {
        "", "vendor", "banker", "repair", "trainer", "class trainer",
        "profession trainer", "guild banker", "auctioneer", "petitioner",
        "tabard designer", "innkeeper", "flight master", "stable master",
        "at:0:-8913.2,554.6,93.8", "trigger:78", "1234",
    };
    for (char const* aim : everyAim)
    {
        bool const isCounter = CounterRoleForAim(aim) != CounterRole::None;
        if (isCounter != IsMaintenanceErrand(aim))
        {
            std::printf("FAIL '%s': the run gate says %s and the arrival branch says %s\n",
                        aim, IsMaintenanceErrand(aim) ? "economy" : "not economy",
                        isCounter ? "counter" : "not a counter");
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    ACharacterAtTheCounterIsHeldThere();
    ACounterNearbyAndOutOfReachIsNotAnErrandThatIsOver();
    ACounterAimWithNothingThereIsReleased();
    ANonCounterErrandStillJustArrives();
    EveryCombinationIsWhatItSays();
    NothingOutOfReachIsEverHeld();
    OnlyACounterEverDeclinesToRelease();
    OnlyTheFourEconomyKeywordsAreCounters();
    TheRunGateAndTheArrivalBranchAgree();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a character that will not stand still cannot sell anything\n");
    return EXIT_SUCCESS;
}
