/*
 * A cleared guild run loots its last boss before it leaves, and a dry tank or
 * damage dealer does not sit the group down again straight after a rest that
 * timed out.
 *
 * Razorfen Kraul run 3390 (2026-10-30): Charlga Razorflank died at 22:22:44,
 * the members were sent out at :45 and the group was disbanded at :47, so
 * nobody reached the corpse. The same run rested 16 times in 43 minutes, 14 of
 * them running out the 120 s on a tank with nothing to drink.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

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

GuildRunClearedLootFacts Cleared(unsigned seconds)
{
    GuildRunClearedLootFacts facts;
    facts.secondsSinceCleared = seconds;
    facts.aliveInside = 5;
    return facts;
}

void TheLastBossIsLooted()
{
    Check("the poll after the kill stays", GuildRunClearedLootWaits(Cleared(1)));
    Check("the walk to the corpse is given its time",
          GuildRunClearedLootWaits(Cleared(GUILD_RUN_CLEARED_LOOT_MIN_SECONDS - 1)));
    Check("nothing left open: the group goes",
          !GuildRunClearedLootWaits(Cleared(GUILD_RUN_CLEARED_LOOT_MIN_SECONDS)));

    GuildRunClearedLootFacts roll = Cleared(GUILD_RUN_CLEARED_LOOT_MIN_SECONDS);
    roll.rollsOpen = true;
    Check("an open roll holds it", GuildRunClearedLootWaits(roll));
    roll.secondsSinceCleared = GUILD_RUN_CLEARED_LOOT_MAX_SECONDS;
    Check("but never past the roll timer", !GuildRunClearedLootWaits(roll));

    GuildRunClearedLootFacts fight = Cleared(GUILD_RUN_CLEARED_LOOT_MIN_SECONDS);
    fight.anyFighting = true;
    Check("a member still fighting holds it", GuildRunClearedLootWaits(fight));

    GuildRunClearedLootFacts dead = Cleared(1);
    dead.aliveInside = 0;
    Check("nobody alive inside: nothing to wait for", !GuildRunClearedLootWaits(dead));
}

GuildRunMemberShape Member(float health, float mana, bool healer)
{
    GuildRunMemberShape m;
    m.healthPct = health;
    m.manaPct = mana;
    m.usesMana = true;
    m.healer = healer;
    return m;
}

GuildRunHoldFacts JustTimedOut(GuildRunRestNeed need, bool vital)
{
    GuildRunHoldFacts facts;
    facts.armed = true;
    facts.aliveInside = 5;
    facts.livingResurrector = true;
    facts.need = need;
    facts.vitalLow = vital;
    facts.restRearmed = true;           // past the 45 s
    facts.casterRestRearmed = false;    // inside the longer window
    return facts;
}

void ADryTankDoesNotSitTheGroupDownAgain()
{
    std::vector<GuildRunMemberShape> dryTank{Member(100.f, 6.f, false), Member(100.f, 90.f, true)};
    Check("a dry tank is low", GuildRunRestNeedOf(dryTank) == GuildRunRestNeed::Low);
    Check("but not vital", !GuildRunRestVitalLow(dryTank));
    Check("a low healer is vital",
          GuildRunRestVitalLow({Member(100.f, 50.f, true)}));
    Check("a hurt member is vital",
          GuildRunRestVitalLow({Member(60.f, 90.f, false)}));

    GuildRunHold hold = GuildRunHoldNext(JustTimedOut(GuildRunRestNeed::Low, false));
    Check("run 3390's tank: the group keeps moving after the timed-out rest",
          hold.step == GuildRunHoldStep::Nothing);

    hold = GuildRunHoldNext(JustTimedOut(GuildRunRestNeed::Low, true));
    Check("a vital need still stops it",
          hold.step == GuildRunHoldStep::Hold && hold.why == GuildRunHoldWhy::Rest);

    GuildRunHoldFacts bodies = JustTimedOut(GuildRunRestNeed::Low, false);
    bodies.deadInside = 1;
    hold = GuildRunHoldNext(bodies);
    Check("a body inside does not restart the dry rest either",
          hold.step == GuildRunHoldStep::Nothing);

    GuildRunHoldFacts rearmed = JustTimedOut(GuildRunRestNeed::Low, false);
    rearmed.casterRestRearmed = true;
    hold = GuildRunHoldNext(rearmed);
    Check("past the window the dry tank rests again",
          hold.step == GuildRunHoldStep::Hold && hold.why == GuildRunHoldWhy::Rest);
}

std::string ReadSource()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadSource();
    std::size_t const wait = source.find("OverseerDecisions::GuildRunClearedLootWaits(loot)");
    std::size_t const end = source.find("EndGuildRun(run, OverseerDecisions::GuildRunVerdictWord(verdict), why, now);");
    Check("the loot wait comes before the run ends",
          wait != std::string::npos && end != std::string::npos && wait < end);
    Check("the rolls are read off the group",
          source.find("group->isRollLootActive()") != std::string::npos);
    Check("a timed-out rest arms the longer window",
          source.find("now + OverseerDecisions::GUILD_RUN_CASTER_REST_REARM_SECONDS") != std::string::npos);
    Check("the hold reads the window and the vital need",
          source.find("facts.casterRestRearmed = now >= run.casterRestRearmAt;") != std::string::npos &&
              source.find("OverseerDecisions::GuildRunRestVitalLow(living)") != std::string::npos);
}

}  // namespace

int main()
{
    TheLastBossIsLooted();
    ADryTankDoesNotSitTheGroupDownAgain();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild run last boss loot: all checks passed\n");
    return 0;
}
