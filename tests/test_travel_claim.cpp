/*
 * Whether the travel book may write an aim, and what its release gate says
 * when it declines to blank one (#560, #561).
 *
 * THE CLAIM. TravelAimBook::Claim refused any aim while a profession errand was
 * outstanding and returned nothing, so a catch-up walk refused for a follower
 * with `learn_skill` set was logged by its caller as started, and the family
 * held its leader for a walk that never began. The follower's column was
 * empty: the bridge aims a learn errand only at a family leader, so there was
 * no trainer walk to protect. A catch-up aim over an empty column is now
 * written; every other claim keeps the #435 fence.
 *
 * THE RELEASE LINE. When the profession fence fired, the release line printed
 * the empty `travel_npc` column as "errand ''". It now names the fence that
 * fired, and never prints an empty errand.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadTravelClaim;
using OverseerDecisions::TravelClaim;
using OverseerDecisions::TravelClaimFacts;
using OverseerDecisions::TravelReleaseFence;

namespace
{

int failures = 0;

char const* Name(TravelClaim claim)
{
    switch (claim)
    {
        case TravelClaim::Write:             return "write";
        case TravelClaim::RefusedProfession: return "refused (profession)";
        case TravelClaim::RefusedForeign:    return "refused (foreign)";
    }
    return "unknown";
}

void CheckClaim(char const* what, TravelClaim got, TravelClaim want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

TravelClaimFacts Facts(uint32_t learnSkill, char const* column, bool ours, bool catchUp)
{
    TravelClaimFacts facts;
    facts.learnSkill = learnSkill;
    facts.column = column;
    facts.columnIsOurs = ours;
    facts.catchUp = catchUp;
    return facts;
}

char const* const CATCH_UP = "at:1:-159.776,-5076.93,20.7455";

// THE THREE CASES THE ISSUE NAMES, for a catch-up aim.
void ACatchUpClaimAcrossTheThreeCases()
{
    // The measured case: skill 202 outstanding, column empty. Refused before.
    CheckClaim("learn_skill set over an empty column is written",
               ReadTravelClaim(Facts(202, "", false, true)), TravelClaim::Write);
    // A foreign aim is somebody's walk, and the profession fence still stands
    // in front of it.
    CheckClaim("learn_skill set over a foreign aim is refused",
               ReadTravelClaim(Facts(202, "vendor", false, true)),
               TravelClaim::RefusedProfession);
    CheckClaim("a trainer walk in the column is refused",
               ReadTravelClaim(Facts(202, "trainer", false, true)),
               TravelClaim::RefusedProfession);
    CheckClaim("nothing set is written", ReadTravelClaim(Facts(0, "", false, true)),
               TravelClaim::Write);
    CheckClaim("a foreign aim alone is refused as foreign",
               ReadTravelClaim(Facts(0, "at:1:1,2,3", false, true)),
               TravelClaim::RefusedForeign);
}

// THE #435 FENCE IS UNCHANGED FOR EVERY OTHER CLAIM. A run's aim over a leader
// with a profession errand is still refused, empty column or not.
void ARunClaimKeepsTheProfessionFence()
{
    CheckClaim("a run's aim with learn_skill set is refused",
               ReadTravelClaim(Facts(186, "", false, false)),
               TravelClaim::RefusedProfession);
    CheckClaim("a run's aim with nothing set is written",
               ReadTravelClaim(Facts(0, "", false, false)), TravelClaim::Write);
    CheckClaim("a run's aim over a vendor errand is refused",
               ReadTravelClaim(Facts(0, "vendor", false, false)),
               TravelClaim::RefusedForeign);
}

// AN AIM THE BOOK ALREADY WROTE IS REPLACED. A catch-up re-aim and a run's
// next leg both write over the book's own previous `at:` aim, which the foreign
// fence would otherwise refuse as if the bridge had written it.
void TheBooksOwnAimIsReplaced()
{
    CheckClaim("a catch-up re-aim over the book's own aim is written",
               ReadTravelClaim(Facts(0, CATCH_UP, true, true)), TravelClaim::Write);
    CheckClaim("and so is a run's next leg",
               ReadTravelClaim(Facts(0, "at:1:1,2,3", true, false)),
               TravelClaim::Write);
    CheckClaim("an empty column is not the book's to vouch for",
               ReadTravelClaim(Facts(186, "", true, false)),
               TravelClaim::RefusedProfession);
}

// #561. The release line names the fence that fired.
void TheReleaseLineNamesTheFence()
{
    CheckText("no fence says nothing", TravelReleaseFence(0, ""), "");
    CheckText("a keyword the book may clear says nothing",
              TravelReleaseFence(0, "trainer"), "");
    CheckText("the profession fence names the skill", TravelReleaseFence(197, ""),
              "a profession errand (skill 197) is outstanding");
    CheckText("the foreign fence names the errand", TravelReleaseFence(0, "vendor"),
              "errand 'vendor' is outstanding");
    CheckText("both fences name both", TravelReleaseFence(202, "at:1:1,2,3"),
              "a profession errand (skill 202) and errand 'at:1:1,2,3' are outstanding");
    CheckText("a profession fence over a non-foreign keyword names only the skill",
              TravelReleaseFence(202, "trainer"),
              "a profession errand (skill 202) is outstanding");
    std::string const measured = TravelReleaseFence(197, "");
    if (measured.find("errand ''") != std::string::npos)
    {
        std::printf("FAIL the line printed an empty errand: '%s'\n", measured.c_str());
        ++failures;
    }
}

}  // namespace

int main()
{
    ACatchUpClaimAcrossTheThreeCases();
    ARunClaimKeepsTheProfessionFence();
    TheBooksOwnAimIsReplaced();
    TheReleaseLineNamesTheFence();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a claim says whether it wrote, and a release says which fence held\n");
    return EXIT_SUCCESS;
}
