/*
 * A family's tank or healer in a guild run (wow-overseer issue 784).
 *
 * The operator, 2026-10-10: a family's tank and healer may take guild-run
 * seats while their own family has no campaign run going, and are never
 * pulled from a family campaign. Before this every family member a finder-run
 * row named refused the whole run. These pin who of the roster a row may
 * seat, and the word a refusal gives.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::GuildFinderFamilyFacts;
using OverseerDecisions::GuildFinderFamilySeat;
using OverseerDecisions::GuildFinderFamilySeatOf;
using OverseerDecisions::GuildFinderFamilySeatWord;
using OverseerDecisions::GuildSeat;

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

GuildFinderFamilyFacts Family(GuildSeat seat)
{
    GuildFinderFamilyFacts f;
    f.onRoster = true;
    f.seat = seat;
    return f;
}

void AGuildmateIsNotTheRostersToJudge()
{
    GuildFinderFamilyFacts f;
    f.seat = GuildSeat::Damage;
    Check("a guildmate off the roster is seated as before",
          GuildFinderFamilySeatOf(f) == GuildFinderFamilySeat::NotFamily);
    Check("and says nothing", std::string(GuildFinderFamilySeatWord(GuildFinderFamilySeatOf(f))).empty());
}

void AnIdleFamilysHealerOrTankIsLent()
{
    Check("an idle family's healer is lent to the healer seat",
          GuildFinderFamilySeatOf(Family(GuildSeat::Healer)) == GuildFinderFamilySeat::Lent);
    Check("an idle family's tank is lent to the tank seat",
          GuildFinderFamilySeatOf(Family(GuildSeat::Tank)) == GuildFinderFamilySeat::Lent);
    Check("a lent member refuses nothing",
          std::string(GuildFinderFamilySeatWord(GuildFinderFamilySeat::Lent)).empty());
}

void NeverFromACampaign()
{
    GuildFinderFamilyFacts f = Family(GuildSeat::Healer);
    f.familyCampaign = true;
    Check("a family whose campaign is armed or running lends nobody",
          GuildFinderFamilySeatOf(f) == GuildFinderFamilySeat::Campaign);
    Check("and the refusal says so",
          std::string(GuildFinderFamilySeatWord(GuildFinderFamilySeatOf(f))).find("campaign") !=
              std::string::npos);
}

void NeverTheHead()
{
    GuildFinderFamilyFacts f = Family(GuildSeat::Tank);
    f.head = true;
    Check("the family's head stays with the party that follows it",
          GuildFinderFamilySeatOf(f) == GuildFinderFamilySeat::Head);
    Check("with a word", *GuildFinderFamilySeatWord(GuildFinderFamilySeat::Head) != '\0');
}

void NeverADamageSeat()
{
    Check("a family member is not lent to a damage seat",
          GuildFinderFamilySeatOf(Family(GuildSeat::Damage)) == GuildFinderFamilySeat::DamageSeat);
    Check("with a word", *GuildFinderFamilySeatWord(GuildFinderFamilySeat::DamageSeat) != '\0');
}

} // namespace

int main()
{
    AGuildmateIsNotTheRostersToJudge();
    AnIdleFamilysHealerOrTankIsLent();
    NeverFromACampaign();
    NeverTheHead();
    NeverADamageSeat();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("guild finder family seat: all checks passed\n");
    return 0;
}
