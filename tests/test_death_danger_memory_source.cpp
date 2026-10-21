/*
 * The danger memory is wired into the live paths (#151).
 *
 * test_death_danger_memory.cpp proves the rules; this reads src/mod_overseer.cpp
 * (run from the repo root) and pins that the adapter asks them: overseer_death
 * is read back on the engagement poll, the pull guard and the hunt refuse a
 * booked entry, travel plans around a dangerous zone and logs the zone, and the
 * `danger` probe hands the list to the family page.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

// The text from `start` up to `end`, or "" when `start` is missing.
std::string Body(std::string const& source, std::string const& start, std::string const& end)
{
    std::size_t const at = source.find(start);
    if (at == std::string::npos)
        return "";
    std::size_t const stop = source.find(end, at + start.size());
    return source.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
}

void TheDeathsAreReadBackOnTheEngagementPoll(std::string const& source)
{
    std::string const poll =
        Body(source, "if (_engagementTimer >= ENGAGEMENT_POLL_MS)", "DriveEngagementSafety();");
    Check("the engagement poll refreshes the memory first", Has(poll, "RefreshDangerMemory();"));
    std::string const refresh =
        Body(source, "static void RefreshDangerMemory()", "static std::vector<OverseerDecisions::ZoneDanger> ZoneDangerFor");
    Check("it reads creature kills from overseer_death",
          Has(refresh, "FROM overseer_death WHERE killer_type = 'creature'"));
    Check("the killer level comes from the creature template",
          Has(refresh, "GetCreatureTemplate(d.killerEntry)") && Has(refresh, "tmpl->maxlevel"));
    Check("and books twice-lethal entries for the session",
          Has(refresh, "ScoreCreatureDanger(") && Has(refresh, "NotePullSuppressions("));
}

void ABookedEntryIsNotPulled(std::string const& source)
{
    std::string const timer = Body(source, "if (_pullGuardTimer >= PULL_GUARD_POLL_MS)", "}");
    Check("the pull guard runs on its own clock", Has(timer, "DrivePullGuard();"));
    std::string const guard = Body(source, "void DrivePullGuard()", "static Creature* ChosenFightOf");
    Check("it asks the rule with the creature's combat state",
          Has(guard, "RefuseVoluntaryPull(book, facts, DANGER_PULL_LIFT_LEVELS)") &&
              Has(guard, "facts.targetInCombat = target->IsInCombat();"));
    std::string const chosen = Body(source, "static Creature* ChosenFightOf", "void RefusePull(");
    Check("the fight being chosen is the combat engine's current target",
          Has(chosen, "GetValue<Unit*>(\"current target\")"));
    std::string const refuse = Body(source, "void RefusePull(", "// ------");
    Check("a refused pull stops the attack and clears the targets",
          Has(refuse, "bot->AttackStop();") && Has(refuse, "current->Set(nullptr);") &&
              Has(refuse, "DriveSelection(session, ObjectGuid::Empty);"));
    Check("the hunt drive asks the same rule before it swings",
          Has(source, "if (step == D::HuntStep::Pull && target)\n"
                      "                    HuntPullUnlessRefused(hunt, who, target);"));
    std::string const hunt =
        Body(source, "static void HuntPullUnlessRefused", "// THE CORPSE, LOOTED");
    Check("and only swings when it is not refused",
          Has(hunt, "RefuseVoluntaryPull(Danger().pulls, facts,") && Has(hunt, "HuntPull(who, target);"));
}

void TravelRoutesAroundADangerousZone(std::string const& source)
{
    std::string const plan = Body(source, "static std::vector<OverseerDecisions::RoutePoint> PlanRoute(",
                                  "// THE JOINED ROAD IS THE WHOLE ROUTE");
    Check("PlanRoute asks for a way round the dangerous zone",
          Has(plan, "plan = AvoidDangerousZones(bot, survey, byId, plan,"));
    std::string const avoid = Body(source, "static OverseerDecisions::RoutePlan AvoidDangerousZones(",
                                   "// A MEASURED STAGING CORRIDOR");
    Check("the zones are scored from the death memory, not read from config",
          Has(avoid, "ZoneDangerFor(bot, partyLevel)") && !Has(avoid, "sConfigMgr"));
    Check("the verdict is the rule's",
          Has(avoid, "AvoidDangerousRoute(") && Has(avoid, "FirstDangerousZone("));
    Check("and the travel log names the zone either way",
          Has(avoid, "ZoneAvoidanceLine(name, target, verdict, danger)") &&
              Has(avoid, "LOG_INFO(\"module.overseer\", \"{} (party level {})\", line") &&
              Has(avoid, "LOG_WARN(\"module.overseer\", \"{} (party level {})\", line"));
    std::string const around = Body(source, "static std::vector<uint32_t> PlanAroundZone(",
                                    "// ROUTED AROUND A ZONE");
    Check("the re-plan prices the zone on a copy of the links",
          Has(around, "std::vector<OverseerDecisions::RouteLink> priced = survey.links;") &&
              Has(around, "PlanFootRoute(survey.nodes, priced,"));
}

void TheFamilyPageCanReadTheList(std::string const& source)
{
    Check("DoProbe answers danger",
          Has(source, "else if (what == \"danger\")\n            out = ProbeDanger(bot);"));
    std::string const probe = Body(source, "static std::string ProbeDanger(Player* bot)", "static char const* DoProbe(");
    Check("the probe formats the list in the decisions module",
          Has(probe, "DangerProbeJson(") && Has(probe, "ZoneDangerFor(bot, partyLevel)") &&
              Has(probe, "Danger().pulls"));
}

}  // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    TheDeathsAreReadBackOnTheEngagementPoll(source);
    ABookedEntryIsNotPulled(source);
    TravelRoutesAroundADangerousZone(source);
    TheFamilyPageCanReadTheList(source);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_death_danger_memory_source: all passed\n");
    return 0;
}
