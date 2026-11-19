/*
 * An individual member roams alone only inside its level band (#1518).
 *
 * A follower of a family on no dungeon or raid job, with no errand of its
 * own, may be handed `new rpg` only while the zone it stands in has a band
 * top of at most its own level plus one, and only behind
 * Overseer.Individual.Enable. This file proves the pure halves:
 *
 *  - the band test, against the top of the zone's band for the member's side;
 *  - the take-back: leaving the zone, the family re-linking, or the switch
 *    going off;
 *  - the death limit: more than three deaths in five minutes, then thirty
 *    minutes with no grant.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}

// A level 30 follower of an unlinked family, no errand, standing in a zone
// whose band tops out at 30.
OverseerDecisions::IndividualRoamFacts Roaming()
{
    OverseerDecisions::IndividualRoamFacts facts;
    facts.enabled = true;
    facts.leadsItsParty = false;
    facts.familyLinked = false;
    facts.ownAim = "";
    facts.zoneHasBand = true;
    facts.zoneBandTop = 30;
    facts.level = 30;
    facts.offUntil = 0;
    facts.now = 100000;
    return facts;
}
}  // namespace

int main()
{
    using namespace OverseerDecisions;

    // --- the band test ---------------------------------------------------------
    Check(ZoneBandAdmits(true, 30, 30), "a band top at the member's level admits");
    Check(ZoneBandAdmits(true, 31, 30), "a band top one above the member's level admits");
    Check(!ZoneBandAdmits(true, 32, 30), "a band top two above the member's level refuses");
    Check(ZoneBandAdmits(true, 12, 30), "a zone far below the member admits");
    Check(!ZoneBandAdmits(false, 0, 80), "a zone with no band refuses, whatever the level");
    Check(!ZoneBandAdmits(false, 5, 80), "no band refuses even with a top filled in");
    Check(!ZoneBandAdmits(true, 1, 0), "an unread level refuses");
    Check(!ZoneBandAdmits(true, 0, 30), "a band top of zero is not a band");
    Check(ZoneBandAdmits(true, 80, UINT32_MAX), "a level at the type's maximum does not wrap");
    Check(ZoneBandAdmits(true, UINT32_MAX, UINT32_MAX), "nor does a band top there");
    Check(!ZoneBandAdmits(true, UINT32_MAX, UINT32_MAX - 2), "and two above still refuses");

    // --- the grant -------------------------------------------------------------
    Check(IndividualRoams(Roaming()), "a follower in its band, family unlinked, no errand, roams");
    Check(IndividualRoamRefusal(Roaming()) == RoamRefusal::None, "and no refusal is named");
    {
        IndividualRoamFacts const nothingKnown;
        Check(!IndividualRoams(nothingKnown), "nothing known grants nothing");
        Check(IndividualRoamRefusal(nothingKnown) == RoamRefusal::SwitchOff,
              "and the default is the switch off (the module ships dark)");
    }
    {
        auto f = Roaming();
        f.enabled = false;
        Check(!IndividualRoams(f), "the switch off grants nobody");
        Check(IndividualRoamRefusal(f) == RoamRefusal::SwitchOff, "the switch off is named");
    }
    {
        auto f = Roaming();
        f.leadsItsParty = true;
        Check(IndividualRoamRefusal(f) == RoamRefusal::Leader,
              "the leader is not this rule's: it carries `new rpg` as leader");
    }
    {
        auto f = Roaming();
        f.ownAim = "trainer";
        Check(IndividualRoamRefusal(f) == RoamRefusal::HasErrand,
              "a member with an errand walks the errand, it does not roam");
    }
    {
        auto f = Roaming();
        f.zoneBandTop = 32;
        Check(IndividualRoamRefusal(f) == RoamRefusal::OutOfBand,
              "a zone whose band top is two above the member is refused");
        f.zoneBandTop = 31;
        Check(IndividualRoams(f), "level plus one is still inside");
    }
    {
        auto f = Roaming();
        f.zoneHasBand = false;
        Check(IndividualRoamRefusal(f) == RoamRefusal::OutOfBand,
              "a zone the table has no band for is refused");
    }

    // --- the take-back ---------------------------------------------------------
    Check(IndividualRoamStep(false, true) == RoamStep::Grant, "not holding it and allowed: grant");
    Check(IndividualRoamStep(true, true) == RoamStep::Leave, "holding it and allowed: leave it");
    Check(IndividualRoamStep(true, false) == RoamStep::TakeBack,
          "holding it and no longer allowed: take it back");
    Check(IndividualRoamStep(false, false) == RoamStep::Leave,
          "not holding it and not allowed: nothing to do");
    {
        // Leaving the zone: the same member walks into a zone above its band.
        auto f = Roaming();
        Check(IndividualRoamStep(false, IndividualRoams(f)) == RoamStep::Grant, "granted in band");
        f.zoneBandTop = 44;
        Check(IndividualRoamStep(true, IndividualRoams(f)) == RoamStep::TakeBack,
              "leaving for a zone above its band takes it back");
    }
    {
        // The switch going off.
        auto f = Roaming();
        f.enabled = false;
        Check(IndividualRoamStep(true, IndividualRoams(f)) == RoamStep::TakeBack,
              "the switch going off takes it back");
    }
    {
        // The family re-linking, by each of the three facts of #1519.
        FamilyReLinkFacts job;
        job.memberJobs = {"quest", "dungeon:deadmines"};
        FamilyReLinkFacts run;
        run.memberJobs = {"quest", "craft"};
        run.anyMemberInRun = true;
        FamilyReLinkFacts summon;
        summon.memberJobs = {"quest"};
        summon.summonAsked = true;
        FamilyReLinkFacts none;
        none.memberJobs = {"quest", "craft", "town run"};
        for (FamilyReLinkFacts const* relink : {&job, &run, &summon})
        {
            auto f = Roaming();
            f.familyLinked = FamilyReLinks(*relink);
            Check(IndividualRoamRefusal(f) == RoamRefusal::FamilyLinked,
                  "a re-linked family is named as the refusal");
            Check(IndividualRoamStep(true, IndividualRoams(f)) == RoamStep::TakeBack,
                  "a dungeon or raid job, a run or a summon takes it back");
        }
        auto f = Roaming();
        f.familyLinked = FamilyReLinks(none);
        Check(IndividualRoamStep(true, IndividualRoams(f)) == RoamStep::Leave,
              "trade and quest jobs do not re-link, so it keeps roaming");
    }

    // --- the death limit -------------------------------------------------------
    RoamDeathLimits const limits;
    Check(limits.deaths == 3, "the limit is three deaths");
    Check(limits.windowSeconds == 5 * 60, "counted over five minutes");
    Check(limits.offSeconds == 30 * 60, "and thirty minutes off");
    std::int64_t const now = 100000;
    Check(RoamOffUntil({}, now, 0) == 0, "no death, no hold");
    Check(RoamOffUntil({now - 200, now - 100, now}, now, 0) == 0,
          "three deaths in five minutes is not more than three");
    Check(RoamOffUntil({now - 250, now - 200, now - 100, now}, now, 0) == now + 30 * 60,
          "the fourth death in five minutes holds it off for thirty minutes");
    Check(RoamOffUntil({now - 299, now - 200, now - 100, now}, now, 0) == now + 30 * 60,
          "a death 299 seconds old is still inside the window");
    Check(RoamOffUntil({now - 300, now - 200, now - 100, now}, now, 0) == 0,
          "a death five minutes old has left the window");
    Check(RoamOffUntil({now - 900, now - 800, now - 100, now}, now, 0) == 0,
          "four deaths spread over fifteen minutes do not trip it");
    Check(RoamOffUntil({now + 10, now + 20, now + 30, now + 40}, now, 0) == 0,
          "a death stamped in the future is not counted");
    Check(RoamOffUntil({}, now, now + 600) == now + 600,
          "a hold still running is kept as it is");
    Check(RoamOffUntil({now - 30, now - 20, now - 10, now}, now, now + 600) == now + 600,
          "and more deaths while it runs do not move its end");
    Check(RoamOffUntil({}, now, now) == 0, "a hold that has run out is cleared");
    Check(RoamOffUntil({now - 1800, now - 1790, now - 1780, now - 1770}, now, now - 1) == 0,
          "the deaths that tripped it are long out of the window when it ends");
    {
        RoamDeathLimits tight;
        tight.deaths = 1;
        tight.windowSeconds = 60;
        tight.offSeconds = 120;
        Check(RoamOffUntil({now - 10, now}, now, 0, tight) == now + 120,
              "the limits are the caller's to pass");
    }

    // The hold reaches the grant: the member rejoins its leader and is not
    // granted again until the thirty minutes are over.
    {
        auto f = Roaming();
        f.now = now;
        f.offUntil = RoamOffUntil({now - 250, now - 200, now - 100, now}, now, 0);
        Check(IndividualRoamRefusal(f) == RoamRefusal::DeathLimit, "the death limit is named");
        Check(IndividualRoamStep(true, IndividualRoams(f)) == RoamStep::TakeBack,
              "more than three deaths in five minutes: it rejoins its leader");
        f.now = now + 30 * 60 - 1;
        Check(!IndividualRoams(f), "not granted again inside the thirty minutes");
        f.now = now + 30 * 60;
        Check(IndividualRoams(f), "granted again once the thirty minutes are over");
    }

    // --- the refusal has a name for the log ------------------------------------
    Check(std::string(RoamRefusalName(RoamRefusal::None)) == "none", "none");
    Check(std::string(RoamRefusalName(RoamRefusal::SwitchOff)) == "switch off", "switch off");
    Check(std::string(RoamRefusalName(RoamRefusal::Leader)) == "leads its party", "leader");
    Check(std::string(RoamRefusalName(RoamRefusal::FamilyLinked)) == "family linked", "linked");
    Check(std::string(RoamRefusalName(RoamRefusal::HasErrand)) == "has an errand", "errand");
    Check(std::string(RoamRefusalName(RoamRefusal::OutOfBand)) == "zone above its band", "band");
    Check(std::string(RoamRefusalName(RoamRefusal::DeathLimit)) == "death limit", "deaths");

    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_individual_roam: ok\n";
    return EXIT_SUCCESS;
}
