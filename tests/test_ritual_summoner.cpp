/* Pure selection rules for a natural guild Ritual of Summoning source. */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ChooseRitualSummoner;
using OverseerDecisions::PlanSummonRung;
using OverseerDecisions::RitualSummonerCandidate;
using OverseerDecisions::RitualSummonerPool;
using OverseerDecisions::SummonRungMember;
using OverseerDecisions::SummonRungStep;

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

RitualSummonerCandidate Warlock(char const* name)
{
    RitualSummonerCandidate candidate;
    candidate.name = name;
    candidate.guildMember = true;
    candidate.inFamily = true;
    candidate.warlock = true;
    candidate.inWorld = true;
    candidate.alive = true;
    candidate.knowsRitual = true;
    candidate.carriesSoulShard = true;
    return candidate;
}

void SelectsTheBestEligibleGuildWarlock()
{
    RitualSummonerCandidate mage = Warlock("NotActuallyAWarlock");
    mage.warlock = false;
    RitualSummonerCandidate first = Warlock("First");
    first.inFamily = false;
    first.distanceToDoor = 90.f;
    RitualSummonerCandidate second = Warlock("Second");
    second.inFamily = false;
    second.distanceToDoor = 10.f;
    auto const choice = ChooseRitualSummoner({mage, first, second}, RitualSummonerPool::Guild);
    Check("the closest eligible guild warlock is selected", choice.name == "Second", true);
}

void NaturalGatesAreAllRequired()
{
    RitualSummonerCandidate noGuild = Warlock("NoGuild");
    noGuild.guildMember = false;
    noGuild.inFamily = false;
    RitualSummonerCandidate noSpell = Warlock("NoSpell");
    noSpell.knowsRitual = false;
    RitualSummonerCandidate noShard = Warlock("NoShard");
    noShard.carriesSoulShard = false;
    RitualSummonerCandidate dead = Warlock("Dead");
    dead.alive = false;
    auto const choice = ChooseRitualSummoner({noGuild, noSpell, noShard, dead});
    Check("no candidate passes without every natural gate", choice.name.empty(), true);
}

void FamilyWarlockNeedsNoPartySwap()
{
    RitualSummonerCandidate member = Warlock("AlreadyInFamily");
    member.inFamily = true;
    auto const choice = ChooseRitualSummoner({member});
    Check("an eligible family warlock is selectable", choice.name == "AlreadyInFamily", true);
    Check("the reason names the real spell and reagent", choice.why.find("Soul Shard") !=
                                                           std::string::npos,
          true);
}

void GuildFallsBackToFamilyAndFamilyPoolStaysScoped()
{
    RitualSummonerCandidate eligibleGuild = Warlock("GuildFarther");
    eligibleGuild.inFamily = false;
    eligibleGuild.distanceToDoor = 100.f;
    RitualSummonerCandidate closerFamily = Warlock("FamilyCloser");
    closerFamily.guildMember = false;
    closerFamily.distanceToDoor = 1.f;
    auto const guildFirst = ChooseRitualSummoner(
        {closerFamily, eligibleGuild}, RitualSummonerPool::Guild);
    Check("eligible guild warlock outranks a closer family warlock",
          guildFirst.name == "GuildFarther", true);

    RitualSummonerCandidate guild = Warlock("UnavailableGuild");
    guild.inFamily = false;
    guild.inCombat = true;
    RitualSummonerCandidate family = Warlock("FamilyFallback");
    family.guildMember = false;
    auto const fallback = ChooseRitualSummoner({guild, family}, RitualSummonerPool::Guild);
    Check("guild pool falls back to an eligible family warlock", fallback.name == "FamilyFallback", true);

    RitualSummonerCandidate outsider = Warlock("GuildOutsider");
    outsider.inFamily = false;
    auto const familyOnly = ChooseRitualSummoner({outsider, family}, RitualSummonerPool::Family);
    Check("family pool excludes a guild member outside family", familyOnly.name == "FamilyFallback", true);
}

void NonePoolDisablesAndDoorRankPrecedesDistanceAndLevel()
{
    RitualSummonerCandidate offMap = Warlock("OffMap");
    offMap.onDoorMap = false;
    offMap.distanceToDoor = 0.f;
    RitualSummonerCandidate near = Warlock("Near");
    near.onDoorMap = true;
    near.distanceToDoor = 8.f;
    RitualSummonerCandidate far = Warlock("FarHigherLevel");
    far.onDoorMap = true;
    far.distanceToDoor = 12.f;
    far.level = 70;
    auto const ranked = ChooseRitualSummoner({offMap, far, near}, RitualSummonerPool::Guild);
    Check("door-map candidate outranks off-map candidate", ranked.name == "Near", true);

    RitualSummonerCandidate lowerLevel = Warlock("LowerLevel");
    lowerLevel.distanceToDoor = 5.f;
    lowerLevel.level = 40;
    RitualSummonerCandidate higherLevel = Warlock("HigherLevel");
    higherLevel.distanceToDoor = 5.f;
    higherLevel.level = 60;
    auto const levelRanked = ChooseRitualSummoner(
        {lowerLevel, higherLevel}, RitualSummonerPool::Guild);
    Check("level breaks equal map and distance ranks", levelRanked.name == "HigherLevel", true);

    auto const disabled = ChooseRitualSummoner({near}, RitualSummonerPool::None);
    Check("none pool disables selection", disabled.name.empty(), true);
    Check("disabled reason is explicit", disabled.why.find("disabled") != std::string::npos, true);
}

void AWarlockOutsideTheFamilyCanBeWalkedToTheStone()
{
    SummonRungMember leader;
    leader.name = "Leader";
    leader.leader = true;
    leader.inWorld = true;
    leader.alive = true;
    leader.atStone = true;
    SummonRungMember helper = leader;
    helper.name = "Helper";
    helper.leader = false;
    SummonRungMember target = leader;
    target.name = "Straggler";
    target.atStone = false;
    SummonRungMember warlock = leader;
    warlock.name = "Warlock";
    warlock.atStone = false;
    warlock.summonable = false;
    auto const plan = PlanSummonRung({leader, helper, target, warlock}, 2, "Warlock");
    Check("an outside warlock is the ritual summoner", plan.step == SummonRungStep::Summon, true);
    Check("the outside warlock is not selected as the target", plan.summoner == "Warlock", true);
    Check("a family member remains the helper", plan.helper == "Leader", true);
    Check("the straggler remains the target", plan.target == "Straggler", true);
}
}  // namespace

int main()
{
    SelectsTheBestEligibleGuildWarlock();
    NaturalGatesAreAllRequired();
    FamilyWarlockNeedsNoPartySwap();
    GuildFallsBackToFamilyAndFamilyPoolStaysScoped();
    NonePoolDisablesAndDoorRankPrecedesDistanceAndLevel();
    AWarlockOutsideTheFamilyCanBeWalkedToTheStone();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ritual summoner selection: all checks passed\n");
    return EXIT_SUCCESS;
}
