/*
 * Binding a home, and the read-back that decides whether one moved.
 *
 * mod-overseer#286. Every member of the family reads map 0 in
 * character_homebind, at the inn its race was born beside, because nothing in
 * this module or upstream could ever change a home for a character with no
 * client for the answer to be shown to. Upstream's `home` command
 * returns false on its second line for a masterless bot and the row still
 * reads `delivered`, which is the exact shape AGENTS.md warns about: a call
 * that reports its failure to a client, to a character that has no client.
 *
 * So the rule this file pins is not "the packet was sent". It is that a bind
 * is judged by comparing the home before, the home after, and the place the
 * character was standing - and that "the home did not move" splits into good
 * news and bad news depending on whether it was already here.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::BindOutcome;
using OverseerDecisions::BindOutcomeWord;
using OverseerDecisions::BindReadBack;
using OverseerDecisions::BindRefusalRetry;
using OverseerDecisions::BindRequest;
using OverseerDecisions::BindVerb;
using OverseerDecisions::ChooseInnkeeper;
using OverseerDecisions::HomeBind;
using OverseerDecisions::ParseBindRequest;
using OverseerDecisions::TownRetryWord;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckOutcome(char const* what, BindOutcome got, BindOutcome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, BindOutcomeWord(got),
                BindOutcomeWord(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

// The tolerance mod_overseer.cpp passes in, named here so a failure reads as
// the rule rather than as a number.
constexpr float SAME_SPOT = 10.f;

// The two binds every member of the family actually carries, read from
// character_homebind: three at the human start and two at the dwarf one, both
// on map 0 and 4,400 yards apart. They are here because they are why this verb
// exists, and because "send everybody home" against these two is the scatter
// this module must never call a reunion.
HomeBind HumanStart()
{
    HomeBind home;
    home.known = true;
    home.mapId = 0;
    home.areaId = 12;
    home.x = -8949.95f;
    home.y = -132.493f;
    home.z = 83.5312f;
    return home;
}

HomeBind DwarfStart()
{
    HomeBind home;
    home.known = true;
    home.mapId = 0;
    home.areaId = 1;
    home.x = -6240.32f;
    home.y = 331.033f;
    home.z = 382.758f;
    return home;
}

// The nearest innkeeper to the dungeon this family is meant to run that an
// Alliance character may actually interact with. The one at the crossroads
// nearby belongs to the other faction, and GetNPCIfCanInteractWith turns that
// one down however close the character stands.
HomeBind NeutralPortTown()
{
    HomeBind home;
    home.known = true;
    home.mapId = 1;
    home.areaId = 17;
    home.x = -1050.04f;
    home.y = -3664.80f;
    home.z = 23.97f;
    return home;
}

void TheOnlyFormIsHereAndNothingAtAllMeansIt()
{
    Check("an empty command is a bind here", ParseBindRequest("").verb == BindVerb::Here, true);
    Check("blanks alone are a bind here", ParseBindRequest("   \t ").verb == BindVerb::Here, true);
    Check("the word here is a bind here", ParseBindRequest("here").verb == BindVerb::Here, true);
    Check("surrounding blanks are tolerated",
          ParseBindRequest("  here  ").verb == BindVerb::Here, true);

    BindRequest const withArgument = ParseBindRequest("here 1234");
    Check("here takes no arguments", withArgument.verb == BindVerb::None, true);
    CheckWord("the argument refusal is the literal the table keys on",
              withArgument.error.c_str(), "malformed bind: here takes no arguments");

    BindRequest const unknown = ParseBindRequest("stormwind");
    Check("a place name is not a bind request", unknown.verb == BindVerb::None, true);
    CheckWord("the unknown-verb refusal is the literal the table keys on",
              unknown.error.c_str(),
              "malformed bind: unknown verb (want here, or nothing at all)");
}

void AHomeThatMovedIsTheOnlyChange()
{
    HomeBind const standing = NeutralPortTown();
    CheckOutcome("a home that crossed a map moved",
                 BindReadBack(HumanStart(), standing, standing, SAME_SPOT), BindOutcome::Moved);

    HomeBind after = HumanStart();
    after.x += 40.f;
    CheckOutcome("a home that moved across one town moved",
                 BindReadBack(HumanStart(), after, after, SAME_SPOT), BindOutcome::Moved);

    HomeBind upstairs = HumanStart();
    upstairs.z += 25.f;
    CheckOutcome("an inn has floors, so height alone is a move",
                 BindReadBack(HumanStart(), upstairs, upstairs, SAME_SPOT), BindOutcome::Moved);
}

void AHomeThatDidNotMoveSplitsTwoWays()
{
    // Standing at the inn the character was already bound at. Nothing needed
    // to happen, so nothing happening is the right answer and not a failure.
    HomeBind standing = HumanStart();
    standing.x += 3.f;
    CheckOutcome("already bound here is not a failure",
                 BindReadBack(HumanStart(), HumanStart(), standing, SAME_SPOT),
                 BindOutcome::SameSpot);

    // THE ONE THIS EXECUTOR EXISTS FOR. The packet went out beside an
    // innkeeper on another continent and the home is still where it was born.
    CheckOutcome("a home still on the other continent is the failure, not a success",
                 BindReadBack(HumanStart(), HumanStart(), NeutralPortTown(), SAME_SPOT),
                 BindOutcome::Unchanged);

    // Same map, far away: still nothing happened.
    CheckOutcome("a home unchanged while standing at the other family bind is a failure",
                 BindReadBack(HumanStart(), HumanStart(), DwarfStart(), SAME_SPOT),
                 BindOutcome::Unchanged);
}

void AReadingNobodyTookIsNeitherOutcome()
{
    HomeBind const unread;
    Check("an unread home defaults to unknown rather than to map zero", unread.known, false);

    CheckOutcome("no home before is unreadable",
                 BindReadBack(unread, HumanStart(), HumanStart(), SAME_SPOT),
                 BindOutcome::Unreadable);
    CheckOutcome("no home after is unreadable",
                 BindReadBack(HumanStart(), unread, HumanStart(), SAME_SPOT),
                 BindOutcome::Unreadable);
    CheckOutcome("nowhere standing is unreadable",
                 BindReadBack(HumanStart(), HumanStart(), unread, SAME_SPOT),
                 BindOutcome::Unreadable);
}

void TheNearestInnkeeperWins()
{
    Check("no innkeeper in reach answers nobody", ChooseInnkeeper({}) == -1, true);
    Check("one innkeeper is the one", ChooseInnkeeper({4.1f}) == 0, true);
    Check("the nearest of three wins", ChooseInnkeeper({4.1f, 2.0f, 5.4f}) == 1, true);
    Check("a tie breaks on the first, not on the sweep order",
          ChooseInnkeeper({3.0f, 3.0f}) == 0, true);
}

void EveryRefusalCarriesWhereToTryAgain()
{
    CheckWord("a malformed line is never retried",
              TownRetryWord(BindRefusalRetry("malformed bind: here takes no arguments")),
              "never");
    CheckWord("an unknown verb is never retried",
              TownRetryWord(
                  BindRefusalRetry("malformed bind: unknown verb (want here, or nothing at all)")),
              "never");
    CheckWord("no innkeeper in reach is answered somewhere else",
              TownRetryWord(BindRefusalRetry("innkeeper not in range")), "elsewhere");
    CheckWord("an instance is answered somewhere else",
              TownRetryWord(BindRefusalRetry("character is inside an instance")), "elsewhere");
    CheckWord("being dead is answered here, in a moment",
              TownRetryWord(BindRefusalRetry("character is dead")), "later");
    CheckWord("a wall this table has never heard of is a transient until proven otherwise",
              TownRetryWord(BindRefusalRetry("something nobody has written down yet")), "later");
}

void TheOutcomeWordsAreTheOnesARowCarries()
{
    CheckWord("moved", BindOutcomeWord(BindOutcome::Moved), "moved");
    CheckWord("same spot", BindOutcomeWord(BindOutcome::SameSpot), "same-spot");
    CheckWord("unchanged", BindOutcomeWord(BindOutcome::Unchanged), "unchanged");
    CheckWord("unreadable", BindOutcomeWord(BindOutcome::Unreadable), "unreadable");
}

}  // namespace

int main()
{
    TheOnlyFormIsHereAndNothingAtAllMeansIt();
    AHomeThatMovedIsTheOnlyChange();
    AHomeThatDidNotMoveSplitsTwoWays();
    AReadingNobodyTookIsNeitherOutcome();
    TheNearestInnkeeperWins();
    EveryRefusalCarriesWhereToTryAgain();
    TheOutcomeWordsAreTheOnesARowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a bind is judged by the home that came back, not by the packet\n");
    return EXIT_SUCCESS;
}
