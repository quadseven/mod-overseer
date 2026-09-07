/*
 * Which errands a follower cut off from its leader may still run.
 *
 * The live failure this pins is a refusal that covered too much. On the dev
 * realm 2026-09-07 three of five roster characters were on the continent their
 * dungeon is on and the leader was on the other. All five bind at ONE point in
 * the starting zone on the wrong continent, 0.68 yards apart, so every death
 * drags the three who are already in the right place back across an ocean. The
 * fix for that is to walk one of them to an innkeeper and bind it there. It was
 * refused:
 *
 *   00:16:28 INFO 'Grog' was sent to 'innkeeper' but does not carry `new rpg` -
 *                 nothing walks it anywhere. Followers travel by following the
 *                 leader; aim the leader instead
 *
 * and when the strategy was put on by hand it lasted nine seconds:
 *
 *   00:18:04 INFO command ('nc +new rpg' for 'Grog') applied
 *   00:18:13 WARN 'Grog' follows but carries `new rpg` with no escort asking
 *                 for it - taking it back
 *
 * Nothing here reunites anybody, and these cases are written so a reader cannot
 * mistake them for a fix: `follow` still cannot cross a map and neither can an
 * `at:` aim. What the verdict buys is that the ONE errand that would stop the
 * split recurring is no longer blocked by the split.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ErrandRunsAlone;
using OverseerDecisions::ReadSplitErrand;
using OverseerDecisions::SplitErrand;
using OverseerDecisions::SplitErrandName;

namespace
{

int failures = 0;

void CheckErrand(char const* target, SplitErrand want)
{
    SplitErrand const got = ReadSplitErrand(target);
    if (got == want)
        return;
    std::printf("FAIL '%s': got %s, wanted %s\n", target, SplitErrandName(got),
                SplitErrandName(want));
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

// Every keyword `overseer_roster.travel_npc` may name, copied from TravelRoles()
// in src/mod_overseer.cpp. Not shared with it on purpose: that table lives
// beside the NPC flags it maps onto and this decision deliberately does not
// consult it, so this list is here as the STATEMENT that all thirteen are
// self-contained rather than as a dependency on the table's shape.
std::vector<std::string> const& EveryRole()
{
    static std::vector<std::string> const roles = {
        "trainer",         "class trainer",   "profession trainer",
        "vendor",          "repair",          "banker",
        "guild banker",    "auctioneer",      "petitioner",
        "tabard designer", "innkeeper",       "flight master",
        "stable master",
    };
    return roles;
}

// THE ERRAND THE WHOLE ISSUE IS ABOUT. An innkeeper is a creature on this
// character's own map, and standing in front of it needs nobody else at all.
void TheOneErrandThatEndsTheSplitRunsAlone()
{
    CheckErrand("innkeeper", SplitErrand::SelfContained);
    Check("an innkeeper errand runs alone", ErrandRunsAlone("innkeeper"), true);
}

// ...AND SO DOES EVERY OTHER ROLE, which is the part that had to be checked one
// by one rather than asserted. Arriving is all a travel errand does: the
// transaction at the far end is its own command every time, so even the two
// that sound like they need other people do not. Signing a guild charter at a
// `petitioner` is a separate command, and a `flight master` is resolved against
// this character's own map and own team.
void EveryRoleAnErrandCanNameRunsAlone()
{
    for (std::string const& role : EveryRole())
    {
        CheckErrand(role.c_str(), SplitErrand::SelfContained);
        Check("a role errand runs alone", ErrandRunsAlone(role), true);
    }
}

// A BARE CREATURE ENTRY IS A ROLE OF ONE. `travel_npc` takes a numeric spawn id
// as well as a keyword, and naming one NPC is naming one NPC: still on this
// character's own map, still nobody else's business.
void ABareCreatureEntryRunsAlone()
{
    CheckErrand("3934", SplitErrand::SelfContained);
    CheckErrand("6738", SplitErrand::SelfContained);
    Check("a numeric entry runs alone", ErrandRunsAlone("6738"), true);
}

// AND THE TWO SHAPES THIS MODULE WRITES FOR ITSELF DO NOT. A point is chosen
// relative to the rest of the family - the catch-up walk aims at where the
// LEADER stands, the dungeon approach aims at a doorway, the staging barrier
// aims at a point picked for the party - and a split is exactly what makes all
// three impossible.
void APointAimStillNeedsTheFamily()
{
    CheckErrand("at:0:-8949.95,-132.493,83.5312", SplitErrand::NeedsTheFamily);
    CheckErrand("at:1:298,-2243.9,95.5", SplitErrand::NeedsTheFamily);
    CheckErrand("trigger:4247", SplitErrand::NeedsTheFamily);
    Check("a point aim does not run alone",
          ErrandRunsAlone("at:1:298,-2243.9,95.5"), false);
    Check("a doorway does not run alone", ErrandRunsAlone("trigger:4247"), false);
}

// AN `at:` NAMING THIS CHARACTER'S OWN MAP IS REFUSED TOO, and that is the
// deliberate part rather than an oversight. Such a walk would in fact be
// takeable; refusing it is what keeps a split follower from walking off under
// an aim the run coordinator meant for a party. The decision cannot tell those
// two apart from the string, so it takes the narrower answer.
void EvenAWalkablePointAimIsRefused()
{
    // The same map the follower measured in the issue was standing on.
    CheckErrand("at:1:-407.1,-2645.2,95.0", SplitErrand::NeedsTheFamily);
}

// AN EMPTY COLUMN IS NOT AN ERRAND. Answered on its own so that neither shape
// test below it has to think about the empty string, and so that "no errand"
// can never be mistaken for "an errand that runs alone" - which would hand a
// follower `new rpg` with nothing to do with it, and that is the 937-yard
// scatter this module took the strategy off the followers to stop.
void AnEmptyColumnIsNotAnErrand()
{
    CheckErrand("", SplitErrand::Nothing);
    Check("an empty column does not run alone", ErrandRunsAlone(""), false);
}

// THE PREFIXES ARE PREFIXES, NOT SUBSTRINGS. A keyword that merely CONTAINS
// "at:" somewhere is not a point aim, and a role whose name starts with the
// letters of one is not either. This is the reading that decides whether a
// character is allowed to steer itself, so it may not be loose.
void OnlyARealPrefixCounts()
{
    CheckErrand("tabard designer", SplitErrand::SelfContained);
    CheckErrand("stable master", SplitErrand::SelfContained);
    // Not "at:" - the colon is in the wrong place.
    CheckErrand("atrium", SplitErrand::SelfContained);
    CheckErrand("attack:1", SplitErrand::SelfContained);
    // Not "trigger:" either.
    CheckErrand("trigger", SplitErrand::SelfContained);
    CheckErrand("triggerman", SplitErrand::SelfContained);
    // ...but a real one, however short its tail, is.
    CheckErrand("at:", SplitErrand::NeedsTheFamily);
    CheckErrand("trigger:", SplitErrand::NeedsTheFamily);
}

// A MALFORMED POINT IS STILL A POINT, HERE. This decision is about which KIND
// of destination was named, not about whether the string parses: whether
// `at:banana` resolves is ResolveTravelTarget's question and it answers no.
// Reading it as self-contained because it fails to parse would be the one
// direction that hands out the strategy by accident.
void AMalformedPointIsStillAPoint()
{
    CheckErrand("at:banana", SplitErrand::NeedsTheFamily);
    CheckErrand("trigger:not-a-number", SplitErrand::NeedsTheFamily);
}

// EVERY VERDICT HAS ITS OWN NAME, so a log line or a failure message can say
// which one was reached rather than printing a number.
void EveryVerdictHasItsOwnName()
{
    SplitErrand const all[] = {
        SplitErrand::Nothing,
        SplitErrand::NeedsTheFamily,
        SplitErrand::SelfContained,
    };
    for (SplitErrand const one : all)
        for (SplitErrand const other : all)
        {
            char const* a = SplitErrandName(one);
            char const* b = SplitErrandName(other);
            bool const same = std::string(a) == b;
            Check("two verdicts share a name", same, one == other);
        }
}

// THE TWO ENTRY POINTS AGREE, ALWAYS. `ErrandRunsAlone` is the form three gates
// in the module ask, and it may never answer something `ReadSplitErrand` would
// not: a gate that granted where the verdict refuses is a follower steering
// itself for a reason nothing wrote down.
void TheShorthandNeverDisagreesWithTheVerdict()
{
    std::vector<std::string> everything = EveryRole();
    everything.push_back("");
    everything.push_back("3934");
    everything.push_back("at:0:1,2,3");
    everything.push_back("trigger:9");
    everything.push_back("at:");
    everything.push_back("atrium");
    for (std::string const& target : everything)
        Check("shorthand agrees with the verdict", ErrandRunsAlone(target),
              ReadSplitErrand(target) == SplitErrand::SelfContained);
}

}  // namespace

int main()
{
    TheOneErrandThatEndsTheSplitRunsAlone();
    EveryRoleAnErrandCanNameRunsAlone();
    ABareCreatureEntryRunsAlone();
    APointAimStillNeedsTheFamily();
    EvenAWalkablePointAimIsRefused();
    AnEmptyColumnIsNotAnErrand();
    OnlyARealPrefixCounts();
    AMalformedPointIsStillAPoint();
    EveryVerdictHasItsOwnName();
    TheShorthandNeverDisagreesWithTheVerdict();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a cut-off follower can still walk to an innkeeper\n");
    return EXIT_SUCCESS;
}
