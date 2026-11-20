/*
 * Source contract for the individual roam (#1518).
 *
 * The rule is proved in test_individual_roam.cpp and the band table in
 * test_zone_band_header.cpp and test_zone_band.py. What only the adapter can
 * get wrong, and what this file pins:
 *
 *  - THE SWITCH. Overseer.Individual.Enable is read with a default of false,
 *    and with it off and no grant outstanding the poll returns before any
 *    read. The Grant step is the only writer of a name into _individualRoams
 *    anywhere in the file, so with the switch off nobody roams and every gate
 *    below answers as it did before.
 *  - THE GRANT. `new rpg` is added only on OverseerDecisions' Grant step, read
 *    back, and the step is asked with the member's own facts: the band of the
 *    zone it stands in for its side from the compiled-in table, its level, its
 *    column, its family's re-link facts and its own deaths.
 *  - THE TAKE-BACK. The TakeBack step forgets the grant and strips `new rpg`
 *    (and idles the rpg status) unless the member leads its party. A member
 *    something else owns this poll is left alone, and a grant whose strategy
 *    another drive took is forgotten.
 *  - THE DEATHS. The member's own death ages from overseer_death feed
 *    RoamOffUntil through RoamDeathTimes, and a hold ends on its clock.
 *  - THE GATES. The follow sweep's strip, the catch-up, the stall nudge, the
 *    regroup wait, the fetch, the party flight and the party death toll all
 *    read RoamsAlone, which also asks the column; and a roaming member's own
 *    flight is called off on the ground.
 */

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

std::string Read(char const* path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

bool Before(std::string const& text, char const* first, char const* second)
{
    std::size_t const a = text.find(first);
    std::size_t const b = text.find(second);
    return a != std::string::npos && b != std::string::npos && a < b;
}

std::size_t Count(std::string const& text, char const* needle)
{
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1))
        ++count;
    return count;
}
}  // namespace

