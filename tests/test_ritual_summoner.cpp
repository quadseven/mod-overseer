/* Pure selection rules for a natural guild Ritual of Summoning source. */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ChooseRitualSummoner;
using OverseerDecisions::PlanSummonRung;
using OverseerDecisions::RitualSummonerCandidate;
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
    candidate.warlock = true;
    candidate.inWorld = true;
    candidate.alive = true;
    candidate.knowsRitual = true;
    candidate.carriesSoulShard = true;
    return candidate;
}

void SelectsTheFirstEligibleGuildWarlock()
{
    RitualSummonerCandidate mage = Warlock("NotActuallyAWarlock");
    mage.warlock = false;
    RitualSummonerCandidate first = Warlock("First");
    RitualSummonerCandidate second = Warlock("Second");
    auto const choice = ChooseRitualSummoner({mage, first, second});
    Check("the first eligible warlock is selected", choice.name == "First", true);
}

void NaturalGatesAreAllRequired()
{
    RitualSummonerCandidate noGuild = Warlock("NoGuild");
    noGuild.guildMember = false;
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
    SelectsTheFirstEligibleGuildWarlock();
    NaturalGatesAreAllRequired();
    FamilyWarlockNeedsNoPartySwap();
    AWarlockOutsideTheFamilyCanBeWalkedToTheStone();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ritual summoner selection: all checks passed\n");
    return EXIT_SUCCESS;
}
