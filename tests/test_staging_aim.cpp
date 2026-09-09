/*
 * Whether a dungeon run's staging aim is still the run's to measure.
 *
 * The live failure this pins is a campaign that never gets a run started.
 * Measured on the dev realm across one afternoon: three consecutive runs of one
 * campaign each died in GATHERING, and three staging failures in a row stop the
 * campaign. The leader was aimed at the start of the approach corridor, the
 * route was planned correctly - four surveyed nodes, 2238 yards of walking legs,
 * no surveyed leg laid over the corridor's own ground - and 2238 yards at run
 * speed is about five minutes against a twelve minute window. It never arrived.
 * Its gap to the leg it was walking wandered between about 1000 and 1500 yards
 * for the whole window and its height relative to that point changed sign, which
 * is a character grinding rather than a character walking a route.
 *
 * It was grinding. GATHERING claimed the leader's staging aim once per leg and
 * never again, which made it the only walking mechanism in the module that does
 * not renew its own aim: the barrier re-claims through its escort on every poll,
 * and so do the crossing, the catch-up walk and the home errand. The leader on a
 * staging aim is the one claimant with no escort entry to re-claim from - he is
 * aimed, not escorted - so no sweep covered him, and every path that can end an
 * errand ended his silently. The travel focus was swept with the errand, which
 * put `grind`, `quest` and `move random` back on his non-combat engine, and the
 * follow drive re-granted him `new rpg` because he leads.
 *
 * None of the three corrections could reach that. The first re-asserts a focus
 * record that was erased with the errand and therefore reports "nothing had come
 * back on, so this is not what is holding it" about the exact thing that was
 * holding it. The second sets a re-issue flag on a travel record the errand
 * loader will never return, because the column is empty. The third clears a
 * movement generator that `grind` refills on the next tick.
 *
 * So the question that was never asked is the one here: has the claim gone from
 * under a leg nobody changed?
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::StagingAim;
using OverseerDecisions::StagingAimRestartsMeasurement;
using OverseerDecisions::StagingAimStep;

namespace
{

int failures = 0;

char const* Name(StagingAim aim)
{
    switch (aim)
    {
        case StagingAim::Hold:   return "hold";
        case StagingAim::NewLeg: return "new leg";
        case StagingAim::Rearm:  return "re-arm";
    }
    return "unknown";
}

void CheckAim(char const* what, StagingAim got, StagingAim want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// THE ONE THE ISSUE IS ABOUT, AND THE ONE READING THAT DID NOT EXIST BEFORE.
// The aim string has not moved, because the leg has not changed and the corridor
// is where it always was, and the claim this run made on it is gone. That is not
// a walk being measured; it is a character with no errand, and the run has to
// take it back rather than spend another rung of a ladder on it.
void ALostClaimUnderAnUnchangedLegIsARearm()
{
    CheckAim("claim gone, leg unchanged", StagingAimStep(false, false),
             StagingAim::Rearm);
}

// ...AND THE STEADY STATE IS STILL SILENT. A leader walking a leg he is still
// claimed on is the ordinary poll, five seconds apart for the whole of a walk
// that legitimately takes minutes, and it must cost a comparison and produce no
// line and no write.
void AStandingClaimOnTheSameLegHolds()
{
    CheckAim("claim stands, leg unchanged", StagingAimStep(false, true),
             StagingAim::Hold);
}

// A LEG CHANGE IS THE ORDINARY HANDOVER AND NOT A FAULT (#242). The poll a
// leader reaches the start of the approach corridor is the poll the leg becomes
// Direct, and the run aims him at the door in the same poll. It has always
// claimed there and still does.
void AChangedLegIsANewLeg()
{
    CheckAim("leg changed, claim stands", StagingAimStep(true, true),
             StagingAim::NewLeg);
}

// AND A LEG CHANGE OUTRANKS A LOST CLAIM, which is the case that decides whether
// this is usable at all. Both are true on exactly the poll a leader arrives at
// the corridor: arriving inside the errand's own tolerance releases the aim, and
// the same poll hands him on to the next point. Reported as a re-arm, that would
// put a warning about an errand ending under the run into the log on every
// single successful handover, which is the normal thing this phase is for.
void ALegChangeOutranksALostClaim()
{
    CheckAim("leg changed and claim gone", StagingAimStep(true, false),
             StagingAim::NewLeg);
}

// THE WHOLE TRUTH TABLE, WRITTEN OUT. Two booleans is four rows and there is no
// reason to leave any of them to inference: this decides whether a dungeon
// campaign runs at all, and a reader should be able to see every answer without
// running it.
void EveryCombinationIsWhatItSays()
{
    struct Row { bool legChanged; bool runStillOwns; StagingAim want; char const* what; };
    Row const rows[] = {
        {false, false, StagingAim::Rearm,  "CLAIM GONE under an unchanged leg - the defect"},
        {false, true,  StagingAim::Hold,   "walking on, still claimed"},
        {true,  false, StagingAim::NewLeg, "handover, and the old aim was released by it"},
        {true,  true,  StagingAim::NewLeg, "handover from the corridor to the door"},
    };
    for (Row const& row : rows)
        CheckAim(row.what, StagingAimStep(row.legChanged, row.runStillOwns), row.want);
}

// BOTH OF THE ANSWERS THAT WRITE AN AIM ALSO START THE MEASUREMENT AGAIN, and
// only those two. The ladder measures the best distance to the thing being
// walked at; after a leg change that thing is a different point, and after a
// re-arm the yards already measured were measured on a character nothing was
// walking. Carrying either mark forward climbs to GiveUp on a leader that is
// walking correctly, which is precisely what #242 found for the leg change.
void OnlyTheAnswersThatWriteAnAimRestartTheMeasurement()
{
    Check("a re-arm restarts it",
          StagingAimRestartsMeasurement(StagingAim::Rearm), true);
    Check("a new leg restarts it",
          StagingAimRestartsMeasurement(StagingAim::NewLeg), true);
    Check("holding does not restart it",
          StagingAimRestartsMeasurement(StagingAim::Hold), false);
}

// EXACTLY ONE ROW SAYS "DO NOTHING", which is the property that makes this safe
// to have got wrong in the cheap direction. A redundant claim costs a map lookup
// and, at worst, one UPDATE that writes the string already there; a withheld one
// costs a dungeon campaign, which is what it cost.
void ExactlyOneRowDoesNothing()
{
    int held = 0;
    for (int legChanged = 0; legChanged < 2; ++legChanged)
        for (int owns = 0; owns < 2; ++owns)
            if (StagingAimStep(legChanged != 0, owns != 0) == StagingAim::Hold)
                ++held;
    if (held == 1)
        return;
    std::printf("FAIL exactly one row does nothing: got %d\n", held);
    ++failures;
}

// AND NOTHING IS REMEMBERED BETWEEN POLLS, on purpose. Every other watchdog in
// this module carries state and has had to explain what a restart costs it; this
// one is two readings taken fresh every poll, so a bounced worldserver loses
// nothing and a run adopted from another process is answered correctly on its
// first poll. Asserted by asking the same question twice in both orders.
void TheAnswerDependsOnNothingButItsArguments()
{
    CheckAim("re-arm, first ask", StagingAimStep(false, false), StagingAim::Rearm);
    CheckAim("hold in between", StagingAimStep(false, true), StagingAim::Hold);
    CheckAim("re-arm, asked again", StagingAimStep(false, false), StagingAim::Rearm);
    CheckAim("new leg after a re-arm", StagingAimStep(true, false), StagingAim::NewLeg);
    CheckAim("re-arm after a new leg", StagingAimStep(false, false), StagingAim::Rearm);
}

}  // namespace

int main()
{
    ALostClaimUnderAnUnchangedLegIsARearm();
    AStandingClaimOnTheSameLegHolds();
    AChangedLegIsANewLeg();
    ALegChangeOutranksALostClaim();
    EveryCombinationIsWhatItSays();
    OnlyTheAnswersThatWriteAnAimRestartTheMeasurement();
    ExactlyOneRowDoesNothing();
    TheAnswerDependsOnNothingButItsArguments();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a staging aim nothing renews is an aim nobody is walking\n");
    return EXIT_SUCCESS;
}
