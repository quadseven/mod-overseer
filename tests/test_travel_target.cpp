/*
 * Who a travel errand may be sent to (#234).
 *
 * THE FIXTURES ARE REAL ROWS, not invented ones. Every faction template below
 * was read out of the realm's own FactionTemplate.dbc on 2026-09-05 and every
 * creature faction out of acore_world.creature_template on the same day, so a
 * test that passes here is a test against the numbers the worldserver will
 * actually see. A faction table made up to make a predicate fire proves the
 * predicate, not the decision.
 *
 * Compiled without AzerothCore, like every other test here, so the rule stays
 * a pure decision.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ChooseTravelTarget;
using OverseerDecisions::FactionStance;
using OverseerDecisions::FactionStanceFriendlyTo;
using OverseerDecisions::FactionStanceHostileTo;
using OverseerDecisions::FactionStanceReaction;
using OverseerDecisions::MayInteractAt;
using OverseerDecisions::Reaction;
using OverseerDecisions::TravelTargetCandidate;
using OverseerDecisions::TravelTargetChoice;
using OverseerDecisions::TravelTargetExplanation;
using OverseerDecisions::TravelTargetVerdict;

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

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got    '%s'\n  wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

// FactionTemplate.dbc, read from the realm's own data directory 2026-09-05.
// The four numbers after `faction` are factionFlags, ourMask, friendlyMask and
// hostileMask, in the DBC's own order.

// Template 1, the Human player's (ChrRaces.dbc FactionID for race 1). Three of
// the five are human.
FactionStance Human()
{
    return FactionStance{1, 0x0048, 0x0003, 0x0002, 0x000c, {}, {}};
}

// Template 3, the Dwarf player's (race 3). One of the five is a dwarf.
FactionStance Dwarf()
{
    return FactionStance{3, 0x0048, 0x0003, 0x0002, 0x000c, {}, {}};
}

// Template 115, the Gnome player's (race 7). One of the five is a gnome, and
// that template's faction id is 8 rather than 115, which is why a template id
// and a faction id are two numbers and nothing here may treat them as one.
FactionStance Gnome()
{
    return FactionStance{8, 0x0048, 0x0003, 0x0002, 0x000c, {}, {}};
}

// Template 29, faction 76 (Orgrimmar). creature_template.faction for Zargh
// (3489), the butcher the errand kept choosing, and for the Horde town's
// vendors generally.
FactionStance HordeTown()
{
    return FactionStance{76, 0x0000, 0x0004, 0x0004, 0x0002, {}, {}};
}

// Template 85, faction 76. creature_template.faction for Horde Guard (3501),
// the level 40 pair that did the killing, and for Stonetalon Grunt (7730).
FactionStance HordeGuard()
{
    return FactionStance{76, 0x0821, 0x0005, 0x0004, 0x000a, {}, {}};
}

// Template 69, faction 470. creature_template.faction for every one of the
// vendors infra#3359 named in the goblin town: 3658, 3491, 3492, 3493, and the
// innkeeper 6791. Note what is NOT here: no ourMask, no friendlyMask, no
// hostileMask, and a friend list naming only itself. This template is friendly
// to nobody and hostile to nobody.
FactionStance GoblinTown()
{
    return FactionStance{470, 0x0000, 0x0000, 0x0000, 0x0000, {}, {470, 0, 0, 0}};
}

// Template 35, faction 31. The "friendly to everyone" template the world uses
// for spirit healers and for a handful of neutral vendors, among them entry
// 5783, which the live sweep found on the same walk as the goblin town's.
FactionStance FriendlyToAll()
{
    return FactionStance{31, 0x0000, 0x0000, 0x0001, 0x0000, {}, {31, 0, 0, 0}};
}

void TheNearVendorIsHostileToThisFamily()
{
    // The whole of #234 in three lines. Entry 3489 is 15 yards from the aim
    // that was written and the core will not let an Alliance character trade
    // with it at any distance.
    Check("the near town's vendor is hostile to a human",
          FactionStanceHostileTo(HordeTown(), Human()), true);
    Check("...and to a dwarf", FactionStanceHostileTo(HordeTown(), Dwarf()), true);
    Check("...and to a gnome", FactionStanceHostileTo(HordeTown(), Gnome()), true);

    Check("so the reaction is hostile",
          FactionStanceReaction(HordeTown(), Human()) == Reaction::Hostile, true);
    Check("and the interaction gate refuses it",
          MayInteractAt(FactionStanceReaction(HordeTown(), Human())), false);

    // The guards that did the killing read the same way, which is why the
    // route to that vendor is the dangerous half of the errand.
    Check("the guard standing beside it is hostile too",
          FactionStanceHostileTo(HordeGuard(), Human()), true);
}

void TheNeutralGoblinVendorIsUsableAndIsNotFriendly()
{
    // THE POINT OF THE WHOLE FIX. The goblin town's template is friendly to
    // nobody: a rule that preferred friendliness would throw away the only
    // shop this family can reach and leave it with no vendor on the continent
    // at all.
    Check("the goblin town is not hostile to a human",
          FactionStanceHostileTo(GoblinTown(), Human()), false);
    Check("it is not friendly to a human either",
          FactionStanceFriendlyTo(GoblinTown(), Human()), false);
    Check("and the human is not friendly to it",
          FactionStanceFriendlyTo(Human(), GoblinTown()), false);

    Check("so the reaction is neutral",
          FactionStanceReaction(GoblinTown(), Human()) == Reaction::Neutral, true);
    Check("and neutral passes the interaction gate",
          MayInteractAt(FactionStanceReaction(GoblinTown(), Human())), true);

    Check("the same for the dwarf",
          MayInteractAt(FactionStanceReaction(GoblinTown(), Dwarf())), true);
    Check("the same for the gnome",
          MayInteractAt(FactionStanceReaction(GoblinTown(), Gnome())), true);
}

void TheGateIsTheCoresGateAndNotAFriendlinessTest()
{
    // Player.cpp:2146 is `<= REP_UNFRIENDLY`, so neutral is in and unfriendly
    // is out. Pinned rank by rank, because getting this boundary wrong by one
    // is exactly how "prefer friendly" would sneak back in.
    Check("hated is refused", MayInteractAt(Reaction::Hated), false);
    Check("hostile is refused", MayInteractAt(Reaction::Hostile), false);
    Check("unfriendly is refused", MayInteractAt(Reaction::Unfriendly), false);
    Check("neutral is allowed", MayInteractAt(Reaction::Neutral), true);
    Check("friendly is allowed", MayInteractAt(Reaction::Friendly), true);
    Check("exalted is allowed", MayInteractAt(Reaction::Exalted), true);
}

void AFriendlyToAllTemplateIsFriendly()
{
    // Template 35 declares friendlyMask 0x01, which is FACTION_MASK_PLAYER,
    // and no ourMask at all, so it is caught by the first of the two friendly
    // directions. The test exists so those two directions are not quietly
    // collapsed into one later.
    Check("the friendly-to-all template is friendly to a player",
          FactionStanceFriendlyTo(FriendlyToAll(), Human()), true);
    Check("and the reaction is friendly",
          FactionStanceReaction(FriendlyToAll(), Human()) == Reaction::Friendly, true);
}

void ARelationListNamesAFactionAndZeroIsNotOne()
{
    // The DBC pads both lists to four with zeros. A stance whose faction id
    // is zero must not match that padding, or every template with a short
    // list becomes an enemy of everything.
    FactionStance nameless = Human();
    nameless.faction = 0;
    FactionStance padded = GoblinTown();
    padded.enemyFactions = {0, 0, 0, 0};
    padded.friendFactions = {0, 0, 0, 0};
    Check("a zero faction does not match the padding as an enemy",
          FactionStanceHostileTo(padded, nameless), false);
    Check("nor as a friend",
          FactionStanceFriendlyTo(padded, nameless), false);

    // And a real named enemy beats the masks, which is the whole reason the
    // lists are consulted first.
    FactionStance namesTheHuman = GoblinTown();
    namesTheHuman.enemyFactions = {1, 0, 0, 0};
    Check("a named enemy is hostile even with no hostile mask",
          FactionStanceHostileTo(namesTheHuman, Human()), true);

    FactionStance befriendsTheHuman = HordeTown();
    befriendsTheHuman.friendFactions = {1, 0, 0, 0};
    Check("a named friend beats the hostile mask",
          FactionStanceHostileTo(befriendsTheHuman, Human()), false);
}

void HatesAllExceptFriendsIsHostileWithNoMaskAtAll()
{
    FactionStance hates = GoblinTown();
    hates.flags = 0x2000;  // FACTION_TEMPLATE_FLAG_HATES_ALL_EXCEPT_FRIENDS
    Check("hates-all-except-friends is not caught by the masks",
          FactionStanceHostileTo(hates, Human()), false);
    Check("but the flag makes the reaction hostile",
          FactionStanceReaction(hates, Human()) == Reaction::Hostile, true);
}

// The live measurement, 2026-09-05. Distances are from one member at
// (-392.37, -2967.97) on map 1 to the nearest vendor spawns, computed against
// acore_world.creature. The first two are the near town's, the last two are
// the goblin town's.
std::vector<TravelTargetCandidate> TheMapAsItWasMeasured()
{
    return {
        {3443, 143.3f, false},   // faction template 29, the near town
        {3489, 258.9f, false},   // faction template 29, the near town
        {3495, 878.3f, true},    // faction template 69, the goblin town
        {3658, 885.1f, true},    // faction template 69, the goblin town
    };
}

void TheChoiceSkipsTheNearOnesItCannotUse()
{
    TravelTargetChoice const choice = ChooseTravelTarget(TheMapAsItWasMeasured());
    Check("a target was chosen", choice.verdict == TravelTargetVerdict::Chosen, true);
    CheckInt("the chosen one is at 878 yards, not the one at 143", choice.index, 2);
    CheckInt("both near vendors were counted as refused", long(choice.refused), 2);
    CheckInt("four spawns were considered", long(choice.considered), 4);
    CheckInt("the nearest refused one is remembered", choice.nearestRefused, 0);

    CheckText("and the log says what was passed over",
              TravelTargetExplanation(choice, TheMapAsItWasMeasured()),
              "chose entry 3495 at 878 yards over 2 nearer one(s) this character "
              "may not interact with - the nearest of those is entry 3443 at 143 yards");
}

void FarAwayIsNotAReasonToRejectTheOnlyUsableTarget()
{
    // infra#3359 measured the usable town at 1,465 to 1,509 yards from the
    // dungeon door and the useless counter at 510. The correct answer being
    // three times farther is not a tie-break, it is the answer.
    std::vector<TravelTargetCandidate> const fromTheDoor = {
        {3489, 510.0f, false},
        {3658, 1465.0f, true},
        {3491, 1509.0f, true},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(fromTheDoor);
    Check("a target was chosen", choice.verdict == TravelTargetVerdict::Chosen, true);
    CheckInt("the far usable one wins over the near useless one", choice.index, 1);
}

void NothingUsableIsARefusalThatNamesItsCause()
{
    // Part D of #234: an errand with no usable target should refuse rather
    // than walk. Today it walked, and the walking is what killed people.
    std::vector<TravelTargetCandidate> const theNearTownOnly = {
        {3489, 15.0f, false},
        {3443, 41.0f, false},
        {3480, 56.0f, false},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(theNearTownOnly);
    Check("no target is chosen",
          choice.verdict == TravelTargetVerdict::NoneWillDealWithUs, true);
    CheckInt("and nothing is named as the answer", choice.index, -1);
    CheckText("the refusal names its cause",
              TravelTargetExplanation(choice, theNearTownOnly),
              "3 of them are on this map and this character may interact with none of "
              "them - the nearest is entry 3489 at 15 yards");
}

void AnEmptyMapIsNotTheSameAnswerAsAHostileOne()
{
    // "There is no vendor here" and "there are vendors here and none of them
    // will serve you" are different facts and lead to different fixes, so
    // they are different verdicts rather than one false.
    TravelTargetChoice const choice = ChooseTravelTarget({});
    Check("an empty list is nothing of that kind",
          choice.verdict == TravelTargetVerdict::NothingOfThatKind, true);
    CheckInt("nothing was considered", long(choice.considered), 0);
    CheckText("and there is nothing extra to say",
              TravelTargetExplanation(choice, {}), "");
}

void AnOrdinaryErrandSaysNothingExtra()
{
    // The nearest spawn is usable, so the gate changed nothing and the log
    // should not imply a near miss. A refused spawn FARTHER away than the
    // chosen one is not a near miss either.
    std::vector<TravelTargetCandidate> const ordinary = {
        {3658, 40.0f, true},
        {3489, 900.0f, false},
    };
    TravelTargetChoice const choice = ChooseTravelTarget(ordinary);
    CheckInt("the nearest usable one is chosen", choice.index, 0);
    CheckInt("the farther unusable one is still counted", long(choice.refused), 1);
    CheckText("but nothing is said about it",
              TravelTargetExplanation(choice, ordinary), "");
}

void ATieGoesToTheLowerIndex()
{
    // Two vendors five yards apart is the ordinary town square, so the answer
    // must not depend on the order a spawn sweep produced.
    std::vector<TravelTargetCandidate> const together = {
        {3491, 12.0f, true},
        {3492, 12.0f, true},
        {3493, 12.0f, true},
    };
    CheckInt("the first of three equals wins", ChooseTravelTarget(together).index, 0);

    std::vector<TravelTargetCandidate> const refusedTogether = {
        {3489, 12.0f, false},
        {3443, 12.0f, false},
    };
    CheckInt("and the same for the nearest refused",
             ChooseTravelTarget(refusedTogether).nearestRefused, 0);
}

}  // namespace

int main()
{
    TheNearVendorIsHostileToThisFamily();
    TheNeutralGoblinVendorIsUsableAndIsNotFriendly();
    TheGateIsTheCoresGateAndNotAFriendlinessTest();
    AFriendlyToAllTemplateIsFriendly();
    ARelationListNamesAFactionAndZeroIsNotOne();
    HatesAllExceptFriendsIsHostileWithNoMaskAtAll();
    TheChoiceSkipsTheNearOnesItCannotUse();
    FarAwayIsNotAReasonToRejectTheOnlyUsableTarget();
    NothingUsableIsARefusalThatNamesItsCause();
    AnEmptyMapIsNotTheSameAnswerAsAHostileOne();
    AnOrdinaryErrandSaysNothingExtra();
    ATieGoesToTheLowerIndex();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a travel errand goes to an npc this character can actually use\n");
    return EXIT_SUCCESS;
}