int main()
{
    std::string const text = Read("src/mod_overseer.cpp");
    if (text.empty())
    {
        std::cerr << "FAIL cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }

    Check(Has(text, "#include \"overseer_zone_band.h\""), "the band table is compiled in");

    // --- the switch ------------------------------------------------------------
    std::string const refresh = Body(text, "    void RefreshIndividualRoams(");
    Check(!refresh.empty(), "RefreshIndividualRoams exists");
    Check(Has(refresh, "GetOption<bool>(\"Overseer.Individual.Enable\", false, false)"),
          "the switch is read with a default of false");
    Check(Before(refresh, "if (!enabled && _individualRoams.empty())", "ReadFamilyLinks()") &&
              Before(refresh, "if (!enabled && _individualRoams.empty())", "ChangeStrategy("),
          "switched off with no grant outstanding, the poll returns before any read or grant");
    Check(Has(refresh, "enabled ? ReadFamilyLinks() : FamilyLinkRead()"),
          "the roster is read only while the switch is on");
    Check(Has(refresh, "facts.enabled = enabled;"), "the switch reaches the pure rule");
    Check(Count(text, "_individualRoams.insert(") == 1 &&
              Has(refresh, "_individualRoams.insert(name);"),
          "one statement in the whole file puts a name in the set, and it is in this poll");
    Check(Count(text, "_individualRoams.swap(") == 0 && Count(text, "_individualRoams = ") == 0 &&
              Count(text, "_individualRoams.emplace(") == 0,
          "and nothing else can fill it");

    // --- the grant -------------------------------------------------------------
    Check(Has(refresh, "OverseerDecisions::IndividualRoamRefusal(facts)"),
          "the refusal is the pure rule's");
    Check(Has(refresh, "OverseerDecisions::IndividualRoamStep(\n"
                       "                holdsGrant, refusal == OverseerDecisions::RoamRefusal::None, "
                       "ownedElsewhere)"),
          "the step is the pure rule's, asked with the grant held and who else owns the member");
    Check(Has(refresh, "bool const granted = _individualRoams.count(name) != 0;") &&
              Has(refresh, "OverseerDecisions::RoamGrantStands(\n"
                           "                granted, botAI->HasStrategy(\"new rpg\", "
                           "BOT_STATE_NON_COMBAT), ownedElsewhere);"),
          "a grant stands only while the strategy is carried or is coming back");
    Check(Before(refresh, "if (granted && !holdsGrant)", "_individualRoams.erase(name);") &&
              Before(refresh, "if (granted && !holdsGrant)",
                     "OverseerDecisions::RoamStep const step"),
          "a grant another drive stripped is forgotten before the step is asked");
    Check(Count(refresh, "ChangeStrategy(\"+new rpg\", BOT_STATE_NON_COMBAT)") == 1 &&
              Before(refresh, "if (step == OverseerDecisions::RoamStep::Grant)",
                     "ChangeStrategy(\"+new rpg\", BOT_STATE_NON_COMBAT)") &&
              Before(refresh, "ChangeStrategy(\"+new rpg\", BOT_STATE_NON_COMBAT)",
                     "if (step == OverseerDecisions::RoamStep::TakeBack)"),
          "`new rpg` is added once, and only on the Grant step");
    Check(Before(refresh, "ChangeStrategy(\"+new rpg\", BOT_STATE_NON_COMBAT)",
                 "if (!botAI->HasStrategy(\"new rpg\", BOT_STATE_NON_COMBAT))") &&
              Before(refresh, "if (!botAI->HasStrategy(\"new rpg\", BOT_STATE_NON_COMBAT))",
                     "_individualRoams.insert(name);"),
          "the grant is read back before the member counts as roaming");
    Check(Has(refresh, "bool const ownedElsewhere = RoamOwnedElsewhere(bot, name);"),
          "who else owns the member is asked once");
    std::string const owned = Body(text, "    bool RoamOwnedElsewhere(");
    Check(Has(owned, "!bot->IsAlive()") && Has(owned, "HeldAfterRevival(name)") &&
              Has(owned, "HeldStill(name)"),
          "a dead or held member is not the roam's to change");
    Check(Has(owned, "bot->IsInFlight()") && Has(owned, "_flightLoans.count(name) != 0"),
          "nor one in the air or lent `new rpg` for its leader's flight");
    Check(Has(owned, "for (HuntState const& hunt : _hunts)") &&
              Has(owned, "for (QuestParty const& party : _questParties)"),
          "nor one a hunt or a quest party took the strategy off");
    Check(Has(refresh, "if (!botAI || IsRealPlayer(botAI->GetMaster()))"),
          "a member a person leads is not granted");

    // --- the facts -------------------------------------------------------------
    Check(Has(refresh, "facts.leadsItsParty = LeadsItsParty(bot);"), "the leader is read");
    Check(Has(refresh, "facts.familyLinked = links.LinkedFor(name);"),
          "the family's link is read");
    Check(Has(refresh,
              "facts.ownAim = aim != aims.end() ? aim->second : _travelAims.TargetFor(name);"),
          "the member's own column is read, as loaded or as claimed since");
    Check(Has(refresh, "OverseerDecisions::ZoneBandTopFor(\n"
                       "                bot->GetMapId(), bot->GetZoneId(), "
                       "bot->GetTeamId() == TEAM_HORDE)"),
          "the band is the compiled-in table's, for the member's map, zone and side");
    Check(Has(refresh, "facts.zoneHasBand = band.hasBand && map && !map->Instanceable();") &&
              Has(refresh, "facts.zoneBandTop = band.top;"),
          "the band reaches the rule, and never inside an instance");
    Check(Has(refresh, "facts.level = bot->GetLevel();"), "the member's level is read");

    std::string const links = Body(text, "    FamilyLinkRead ReadFamilyLinks() const");
    Check(Has(links, "LoadAllJobs(read.jobsRead)") && Has(links, "LoadRosterFamilies()"),
          "the family read is the errand rule's two loaders");
    Check(Has(links, "InAnyDungeonRun(m)") && Has(links, "SummonAskedFor()") &&
              Has(links, "OverseerDecisions::FamilyReLinks(relink)"),
          "a job, a run or a summon re-links the whole family through the pure rule");
    Check(Has(text, "return !jobsRead || family == familyOf.end() || family->second.empty() ||\n"
                    "                   reLinked.count(family->second) != 0;"),
          "a failed job read, an unnamed member and an empty family fail closed");

    // --- the take-back ---------------------------------------------------------
    std::size_t const takeBackAt = refresh.find("if (step == OverseerDecisions::RoamStep::TakeBack)");
    std::string const takeBack =
        takeBackAt == std::string::npos ? std::string() : refresh.substr(takeBackAt);
    Check(Has(takeBack, "_individualRoams.erase(name);"), "the take-back forgets the grant");
    Check(Before(takeBack, "OverseerDecisions::RoamTakeBackStrips(facts.leadsItsParty)",
                 "botAI->ChangeStrategy(\"-new rpg\", BOT_STATE_NON_COMBAT);"),
          "and strips `new rpg`, unless the member leads its party");
    Check(Before(takeBack, "botAI->rpgInfo.ChangeToIdle();",
                 "botAI->ChangeStrategy(\"-new rpg\", BOT_STATE_NON_COMBAT);"),
          "the rpg status is idled with it");
    Check(Has(takeBack, "OverseerDecisions::RoamRefusalName(refusal)"),
          "the take-back says why");
    Check(Has(refresh, "if (holdsGrant || !OverseerDecisions::RoamRefusalIsSaid(refusal))"),
          "a refusal is said by the pure rule's leave, and only when it changes");
    Check(Count(refresh, "ChangeStrategy(\"-new rpg\", BOT_STATE_NON_COMBAT)") == 1,
          "nothing else in the poll strips the strategy");

    // --- the deaths ------------------------------------------------------------
    Check(Has(refresh, "OverseerDecisions::RoamOffUntil(\n"
                       "                    OverseerDecisions::RoamDeathTimes(\n"
                       "                        RecentDeathAges(name, "
                       "ROAM_DEATH_LIMITS.windowSeconds), now),\n"
                       "                    now, facts.offUntil, ROAM_DEATH_LIMITS);"),
          "the member's own deaths feed RoamOffUntil over the limit's window");
    Check(Has(text, "static constexpr OverseerDecisions::RoamDeathLimits ROAM_DEATH_LIMITS{};"),
          "the limits are the decisions' defaults");
    std::string const ages = Body(text, "    std::vector<int64_t> RecentDeathAges(");
    Check(Has(ages, "SELECT TIMESTAMPDIFF(SECOND, created_at, NOW()) FROM overseer_death") &&
              Has(ages, "WHERE character_name = '{}' AND created_at >= NOW() - INTERVAL {} SECOND") &&
              Has(ages, "Esc(name), seconds"),
          "the ages are that member's rows of the death table, on the table's own clock");
    Check(Has(refresh, "it = now < it->second ? std::next(it) : _roamOffUntil.erase(it);"),
          "a death hold ends on its clock");
    Check(Has(refresh, "facts.offUntil = off == _roamOffUntil.end() ? 0 : off->second;") &&
              Has(refresh, "_roamOffUntil[name] = facts.offUntil;"),
          "the hold is carried from poll to poll");

    // --- the travel poll -------------------------------------------------------
    std::string const travel = Body(text, "    void DriveTravel()");
    Check(Before(travel, "RefreshIndividualErrands(aims);", "RefreshIndividualRoams(aims);") &&
              Before(travel, "RefreshIndividualRoams(aims);", "if (aims.empty())"),
          "the travel poll refreshes the roam after the errands and before the empty return");

    // --- the gates -------------------------------------------------------------
    std::string const roams = Body(text, "    bool RoamsAlone(std::string const& name) const");
    Check(Has(roams, "_individualRoams.count(name) != 0 && _travelAims.TargetFor(name).empty()"),
          "a member roams only while it holds the grant and its column is empty");

    std::string const steer = Body(text, "    bool MaySteerItself(std::string const& name) const");
    Check(Has(steer, "RoamsAlone(name)"),
          "MaySteerItself asks RoamsAlone, so the follow sweep does not strip the grant");

    std::string const catchUp = Body(text, "    void DriveCatchUp(");
    Check(Before(catchUp, "if (RoamsAlone(name))\n            return;", "DriveStrandedMember("),
          "DriveCatchUp returns for a roaming member before any walk back");

    Check(Has(text, "!RoamsAlone(p->GetName()) && progress.stalled &&"),
          "the stall nudge leaves a roaming member alone");

    std::string const together = Body(text, "    void KeepTheFamilyTogether(");
    Check(Has(together, "member.roamsAlone = RoamsAlone(member.name);"),
          "the regroup wait reads it");

    std::string const fetch = Body(text, "    void DriveFetch(");
    Check(Has(fetch, "member.roamsAlone = RoamsAlone(member.name);"), "the fetch reads it");

    std::string const seat = Body(text, "    void ReadPartyFlightSeat(");
    Check(Has(seat, "FetchTargetOf(leader->GetName()) == seat.read.name,\n"
                    "            RoamsAlone(seat.read.name));"),
          "the leader's flight neither boards nor waits for a roaming member");

    std::string const party = Body(text, "    uint32 RecentPartyDeathCount(");
    Check(Has(party, "OverseerDecisions::PartyTollCounts(WalksItsOwnErrand(member->GetName()),\n"
                     "                                                       "
                     "RoamsAlone(member->GetName()))"),
          "a roaming member is left out of another walker's party toll");

    std::string const grounded = Body(text, "    void KeepRoamersGrounded()");
    Check(Has(grounded, "for (std::string const& name : _individualRoams)") &&
              Has(grounded, "OverseerDecisions::RoamerUnissuedFlight(\n"
                            "                    RoamsAlone(name), "
                            "ai->rpgInfo.GetStatus() == RPG_TRAVEL_FLIGHT,\n"
                            "                    bot->IsInFlight());"),
          "a roaming member's own flight is judged by the pure rule");
    Check(Before(grounded, "if (step == OverseerDecisions::UnissuedFlightStep::Cancel)",
                 "ai->rpgInfo.ChangeToIdle();") &&
              Count(grounded, "ChangeStrategy(") == 0,
          "and called off on the ground by idling the status, the strategy kept");
    Check(Before(text, "KeepTownLeadersGrounded();\n", "            KeepRoamersGrounded();\n"),
          "on the five second clock, beside the town leaders' check");

    // --- the conf template -----------------------------------------------------
    std::string const conf = Read("conf/mod_overseer.conf.dist");
    Check(Has(conf, "#Overseer.Individual.Enable = 0"),
          "the conf template documents the switch off by default");
    Check(Has(conf, "roam alone (#1518)") && Has(conf, "at most its own level plus one"),
          "and says what the switch also turns on");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "individual roam source: ok\n";
    return EXIT_SUCCESS;
}
