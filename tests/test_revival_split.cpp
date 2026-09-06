/*
 * Whether a revival may put a character on a map its party is not on.
 *
 * The live failure this pins is a party cut in half. On the dev realm
 * 2026-09-05 the LEADER died twice at one Barrens graveyard inside the repeat
 * window, took the bind-point escalation, and arrived in Duskwood while the
 * other four stayed on Kalimdor:
 *
 *   17:34:31  'Grug' resurrected at the nearest graveyard
 *   17:35:43  'Grug' twice at one graveyard inside 300s means it cannot live
 *             there, so it was sent to its own bind point instead
 *   17:47     Grug map 0 Duskwood; Bork, Grog, Og and Ugga all map 1
 *
 * Nothing in the module can undo that: `follow` cannot cross a map, the
 * catch-up walk refuses a cross-map gap and returns silently, and an `at:` aim
 * cannot name a coordinate on another map. Eleven minutes of four followers
 * standing still with no log line, and an operator moving them back by hand.
 *
 * These cases pin the narrowing that fix uses. #188 could close its ladder
 * under "never change maps" outright, because the character there was alive
 * and doing nothing was always available. A revival has no such option: one of
 * these four exits fires because the map has no graveyard AT ALL, which is
 * every death inside an instance, and refusing to move there would strand a
 * corpse. So the rule is about the PARTY'S map, not about moving.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::RevivalMayCrossMaps;
using OverseerDecisions::RevivalMove;
using OverseerDecisions::RevivalMoveVerdict;

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

void CheckMap(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got map %u, wanted map %u\n", what, got, want);
    ++failures;
}

// The live shape: the character is on Kalimdor, every roster bind row is map 0,
// and the other four are on Kalimdor with it.
RevivalMove OnKalimdorWithTheFamily(bool graveyardHere = true)
{
    RevivalMove move;
    move.bindMapId = 0;
    move.partyMapIds = {1, 1, 1, 1};
    move.graveyardOnThisMap = graveyardHere;
    return move;
}

// THE INCIDENT. The leader is about to be sent to its own bind on map 0 while
// the four it leads are on map 1, and the Barrens has graveyards.
void TheLeaderIsNotSentOffTheMapItsFamilyIsOn()
{
    RevivalMoveVerdict const v = RevivalMayCrossMaps(OnKalimdorWithTheFamily());
    Check("a bind on another map is refused when this map has a graveyard",
          v.mayMove, false);
    Check("and refusing is not the same as splitting", v.splitsParty, false);
    Check("the party map was established", v.partyMapKnown, true);
    CheckMap("and it is Kalimdor", v.partyMapId, 1);
}

// THE CASE THAT MUST KEEP ITS TELEPORT. `game_graveyard` holds zero rows for
// map 36, so a character that dies in the Deadmines has nowhere on its own map
// to be revived at. Refusing here would restore the regression the branch was
// written for: a body lay there for 29 minutes while the drive ran.
void ADeathInsideAnInstanceStillGetsOutAndSaysWhatItCost()
{
    RevivalMove move;
    move.bindMapId = 0;
    move.partyMapIds = {36, 36, 36, 36};   // the rest of the party is inside
    move.graveyardOnThisMap = false;
    RevivalMoveVerdict const v = RevivalMayCrossMaps(move);
    Check("nothing on this map means the bind is the only answer", v.mayMove, true);
    Check("and it is loud about leaving the party behind", v.splitsParty, true);
    CheckMap("the party it left is on the instance map", v.partyMapId, 36);
}

// A party that binds where it plays is not split by a bind teleport, so
// nothing about this rule may get in the way of the existing escape.
void ABindOnThePartysOwnMapIsStillTaken()
{
    RevivalMove move;
    move.bindMapId = 0;
    move.partyMapIds = {0, 0, 0, 0};
    move.graveyardOnThisMap = true;
    RevivalMoveVerdict const v = RevivalMayCrossMaps(move);
    Check("the death-trap escape still works at home", v.mayMove, true);
    Check("and splits nothing", v.splitsParty, false);
}

// UNKNOWN IS NOT AGREEMENT, and it is not refusal either. A character with no
// group, or one whose groupmates are all offline, has no party map, and an
// unestablished party map may not be used to refuse a revival.
void AnUnknownPartyMapRefusesNothing()
{
    RevivalMove alone;
    alone.bindMapId = 0;
    alone.graveyardOnThisMap = true;   // partyMapIds deliberately empty
    RevivalMoveVerdict const v = RevivalMayCrossMaps(alone);
    Check("an ungrouped character may still be sent home", v.mayMove, true);
    Check("it cannot split a party it does not have", v.splitsParty, false);
    Check("and the rule says it does not know", v.partyMapKnown, false);
    CheckMap("rather than guessing map 0", v.partyMapId, 0);

    // The same, but with nobody resolvable. Still unknown, still permitted.
    RevivalMove offline = alone;
    offline.partyMapIds.clear();
    Check("a group whose members are all out of the world reads the same",
          RevivalMayCrossMaps(offline).mayMove, true);
}

// THE PARTY'S MAP IS THE MAJORITY OF THE OTHERS, which is the reading that
// works whichever member is the one dying. Asking for the leader's map would
// get the wrong answer in exactly the case that caused #241.
void ThePartyMapIsWhereMostOfTheOthersAre()
{
    RevivalMove mostlyKalimdor;
    mostlyKalimdor.bindMapId = 0;
    mostlyKalimdor.partyMapIds = {0, 1, 1, 1};   // one already stranded at home
    mostlyKalimdor.graveyardOnThisMap = true;
    RevivalMoveVerdict const v = RevivalMayCrossMaps(mostlyKalimdor);
    CheckMap("three on Kalimdor outvote one on the eastern map", v.partyMapId, 1);
    Check("so the bind is still refused", v.mayMove, false);

    // And the other way round: once most of the family IS at the bind map,
    // going there rejoins them rather than splitting them.
    RevivalMove mostlyHome = mostlyKalimdor;
    mostlyHome.partyMapIds = {0, 0, 0, 1};
    RevivalMoveVerdict const home = RevivalMayCrossMaps(mostlyHome);
    CheckMap("the majority is now the bind map", home.partyMapId, 0);
    Check("and the teleport is allowed again", home.mayMove, true);
}

// A tie has to answer the same way every time it is asked, or the drive's
// behaviour depends on iteration order.
void ATieIsStableRatherThanArbitrary()
{
    RevivalMove split;
    split.bindMapId = 530;   // somewhere neither half is
    split.partyMapIds = {1, 1, 0, 0};
    split.graveyardOnThisMap = true;
    uint32_t const first = RevivalMayCrossMaps(split).partyMapId;
    CheckMap("a tie keeps the map seen first", first, 1);
    for (int i = 0; i < 50; ++i)
        CheckMap("and keeps keeping it", RevivalMayCrossMaps(split).partyMapId, first);
}

// A single companion is a party map too. The rule must not need a quorum.
void OneGroupmateIsEnoughToEstablishAMap()
{
    RevivalMove pair;
    pair.bindMapId = 0;
    pair.partyMapIds = {1};
    pair.graveyardOnThisMap = true;
    RevivalMoveVerdict const v = RevivalMayCrossMaps(pair);
    Check("one companion establishes the map", v.partyMapKnown, true);
    CheckMap("which is that companion's", v.partyMapId, 1);
    Check("and the bind is refused for it", v.mayMove, false);
}

// THE INVARIANT, STATED AS ONE SENTENCE. Over every shape this rule can be
// handed, a move is only ever allowed to leave the party behind when there was
// nowhere on the character's own map to put it. `splitsParty` is exactly that
// case and never any other.
void APartyIsOnlyEverSplitWhenThereWasNoAlternative()
{
    uint32_t const maps[] = {0, 1, 36, 530};
    for (uint32_t bind : maps)
        for (uint32_t a : maps)
            for (uint32_t b : maps)
                for (int graveyard = 0; graveyard < 2; ++graveyard)
                {
                    RevivalMove move;
                    move.bindMapId = bind;
                    move.partyMapIds = {a, b};
                    move.graveyardOnThisMap = graveyard != 0;
                    RevivalMoveVerdict const v = RevivalMayCrossMaps(move);

                    Check("a split is always a move",
                          !v.splitsParty || v.mayMove, true);
                    Check("a split never happens with a graveyard to hand",
                          !v.splitsParty || !move.graveyardOnThisMap, true);
                    Check("a split never happens onto the party's own map",
                          !v.splitsParty || v.partyMapId != bind, true);
                    Check("a move onto another map with an alternative is refused",
                          !(v.mayMove && move.graveyardOnThisMap &&
                            v.partyMapKnown && v.partyMapId != bind),
                          true);
                    Check("a move that lands on the party's map is never a split",
                          !(v.partyMapKnown && v.partyMapId == bind && v.splitsParty),
                          true);
                }
}

}  // namespace

int main()
{
    TheLeaderIsNotSentOffTheMapItsFamilyIsOn();
    ADeathInsideAnInstanceStillGetsOutAndSaysWhatItCost();
    ABindOnThePartysOwnMapIsStillTaken();
    AnUnknownPartyMapRefusesNothing();
    ThePartyMapIsWhereMostOfTheOthersAre();
    ATieIsStableRatherThanArbitrary();
    OneGroupmateIsEnoughToEstablishAMap();
    APartyIsOnlyEverSplitWhenThereWasNoAlternative();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a revival does not put a character where its family cannot follow\n");
    return EXIT_SUCCESS;
}
