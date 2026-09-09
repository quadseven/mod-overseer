/*
 * Whether a member's home suits the dungeon its campaign runs (mod-overseer#348).
 *
 * WHAT THIS IS ABOUT. Read out of `character_homebind`, three of the five
 * members of a family whose campaign runs Wailing Caverns - a dungeon approached
 * from map 1 - were bound on map 0, at one doorway in the human starting zone.
 * The hearthstone resolves TARGET_DEST_HOME, which IS m_homebind*, and four
 * revival escalations teleport straight to the same three floats, so for those
 * three members every way of "going home" meant crossing an ocean away from the
 * door their party was gathering at. Nothing in the module can walk anybody
 * back: `follow` cannot cross a map, the catch-up walk returns on a cross-map
 * gap, and an `at:` aim cannot name a coordinate on another one.
 *
 * SO THE RULE PINNED HERE IS NOT "IS THIS HOME SOMEWHERE". It is whether a home
 * is the town the campaign's own approach to that door begins in - which is a
 * question about the DUNGEON and not about the character, and which has to be
 * answered without a distance whenever the two are not even on the same map.
 *
 * THE FIXTURES ARE THE REAL ROWS. The Elwynn bind is the one all three wrong
 * members carry; the Ratchet anchor is Innkeeper Wiley's own spawn point, which
 * is also the first waypoint of the module's measured corridor to that door and
 * also the one home on this roster that was already correct.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CampaignHome;
using OverseerDecisions::CampaignHomeAnchor;
using OverseerDecisions::CampaignHomeName;
using OverseerDecisions::CampaignHomeNeedsRebinding;
using OverseerDecisions::HomeBind;
using OverseerDecisions::ReadCampaignHome;

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

void CheckVerdict(char const* what, CampaignHome got, CampaignHome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, CampaignHomeName(got),
                CampaignHomeName(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

// The tolerance mod_overseer.cpp passes in (CAMPAIGN_HOME_TOWN_YARDS), named
// here so a failure reads as the rule rather than as a number.
constexpr float TOWN = 250.f;

// The home three of the five carry: map 0, area 12, one doorway in the human
// starting zone, all within 0.68 yards of each other.
HomeBind ElwynnBind()
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

// The town the Wailing Caverns row names, which is Innkeeper Wiley's spawn
// point in Ratchet: neutral, and therefore the only inn near that door an
// Alliance family can be bound at.
CampaignHomeAnchor RatchetInn()
{
    CampaignHomeAnchor anchor;
    anchor.known = true;
    anchor.mapId = 1;
    anchor.x = -1050.0f;
    anchor.y = -3664.8f;
    anchor.z = 24.36f;
    return anchor;
}

// The one home on this roster that was already right, to the yard.
HomeBind RatchetBind()
{
    HomeBind home;
    home.known = true;
    home.mapId = 1;
    home.areaId = 392;
    home.x = -1050.04f;
    home.y = -3664.80f;
    home.z = 23.97f;
    return home;
}

// What every portal row that names no town produces: the three rows that shipped
// before Wailing Caverns carry no home, exactly as they carry no corridor.
CampaignHomeAnchor DoorWithNoTown()
{
    return CampaignHomeAnchor();
}

void TheWrongContinentIsTheFailureThisExistsFor()
{
    CheckVerdict("the Elwynn bind does not suit a Kalimdor door",
                 ReadCampaignHome(ElwynnBind(), RatchetInn(), TOWN),
                 CampaignHome::OffTheDungeonsMap);
    Check("and it is answered by walking to an inn",
          CampaignHomeNeedsRebinding(
              ReadCampaignHome(ElwynnBind(), RatchetInn(), TOWN)),
          true);
}

void NoDistanceIsTakenAcrossAMapBoundary()
{
    // THE HALF #241 WAS CAUGHT ACTING ON. Two coordinate systems subtracted from
    // each other are not yards. This home is 8 yards from the anchor's numbers
    // and on the other continent, and the verdict has to be about the map.
    HomeBind nearNumbers = RatchetBind();
    nearNumbers.mapId = 0;
    CheckVerdict("coordinates that happen to be close on another map are still another map",
                 ReadCampaignHome(nearNumbers, RatchetInn(), TOWN),
                 CampaignHome::OffTheDungeonsMap);
}

void TheOneBindThatWorksSuits()
{
    CheckVerdict("the home the corridor was measured from suits",
                 ReadCampaignHome(RatchetBind(), RatchetInn(), TOWN),
                 CampaignHome::Suits);
    Check("and nothing is walked anywhere for it",
          CampaignHomeNeedsRebinding(
              ReadCampaignHome(RatchetBind(), RatchetInn(), TOWN)),
          false);
}

void TheRightMapIsNotEnoughOnItsOwn()
{
    // A home on the dungeon's own continent and a long way from the town the
    // approach begins in. Hearthing to it lands the character somewhere the
    // module has measured no way to the door from, which is the second half of
    // the same defect.
    HomeBind farInn = RatchetBind();
    farInn.x += 3000.f;
    CheckVerdict("an inn a continent's-worth of ground away is still not this town",
                 ReadCampaignHome(farInn, RatchetInn(), TOWN),
                 CampaignHome::TooFarFromTheTown);
    Check("and it is answered by walking, like the wrong map is",
          CampaignHomeNeedsRebinding(ReadCampaignHome(farInn, RatchetInn(), TOWN)),
          true);

    // Just inside the line: somewhere else in the same town is the same town.
    // The number is not delicate - the next innkeeper on that map is 940 yards
    // off - and this is what it buys.
    HomeBind acrossTown = RatchetBind();
    acrossTown.y += 200.f;
    CheckVerdict("the far side of the same town is the same town",
                 ReadCampaignHome(acrossTown, RatchetInn(), TOWN),
                 CampaignHome::Suits);
}

void AnInnHasFloorsAndThatDoesNotMatterHere()
{
    // BindReadBack counts height because it asks whether a character moved
    // between two binds and an inn has an upstairs. This asks whether a home is
    // in the right TOWN, and every distance the module measures against a place
    // on a map is a 2D one. A home directly above the anchor is that anchor.
    HomeBind upstairs = RatchetBind();
    upstairs.z += 40.f;
    CheckVerdict("height alone does not put a home in another town",
                 ReadCampaignHome(upstairs, RatchetInn(), TOWN),
                 CampaignHome::Suits);
}

void ADoorThatNamesNoTownJudgesNobody()
{
    CheckVerdict("no town, no opinion",
                 ReadCampaignHome(ElwynnBind(), DoorWithNoTown(), TOWN),
                 CampaignHome::NotAsked);
    Check("and nobody is walked anywhere for it",
          CampaignHomeNeedsRebinding(
              ReadCampaignHome(ElwynnBind(), DoorWithNoTown(), TOWN)),
          false);

    // ASKED BEFORE THE HOME IS EVEN LOOKED AT. A campaign that names no town is
    // not judging anybody, so an unread home there is not a complaint - it is a
    // reading nobody wanted.
    HomeBind const unread;
    CheckVerdict("an unread home against no town is still not-asked",
                 ReadCampaignHome(unread, DoorWithNoTown(), TOWN),
                 CampaignHome::NotAsked);
}

void AReadingNobodyTookIsNotAVerdict()
{
    HomeBind const unread;
    Check("an unread home defaults to unknown rather than to map zero", unread.known,
          false);
    CheckVerdict("an unread home against a real town is unreadable",
                 ReadCampaignHome(unread, RatchetInn(), TOWN),
                 CampaignHome::Unreadable);
    Check("and it is not walked anywhere on a reading nobody took",
          CampaignHomeNeedsRebinding(ReadCampaignHome(unread, RatchetInn(), TOWN)),
          false);
}

void ADoorApproachedFromMapZeroIsNotSpecialCased()
{
    // The three portals that shipped before Wailing Caverns are approached from
    // map 0, which is where this family already lives. The rule is equality
    // against the door's own outside map, not "is this Kalimdor", so a campaign
    // that named a town in the human starting zone would read the Elwynn bind as
    // suiting it.
    CampaignHomeAnchor elwynnInn;
    elwynnInn.known = true;
    elwynnInn.mapId = 0;
    elwynnInn.x = -8949.95f;
    elwynnInn.y = -132.493f;
    elwynnInn.z = 83.5312f;
    CheckVerdict("map 0 is a real map and the rule is equality",
                 ReadCampaignHome(ElwynnBind(), elwynnInn, TOWN), CampaignHome::Suits);
    CheckVerdict("and a Kalimdor home does not suit a map 0 door either",
                 ReadCampaignHome(RatchetBind(), elwynnInn, TOWN),
                 CampaignHome::OffTheDungeonsMap);
}

void TheVerdictWordsAreTheOnesALogLineCarries()
{
    CheckWord("suits", CampaignHomeName(CampaignHome::Suits), "suits");
    CheckWord("not asked", CampaignHomeName(CampaignHome::NotAsked), "not-asked");
    CheckWord("off the dungeon's map",
              CampaignHomeName(CampaignHome::OffTheDungeonsMap), "off-the-dungeons-map");
    CheckWord("too far", CampaignHomeName(CampaignHome::TooFarFromTheTown), "too-far");
    CheckWord("unreadable", CampaignHomeName(CampaignHome::Unreadable), "unreadable");
}

}  // namespace

int main()
{
    TheWrongContinentIsTheFailureThisExistsFor();
    NoDistanceIsTakenAcrossAMapBoundary();
    TheOneBindThatWorksSuits();
    TheRightMapIsNotEnoughOnItsOwn();
    AnInnHasFloorsAndThatDoesNotMatterHere();
    ADoorThatNamesNoTownJudgesNobody();
    AReadingNobodyTookIsNotAVerdict();
    ADoorApproachedFromMapZeroIsNotSpecialCased();
    TheVerdictWordsAreTheOnesALogLineCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a home is judged against the dungeon its campaign runs, "
                "and the map is asked before any distance is\n");
    return EXIT_SUCCESS;
}
