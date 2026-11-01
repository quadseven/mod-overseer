/*
 * A family mage's portal or teleport to a capital, instead of the walk (#1161).
 *
 * WHERE THE FIXTURE ROWS COME FROM. Nothing here is remembered from a wiki.
 *   - Which spells each mage knows: the read-only read of the dev realm's
 *     character spell list quoted on #1161 (2026-10-26). Og knows 3561, 3562,
 *     10059 and 11416; Oz knows 3567, 3563 and 3566.
 *   - Where each spell lands: spell_target_position in the pinned core
 *     (data/sql/base/db_world/spell_target_position.sql at realm-pins.env's
 *     CORE_SHA). A portal's landing is the row of the spell its object casts:
 *     gameobject_template gives 176296 data0 17334 and 176497 data0 44089, and
 *     those two rows land on the same points as teleports 3561 and 3562.
 *   - Which ids exist to be known: npc_trainer in the same tree, whose rows
 *     teach exactly these ids at levels 20 and 40.
 *   - Rune entries: item_template in the same tree gives 17031 Rune of
 *     Teleportation and 17032 Rune of Portals. Which spell burns which rune is
 *     Spell.dbc, which that tree does not carry, so the fixture pairs them by
 *     the item's name; the adapter reads the pair off SpellInfo.
 * The adapter feeds these same rows to MageTravelSpellsFrom at runtime, so these
 * ids are written down only here, where a test pins them.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::DecideMageTravel;
using OverseerDecisions::MAGE_TRAVEL_CAST_TRIES;
using OverseerDecisions::MAGE_TRAVEL_LANDING_REACH_YARDS;
using OverseerDecisions::MAGE_TRAVEL_MIN_SAVED_YARDS;
using OverseerDecisions::MageTravelCalledOff;
using OverseerDecisions::MageTravelCastRetries;
using OverseerDecisions::MageKnownSpell;
using OverseerDecisions::MageSpellLanding;
using OverseerDecisions::MageTravelClicker;
using OverseerDecisions::MageTravelFacts;
using OverseerDecisions::MageTravelSpell;
using OverseerDecisions::MageTravelSpellsFrom;
using OverseerDecisions::MageTravelVerdict;
using OverseerDecisions::MageTravelWay;
using OverseerDecisions::MageTravelWayWord;
using OverseerDecisions::NextPortalClickers;

namespace CastRefusal = OverseerDecisions::CastRefusal;
namespace Refusal = OverseerDecisions::MageTravelRefusal;

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

void CheckVerdict(char const* what, MageTravelVerdict const& got, MageTravelWay way,
                  uint32_t spellId, char const* reason)
{
    if (got.way == way && got.spellId == spellId && std::string(got.reason) == reason)
        return;
    std::printf("FAIL %s: got %s spell %u reason '%s', wanted %s spell %u reason '%s'\n", what,
                MageTravelWayWord(got.way), got.spellId, got.reason, MageTravelWayWord(way),
                spellId, reason);
    ++failures;
}

// item_template's own entries. Each rune is counted on its own, so a bag of one
// kind must not pay for a spell that burns the other.
constexpr uint32_t RUNE_OF_TELEPORTATION = 17031;
constexpr uint32_t RUNE_OF_PORTALS = 17032;

MageTravelSpell Spell(uint32_t id, bool portal, uint32_t map, float x, float y,
                      uint32_t carried = 1)
{
    MageTravelSpell s;
    s.spellId = id;
    s.makesPortal = portal;
    s.map = map;
    s.x = x;
    s.y = y;
    s.reagentEntry = portal ? RUNE_OF_PORTALS : RUNE_OF_TELEPORTATION;
    s.reagentNeeded = 1;
    s.reagentCarried = carried;
    return s;
}

// The landings are spell_target_position's own, cut to the fields decided on.
std::vector<MageTravelSpell> OgSpells(uint32_t runes = 1)
{
    return {Spell(3561, false, 0, -9003.46f, 870.031f, runes),
            Spell(3562, false, 0, -4613.62f, -915.38f, runes),
            Spell(10059, true, 0, -9003.46f, 870.031f, runes),
            Spell(11416, true, 0, -4613.62f, -915.38f, runes)};
}

std::vector<MageTravelSpell> OzSpells()
{
    return {Spell(3567, false, 1, 1469.85f, -4221.52f),
            Spell(3563, false, 0, 1773.47f, 61.121f),
            Spell(3566, false, 1, -964.98f, 283.433f)};
}

// Bound for the Stormwind trade district, from Kalimdor.
MageTravelFacts ToStormwind(std::vector<MageTravelSpell> spells)
{
    MageTravelFacts f;
    f.casterIsTraveller = true;
    f.sameMap = false;
    f.destMap = 0;
    f.destX = -8900.f;
    f.destY = 800.f;
    f.spells = std::move(spells);
    return f;
}

// spell_target_position in the pinned core, cut to the fields read, with the
// rows of the two portal objects' spells and of spells that must not be taken.
std::vector<MageSpellLanding> LandingRows()
{
    return {{3561, 0, -9003.46f, 870.031f},   {3562, 0, -4613.62f, -915.38f},
            {3563, 0, 1773.47f, 61.121f},     {3566, 1, -964.98f, 283.433f},
            {3567, 1, 1469.85f, -4221.52f},   {17334, 0, -9003.46f, 870.031f},
            {44089, 0, -4613.62f, -915.38f},  {18960, 1, 7991.88f, -2679.84f},
            {33690, 530, -1800.75f, 5408.09f}, {53140, 571, 5807.75f, 588.347f}};
}

MageKnownSpell Known(uint32_t id, uint32_t portalObjectSpell = 0, bool mageFamily = true)
{
    MageKnownSpell k;
    k.spellId = id;
    k.mageFamily = mageFamily;
    k.portalObjectSpell = portalObjectSpell;
    k.reagentEntry = portalObjectSpell ? RUNE_OF_PORTALS : RUNE_OF_TELEPORTATION;
    k.reagentNeeded = 1;
    k.reagentCarried = 1;
    return k;
}

std::vector<uint32_t> Ids(std::vector<MageTravelSpell> const& spells)
{
    std::vector<uint32_t> ids;
    for (MageTravelSpell const& spell : spells)
        ids.push_back(spell.spellId);
    return ids;
}

void TestTheIdsAreTheOnesTheWorldGives()
{
    // Og's list on the read quoted above, with Frostbolt (116), which moves
    // nobody, and the Druid's Teleport: Moonglade (18960), which is no mage's.
    std::vector<MageTravelSpell> const og = MageTravelSpellsFrom(
        {Known(116), Known(3561), Known(3562), Known(10059, 17334), Known(11416, 44089),
         Known(18960, 0, false)},
        LandingRows());
    Check("Og's ways are these four ids and no others",
          Ids(og) == std::vector<uint32_t>({3561, 3562, 10059, 11416}));
    Check("the teleports are teleports", og.size() == 4 && !og[0].makesPortal && !og[1].makesPortal);
    Check("the portals are portals", og.size() == 4 && og[2].makesPortal && og[3].makesPortal);
    Check("a portal lands where its object's spell lands",
          og.size() == 4 && og[2].map == 0 && og[2].x == -9003.46f && og[3].y == -915.38f);
    Check("a portal keeps its own rune", og.size() == 4 && og[2].reagentEntry == RUNE_OF_PORTALS);

    std::vector<MageTravelSpell> const oz =
        MageTravelSpellsFrom({Known(3567), Known(3563), Known(3566)}, LandingRows());
    Check("Oz's ways are these three ids", Ids(oz) == std::vector<uint32_t>({3567, 3563, 3566}));
    Check("a teleport lands on its own row", oz.size() == 3 && oz[0].map == 1 && oz[1].map == 0);
}

void TestALandingPastTheClassicWorldIsNoWay()
{
    Check("Shattrath and Dalaran are not ways to a capital here",
          MageTravelSpellsFrom({Known(33690), Known(53140)}, LandingRows()).empty());
}

void TestAPortalWhoseObjectLandsNowhereIsNoWay()
{
    Check("an object spell with no row is no way",
          MageTravelSpellsFrom({Known(10059, 999999)}, LandingRows()).empty());
}

void TestTheWorldsIdsDriveTheChoice()
{
    MageTravelFacts f = ToStormwind(MageTravelSpellsFrom(
        {Known(3561), Known(10059, 17334), Known(3562), Known(11416, 44089)}, LandingRows()));
    CheckVerdict("a lone mage teleports on the id the world gave", DecideMageTravel(f),
                 MageTravelWay::Teleport, 3561, "");
    f.othersPresent = 1;
    CheckVerdict("a party takes the portal the world gave", DecideMageTravel(f),
                 MageTravelWay::Portal, 10059, "");
}

void TestTheMageAloneTeleports()
{
    CheckVerdict("a lone mage crossing a continent teleports",
                 DecideMageTravel(ToStormwind(OgSpells())), MageTravelWay::Teleport, 3561, "");
}

void TestThePartyTakesAPortal()
{
    MageTravelFacts f = ToStormwind(OgSpells());
    f.othersPresent = 3;
    CheckVerdict("a mage with its party beside it opens the portal", DecideMageTravel(f),
                 MageTravelWay::Portal, 10059, "");

    f.casterIsTraveller = false;
    f.othersPresent = 1;
    CheckVerdict("a mage opens it for the leader who was sent", DecideMageTravel(f),
                 MageTravelWay::Portal, 10059, "");
}

void TestTheRightCapitalIsPicked()
{
    MageTravelFacts f = ToStormwind(OgSpells());
    f.destX = -4800.f;
    f.destY = -1100.f;
    CheckVerdict("Ironforge takes Ironforge's teleport, not the first one listed",
                 DecideMageTravel(f), MageTravelWay::Teleport, 3562, "");
}

void TestNoRuneOfPortalsIsANamedRefusal()
{
    MageTravelFacts f = ToStormwind(OgSpells(0));
    f.othersPresent = 2;
    CheckVerdict("a missing Rune of Portals is refused by name", DecideMageTravel(f),
                 MageTravelWay::Walk, 10059, Refusal::NoRuneOfPortals);
}

void TestNoRuneForATeleportIsANamedRefusal()
{
    CheckVerdict("a teleport short of its reagent is refused by name",
                 DecideMageTravel(ToStormwind(OgSpells(0))), MageTravelWay::Walk, 3561,
                 Refusal::NoRuneOfTeleportation);
}

void TestTheOtherRuneDoesNotPayForAPortal()
{
    std::vector<MageTravelSpell> spells = OgSpells(0);
    spells[0].reagentCarried = 3;
    spells[1].reagentCarried = 3;
    MageTravelFacts f = ToStormwind(spells);
    f.othersPresent = 2;
    CheckVerdict("Runes of Teleportation do not open a portal", DecideMageTravel(f),
                 MageTravelWay::Walk, 10059, Refusal::NoRuneOfPortals);
}

void TestATeleportNeedsNoReagentOnASpellThatTakesNone()
{
    std::vector<MageTravelSpell> spells = OgSpells(0);
    spells[0].reagentEntry = 0;
    spells[0].reagentNeeded = 0;
    CheckVerdict("a spell with no reagent asks for none",
                 DecideMageTravel(ToStormwind(spells)), MageTravelWay::Teleport, 3561, "");
}

void TestAnUnknownSpellWalks()
{
    MageTravelFacts f = ToStormwind(OzSpells());
    CheckVerdict("a mage with nothing for that capital walks, and says so",
                 DecideMageTravel(f), MageTravelWay::Walk, 0, Refusal::NoSpellForThatPlace);

    // Oz at level 37: the teleport is known, the portal trains at 40. Taking
    // the teleport would leave the party behind.
    f.destMap = 1;
    f.destX = 1600.f;
    f.destY = -4400.f;
    f.othersPresent = 2;
    CheckVerdict("a known teleport does not stand in for an unknown portal",
                 DecideMageTravel(f), MageTravelWay::Walk, 0, Refusal::NoPortalForTheParty);
}

void TestAShortWalkWalksSilently()
{
    MageTravelFacts f = ToStormwind(OgSpells());
    f.sameMap = true;
    f.walkYards = MAGE_TRAVEL_MIN_SAVED_YARDS - 1.f;
    CheckVerdict("a short walk is no refusal at all", DecideMageTravel(f), MageTravelWay::Walk, 0,
                 "");
}

void TestTheSavingIsWhatCounts()
{
    // The destination is about 120 yards from the landing.
    MageTravelFacts f = ToStormwind(OgSpells());
    f.sameMap = true;
    f.walkYards = MAGE_TRAVEL_MIN_SAVED_YARDS + 50.f;
    CheckVerdict("a spell that saves too little is not cast", DecideMageTravel(f),
                 MageTravelWay::Walk, 0, "");

    f.walkYards = MAGE_TRAVEL_MIN_SAVED_YARDS + 200.f;
    CheckVerdict("a long enough walk on one continent is cast", DecideMageTravel(f),
                 MageTravelWay::Teleport, 3561, "");
}

void TestALandingOutOfReachIsNotThatCapital()
{
    MageTravelFacts f = ToStormwind(OgSpells());
    f.destX = -9003.46f - MAGE_TRAVEL_LANDING_REACH_YARDS - 10.f;
    f.destY = 870.031f;
    CheckVerdict("somewhere past the capital is not the capital", DecideMageTravel(f),
                 MageTravelWay::Walk, 0, Refusal::NoSpellForThatPlace);

    // Same coordinates on the other continent: the two share a coordinate
    // space, and a comparison that forgot the map would call this a capital.
    MageTravelFacts other = ToStormwind(OgSpells());
    other.destMap = 1;
    CheckVerdict("a capital's coordinates on another map are not the capital",
                 DecideMageTravel(other), MageTravelWay::Walk, 0, Refusal::NoSpellForThatPlace);
}

void TestWayWords()
{
    Check("walk word", std::string(MageTravelWayWord(MageTravelWay::Walk)) == "walk");
    Check("teleport word", std::string(MageTravelWayWord(MageTravelWay::Teleport)) == "teleport");
    Check("portal word", std::string(MageTravelWayWord(MageTravelWay::Portal)) == "portal");
}

void TestOnlyAWaitableRefusalIsRetried()
{
    Check("a moving mage is asked again", MageTravelCastRetries(CastRefusal::Moving, 1));
    Check("a mage in combat is asked again", MageTravelCastRetries(CastRefusal::InCombat, 1));
    Check("the asking is bounded",
          !MageTravelCastRetries(CastRefusal::Moving, MAGE_TRAVEL_CAST_TRIES));
    Check("a missing reagent is not waited out",
          !MageTravelCastRetries(CastRefusal::MissingReagent, 1));
    Check("an unlearned spell is not waited out",
          !MageTravelCastRetries(CastRefusal::NotLearned, 1));
}

void TestAnErrandCalledOffEndsTheRun()
{
    Check("the same errand runs on", !MageTravelCalledOff(false, true, false, true));
    Check("an emptied column ends it", MageTravelCalledOff(false, false, false, false));
    Check("a different errand ends it", MageTravelCalledOff(false, true, false, false));
    Check("an operator's guard ends it", MageTravelCalledOff(false, true, true, false));
    Check("a traveller already sent through has nothing to call off",
          !MageTravelCalledOff(true, false, false, false));
}

MageTravelClicker Clicker(bool traveller, bool mage, bool waiting, bool underWay = false)
{
    MageTravelClicker c;
    c.traveller = traveller;
    c.mage = mage;
    c.waiting = waiting;
    c.underWay = underWay;
    return c;
}

bool Same(std::vector<std::size_t> const& got, std::vector<std::size_t> const& want)
{
    return got == want;
}

void TestTheTravellerStepsThroughFirst()
{
    std::vector<MageTravelClicker> const party = {Clicker(false, false, true),
                                                  Clicker(true, false, true),
                                                  Clicker(false, true, true)};
    Check("the leader goes alone first", Same(NextPortalClickers(party), {1}));
}

void TestNobodyFollowsAnUnfinishedClick()
{
    std::vector<MageTravelClicker> const party = {Clicker(false, false, true),
                                                  Clicker(true, false, false, true),
                                                  Clicker(false, true, true)};
    Check("the rest wait for the leader's click to read back",
          NextPortalClickers(party).empty());
}

void TestTheRestGoTogetherAndTheMageLast()
{
    // Unit::RemoveFromWorld ends in RemoveAllGameObjects, so the portal closes
    // the moment its mage leaves the map.
    std::vector<MageTravelClicker> party = {Clicker(false, false, true),
                                            Clicker(false, false, true),
                                            Clicker(true, false, false),
                                            Clicker(false, true, true)};
    Check("the followers go together", Same(NextPortalClickers(party), {0, 1}));

    party[0] = Clicker(false, false, false, true);
    party[1] = Clicker(false, false, false);
    Check("the mage waits for a click still under way", NextPortalClickers(party).empty());

    party[0] = Clicker(false, false, false);
    Check("the mage goes when nobody is left", Same(NextPortalClickers(party), {3}));
}

void TestAMageTravellerIsStillLast()
{
    std::vector<MageTravelClicker> const party = {Clicker(false, false, true),
                                                  Clicker(true, true, true)};
    Check("a leading mage holds the door", Same(NextPortalClickers(party), {0}));
}

}  // namespace

int main()
{
    TestTheIdsAreTheOnesTheWorldGives();
    TestALandingPastTheClassicWorldIsNoWay();
    TestAPortalWhoseObjectLandsNowhereIsNoWay();
    TestTheWorldsIdsDriveTheChoice();
    TestTheMageAloneTeleports();
    TestThePartyTakesAPortal();
    TestTheRightCapitalIsPicked();
    TestNoRuneOfPortalsIsANamedRefusal();
    TestNoRuneForATeleportIsANamedRefusal();
    TestTheOtherRuneDoesNotPayForAPortal();
    TestATeleportNeedsNoReagentOnASpellThatTakesNone();
    TestAnUnknownSpellWalks();
    TestAShortWalkWalksSilently();
    TestTheSavingIsWhatCounts();
    TestALandingOutOfReachIsNotThatCapital();
    TestWayWords();
    TestOnlyAWaitableRefusalIsRetried();
    TestAnErrandCalledOffEndsTheRun();
    TestTheTravellerStepsThroughFirst();
    TestNobodyFollowsAnUnfinishedClick();
    TestTheRestGoTogetherAndTheMageLast();
    TestAMageTravellerIsStillLast();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("mage travel: all passed\n");
    return EXIT_SUCCESS;
}
