/*
 * The family meter: damage, healing and damage taken per member over one
 * fight, a fight closing after METER_IDLE_SECONDS without an event, and the
 * probe's JSON with per-second rates and threat as a percent of the top.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::METER_IDLE_SECONDS;
using OverseerDecisions::MeterBook;
using OverseerDecisions::MeterKind;
using OverseerDecisions::MeterLine;
using OverseerDecisions::MeterLive;
using OverseerDecisions::MeterProbeJson;
using OverseerDecisions::MeterRecord;
using OverseerDecisions::MeterSeconds;

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

bool Has(std::string const& json, std::string const& part)
{
    return json.find(part) != std::string::npos;
}

void OneFightAddsUp()
{
    MeterBook book;
    MeterRecord(book, "Grug", MeterKind::Damage, 100, 1000);
    MeterRecord(book, "Grug", MeterKind::Taken, 40, 1002);
    MeterRecord(book, "Ugga", MeterKind::Healing, 60, 1004);
    MeterRecord(book, "Grug", MeterKind::Damage, 50, 1010);
    Check("damage adds up", book.current.by["Grug"].damage == 150);
    Check("taken is its own column", book.current.by["Grug"].taken == 40);
    Check("healing is its own column", book.current.by["Ugga"].healing == 60);
    Check("the fight is live inside the gap", MeterLive(book, 1010 + METER_IDLE_SECONDS));
    Check("a live fight runs to now", MeterSeconds(book, 1015) == 15);
}

void AGapClosesTheFight()
{
    MeterBook book;
    MeterRecord(book, "Grug", MeterKind::Damage, 100, 1000);
    MeterRecord(book, "Grug", MeterKind::Damage, 100, 1006);
    Check("over after the gap", !MeterLive(book, 1006 + METER_IDLE_SECONDS + 1));
    Check("a finished fight stops at its last event",
          MeterSeconds(book, 2000) == 6);
    MeterRecord(book, "Grug", MeterKind::Damage, 7, 1006 + METER_IDLE_SECONDS + 1);
    Check("the next event opens a new fight", book.current.by["Grug"].damage == 7);
    Check("the old fight is kept as previous", book.previous.by["Grug"].damage == 200);
}

void TheGapItselfIsStillOneFight()
{
    MeterBook book;
    MeterRecord(book, "Grug", MeterKind::Damage, 1, 1000);
    MeterRecord(book, "Grug", MeterKind::Damage, 1, 1000 + METER_IDLE_SECONDS);
    Check("exactly the gap keeps the fight", book.current.by["Grug"].damage == 2);
}

void NoFightIsOneSecond()
{
    MeterBook book;
    Check("an empty book is never live", !MeterLive(book, 5));
    Check("an empty book lasts one second, never zero", MeterSeconds(book, 5) == 1);
}

void TheJsonSaysRatesAndThreat()
{
    MeterLine tank;
    tank.name = "Grug";
    tank.totals.damage = 300;
    tank.totals.taken = 90;
    tank.threat = 500.0f;
    MeterLine healer;
    healer.name = "Ugga";
    healer.totals.healing = 150;
    healer.threat = 125.0f;
    MeterLine away;
    away.name = "Og";
    std::string const json =
        MeterProbeJson({tank, healer, away}, true, 30, "Skeletal \"Flayer\"", 500.0f);
    Check("live", Has(json, "\"live\":true"));
    Check("seconds", Has(json, "\"seconds\":30"));
    Check("the target is escaped", Has(json, "\"target\":\"Skeletal \\\"Flayer\\\"\""));
    Check("dps", Has(json, "\"name\":\"Grug\",\"damage\":300,\"dps\":10"));
    Check("hps", Has(json, "\"healing\":150,\"hps\":5"));
    Check("the tank holds all of it", Has(json, "\"threat\":500.0,\"threat_pct\":100"));
    Check("the healer a quarter", Has(json, "\"threat\":125.0,\"threat_pct\":25"));
    Check("no threat entry reads -1", Has(json, "\"threat\":-1.0,\"threat_pct\":-1"));
    Check("zero seconds never divides by zero",
          Has(MeterProbeJson({tank}, false, 0, "", 0.0f), "\"dps\":300"));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("DoProbe answers meter",
          Has(source, "else if (what == \"meter\")\n            out = ProbeMeter(bot);"));
    Check("the hooks are registered", Has(source, "new OverseerMeterScript();"));
    Check("both hooks are enabled",
          Has(source, "UNITHOOK_ON_DAMAGE,\n        UNITHOOK_ON_HEAL,"));
    Check("the seats refresh on the party poll", Has(source, "RefreshMeterSeats(rosters);"));
}

}  // namespace

int main()
{
    OneFightAddsUp();
    AGapClosesTheFight();
    TheGapItselfIsStillOneFight();
    NoFightIsOneSecond();
    TheJsonSaysRatesAndThreat();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_family_meter\n");
    return 0;
}
