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
 *    minutes with no grant;
 *  - what the adapter's wiring leans on: a member something else owns is left
 *    alone, a grant whose strategy another writer took is forgotten, a
 *    member that leads keeps its strategy, death ages are
 *    placed on the module's clock, and a roaming member is not waited for,
 *    not fetched, not boarded on its leader's flight, not counted in the
 *    family's party toll and not left to take a flight of its own.
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

    // --- with the switch off nobody is ever granted ------------------------------
    // Whatever else is true of the member, of its family and of the ground it
    // stands on, and whether or not it stands still. The adapter's only grant
    // is on this step, so this is "behaviour unchanged while the switch is off".
    {
        bool anyGrant = false;
        bool everyHolderTakenBack = true;
        for (int bits = 0; bits < 64; ++bits)
        {
            IndividualRoamFacts f = Roaming();
            f.enabled = false;
            f.leadsItsParty = (bits & 1) != 0;
            f.familyLinked = (bits & 2) != 0;
            f.ownAim = (bits & 4) != 0 ? "trainer" : "";
            f.zoneHasBand = (bits & 8) != 0;
            f.offUntil = (bits & 16) != 0 ? f.now + 600 : 0;
            bool const standsStill = (bits & 32) != 0;
            anyGrant = anyGrant || IndividualRoams(f) ||
                       IndividualRoamStep(false, IndividualRoams(f), standsStill) !=
                           RoamStep::Leave;
            if (!standsStill)
                everyHolderTakenBack =
                    everyHolderTakenBack &&
                    IndividualRoamStep(true, IndividualRoams(f), false) == RoamStep::TakeBack;
        }
        Check(!anyGrant, "with the switch off no combination of facts grants the roam");
        Check(everyHolderTakenBack,
              "and a grant made while it was on is taken back on the first free poll");
    }

    // --- a member something else owns is left as it is ---------------------------
    // Dead, in the air, held by the module, lent for a flight or under a
    // command: each hands back exactly what it took, so neither a grant nor a
    // take-back is made under one.
    Check(IndividualRoamStep(false, true, true) == RoamStep::Leave,
          "no grant to a member that is dead, held, flying or under a command");
    Check(IndividualRoamStep(true, false, true) == RoamStep::Leave,
          "no take-back under a hold: the hold's own hand-back would undo it");
    Check(IndividualRoamStep(false, true, false) == RoamStep::Grant,
          "the grant is made on the first poll after it is free");
    Check(IndividualRoamStep(true, false, false) == RoamStep::TakeBack,
          "and so is the take-back");
    Check(IndividualRoamStep(true, true, false) == RoamStep::Leave &&
              IndividualRoamStep(false, false, false) == RoamStep::Leave,
          "free and nothing to change is still nothing to do");

    // --- does an earlier grant still stand ----------------------------------------
    Check(RoamGrantStands(true, true, false), "a free member that carries `new rpg` holds it");
    Check(RoamGrantStands(true, false, true),
          "one owned elsewhere holds it without the strategy: it is handed back");
    Check(RoamGrantStands(true, true, true), "and with it");
    Check(!RoamGrantStands(true, false, false),
          "a free member that lost `new rpg` to another writer no longer holds the grant");
    Check(!RoamGrantStands(false, true, false) && !RoamGrantStands(false, true, true) &&
              !RoamGrantStands(false, false, true),
          "carrying the strategy is not a grant: a lent or cut-off follower was never granted");
    // The follow sweep stripped it on an aim that came and went between two
    // polls: the grant is forgotten and, still admitted, made again.
    Check(IndividualRoamStep(RoamGrantStands(true, false, false), true, false) == RoamStep::Grant,
          "a lost grant is made afresh while the member is still admitted");
    Check(IndividualRoamStep(RoamGrantStands(true, false, false), false, false) ==
              RoamStep::Leave,
          "and simply forgotten when it is not");

    // --- what a take-back takes off ---------------------------------------------
    Check(!RoamTakeBackStrips(true),
          "a member that leads keeps `new rpg`: it carries it as leader");
    Check(RoamTakeBackStrips(false), "a follower's is taken off");
    {
        // The switch goes off on a holder that has come to lead. The refusal
        // named is the switch, asked first, and the strategy still stays.
        auto f = Roaming();
        f.enabled = false;
        f.leadsItsParty = true;
        Check(IndividualRoamRefusal(f) == RoamRefusal::SwitchOff, "the switch is named first");
        Check(IndividualRoamStep(true, IndividualRoams(f), false) == RoamStep::TakeBack &&
                  !RoamTakeBackStrips(f.leadsItsParty),
              "the grant is taken back and the leader keeps its strategy");
    }

    // --- which refusals are said ---------------------------------------------------
    Check(!RoamRefusalIsSaid(RoamRefusal::None), "no refusal, nothing to say");
    Check(!RoamRefusalIsSaid(RoamRefusal::HasErrand),
          "an errand says itself, and comes and goes too often to say again here");
    for (RoamRefusal const refusal :
         {RoamRefusal::SwitchOff, RoamRefusal::Leader, RoamRefusal::FamilyLinked,
          RoamRefusal::DeathLimit, RoamRefusal::OutOfBand})
        Check(RoamRefusalIsSaid(refusal), "every other refusal is said when it changes");

    // --- death ages are placed on the module's clock ----------------------------
    {
        std::vector<std::int64_t> const times = RoamDeathTimes({250, 200, 100, 0}, now);
        Check(times == std::vector<std::int64_t>({now - 250, now - 200, now - 100, now}),
              "an age is that many seconds before now");
        Check(RoamOffUntil(times, now, 0) == now + 30 * 60,
              "four deaths aged inside five minutes trip the limit");
        Check(RoamOffUntil(RoamDeathTimes({301, 200, 100, 0}, now), now, 0) == 0,
              "one aged out of the window does not");
        Check(RoamDeathTimes({-5}, now) == std::vector<std::int64_t>({now}),
              "a row stamped after the read is placed at now, not in the future");
        Check(RoamDeathTimes({}, now).empty(), "no rows, no deaths");
    }

    // --- the family does not hold its leader for a roaming member ---------------
    {
        RegroupLimits regroup;
        regroup.splitYards = 60.f;
        regroup.rejoinYards = 20.f;
        regroup.maxWaitYards = 0.f;
        RegroupMember away;
        away.name = "Og";
        away.seen = true;
        away.sameMap = true;
        away.alive = true;
        away.yards = 300.f;
        Check(ReadRegroupClaim(away, regroup, false) == RegroupClaim::Rejoining,
              "a far follower that does not roam is waited for");
        away.roamsAlone = true;
        Check(ReadRegroupClaim(away, regroup, false) == RegroupClaim::RoamsAlone,
              "a roaming follower is its own claim");
        Check(!RegroupClaimIsWaitedFor(RegroupClaim::RoamsAlone),
              "the leader is never held for a roaming member");
        Check(!ReadFamilyRegroup({away}, regroup, false).wait,
              "the family does not wait for a roaming member");
        Check(std::string(RegroupClaimName(RegroupClaim::RoamsAlone)) ==
                  "roaming alone inside its band",
              "the claim has a name the log can say");
        Check(RegroupCarriedOnWithout({away}, regroup, false) ==
                  "Og (roaming alone inside its band)",
              "and the family says whom it carries on without");
        away.yards = 10.f;
        Check(ReadRegroupClaim(away, regroup, false) == RegroupClaim::InFormation,
              "a roaming member beside the leader is in formation");
    }

    // --- and the leader is never sent back for one -------------------------------
    {
        FetchLimits fetch;
        fetch.footLimitYards = 100.f;
        fetch.gatheredYards = 40.f;
        FetchCandidate held;
        held.name = "Og";
        held.seen = true;
        held.sameMap = true;
        held.alive = true;
        held.heldTooFar = true;
        held.yards = 400.f;
        Check(PickFetchTarget({held}, fetch, false) == "Og",
              "a held member that does not roam is fetched");
        held.roamsAlone = true;
        Check(PickFetchTarget({held}, fetch, false).empty(), "a roaming member is never fetched");

        FetchCandidate walking;
        walking.name = "Bork";
        walking.seen = true;
        walking.sameMap = true;
        walking.alive = true;
        walking.yards = 300.f;
        FetchCandidate other = held;
        other.name = "Grog";
        other.roamsAlone = false;
        Check(PickFetchTarget({walking, other}, fetch, false).empty(),
              "a member still walking back holds every fetch");
        walking.roamsAlone = true;
        Check(PickFetchTarget({walking, other}, fetch, false) == "Grog",
              "a roaming member is not one still walking back");
    }

    // --- nor boarded on its leader's flight, nor counted in the party toll -------
    Check(MemberFollowsForFlight(true, false, false, false, false),
          "an empty column follows the leader's flight");
    Check(!MemberFollowsForFlight(true, false, false, false, false, true),
          "but not when the member roams alone: it is not behind the leader");
    Check(!MemberFollowsForFlight(false, true, true, true, false, true),
          "whatever else is true of it");
    Check(PartyTollCounts(false, false), "a member with the family counts in the party toll");
    Check(!PartyTollCounts(false, true), "a roaming member does not");
    Check(!PartyTollCounts(true, false), "nor one on its own errand, as before");
    Check(!PartyTollCounts(true, true), "nor one that is somehow both");

    // --- a roaming member takes no flight ----------------------------------------
    Check(RoamerUnissuedFlight(true, true, false) == UnissuedFlightStep::Cancel,
          "a roaming member walking to a flight master is called off on the ground");
    Check(RoamerUnissuedFlight(true, true, true) == UnissuedFlightStep::Airborne,
          "one already in the air is said, not stopped");
    Check(RoamerUnissuedFlight(true, false, false) == UnissuedFlightStep::Leave,
          "any other status is the roam itself");
    Check(RoamerUnissuedFlight(true, false, true) == UnissuedFlightStep::Leave,
          "a taxi with no travel flight status is not upstream's roll");
    Check(RoamerUnissuedFlight(false, true, false) == UnissuedFlightStep::Leave &&
              RoamerUnissuedFlight(false, true, true) == UnissuedFlightStep::Leave,
          "a member that does not roam is not this rule's");

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
