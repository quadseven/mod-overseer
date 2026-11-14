/*
 * Which mob the meter probe reads threat on (#1484).
 *
 * The probe read the family leader's current victim, and the leader is often
 * between targets at the sample instant, so the site card showed an empty
 * target and -1 for every member while damage and healing counted. The order is
 * now the leader's victim, else the tank's victim, else the hostile unit whose
 * threat list holds the most party members.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::MeterLine;
using OverseerDecisions::MeterPickTarget;
using OverseerDecisions::MeterProbeJson;
using OverseerDecisions::MeterTargetChoice;
using OverseerDecisions::MeterTargetFacts;
using OverseerDecisions::MeterTargetSource;

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

void TheLeadersVictimComesFirst()
{
    MeterTargetFacts facts;
    facts.leaderHasVictim = true;
    facts.tankHasVictim = true;
    facts.hostilePartyCounts = {3, 5};
    Check("leader first", MeterPickTarget(facts).source == MeterTargetSource::Leader);
}

void TheTankIsTheSecondChoice()
{
    MeterTargetFacts facts;
    facts.tankHasVictim = true;
    facts.hostilePartyCounts = {3};
    Check("tank second", MeterPickTarget(facts).source == MeterTargetSource::Tank);
}

void TheBusiestThreatListIsTheLastResort()
{
    MeterTargetFacts facts;
    facts.hostilePartyCounts = {1, 4, 2};
    MeterTargetChoice const choice = MeterPickTarget(facts);
    Check("threat list", choice.source == MeterTargetSource::ThreatList);
    Check("the mob with the most party members", choice.hostile == 1);

    facts.hostilePartyCounts = {2, 2};
    Check("a tie keeps the first", MeterPickTarget(facts).hostile == 0);
}

void OutOfCombatIsNoTarget()
{
    MeterTargetFacts facts;
    Check("nobody fighting", MeterPickTarget(facts).source == MeterTargetSource::None);
    facts.hostilePartyCounts = {0, 0};
    Check("hostiles holding no party member are not a target",
          MeterPickTarget(facts).source == MeterTargetSource::None);
}

void AMemberOffTheListReadsZeroNotMinusOne()
{
    // The leader has no victim; the picked mob's list holds the tank and the
    // healer. The adapter reads 0 for a member the list does not hold, and the
    // probe turns that into 0 percent, while out of combat the line stays -1.
    MeterLine tank;
    tank.name = "Grug";
    tank.threat = 400.0f;
    MeterLine healer;
    healer.name = "Ugga";
    healer.threat = 100.0f;
    MeterLine bystander;
    bystander.name = "Og";
    bystander.threat = 0.0f;
    std::string const json = MeterProbeJson({tank, healer, bystander}, true, 20, "Flayer", 400.0f);
    Check("tank pct", json.find("\"threat\":400.0,\"threat_pct\":100") != std::string::npos);
    Check("healer pct", json.find("\"threat\":100.0,\"threat_pct\":25") != std::string::npos);
    Check("bystander is 0", json.find("\"threat\":0.0,\"threat_pct\":0") != std::string::npos);

    MeterLine idle;
    idle.name = "Grug";
    std::string const quiet = MeterProbeJson({idle}, false, 0, "", 0.0f);
    Check("out of combat keeps -1 and an empty target",
          quiet.find("\"target\":\"\"") != std::string::npos &&
              quiet.find("\"threat_pct\":-1") != std::string::npos);
}

void TheProbeAsksTheDecision()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    std::size_t const probe = source.find("static std::string ProbeMeter(Player* bot)");
    Check("the module source is readable (run from the repo root)", probe != std::string::npos);
    std::size_t const pick = source.find("OverseerDecisions::MeterPickTarget(facts)", probe);
    std::size_t const json = source.find("OverseerDecisions::MeterProbeJson(", probe);
    Check("ProbeMeter picks its target through MeterPickTarget before it writes the json",
          pick != std::string::npos && json != std::string::npos && pick < json);
}
}  // namespace

int main()
{
    TheLeadersVictimComesFirst();
    TheTankIsTheSecondChoice();
    TheBusiestThreatListIsTheLastResort();
    OutOfCombatIsNoTarget();
    AMemberOffTheListReadsZeroNotMinusOne();
    TheProbeAsksTheDecision();
    if (failures)
        return 1;
    std::printf("ok test_meter_target\n");
    return 0;
}
