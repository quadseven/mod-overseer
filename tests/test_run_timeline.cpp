/* The dungeon run timeline is a diff of two coordinator snapshots, so what it
 * writes, and what it must NOT write, is tested here without a worldserver. */
#include "overseer_decisions.h"
#include <cstdio>
#include <string>

using OverseerDecisions::DcOnMark;
using OverseerDecisions::DcOnTimelineEvents;
using OverseerDecisions::RunTimelineDetail;
using OverseerDecisions::RunTimelineEvents;
using OverseerDecisions::RunTimelineSnapshot;

int main()
{
    int failures = 0;
    auto check = [&](char const* name, bool ok) {
        if (!ok)
        {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    };

    RunTimelineSnapshot gathering;
    gathering.phase = "GATHERING";
    gathering.campaignId = 8;
    gathering.runNumber = 2;
    gathering.portal = "ragefire";

    // Nothing changed: nothing is written. This is the volume bound.
    check("an unchanged poll writes nothing", RunTimelineEvents(gathering, gathering).empty());

    // A phase change is one row, stamped with the run.
    RunTimelineSnapshot barrier = gathering;
    barrier.phase = "BARRIER";
    auto events = RunTimelineEvents(gathering, barrier);
    check("a phase change is one row", events.size() == 1);
    check("the row is a phase row", !events.empty() && events[0].kind == "phase");
    check("the row names both phases",
          !events.empty() && events[0].detail == "GATHERING -> BARRIER");
    check("the row carries the run number", !events.empty() && events[0].runNumber == 2);
    check("the row carries the campaign", !events.empty() && events[0].campaignId == 8);

    // A run opening from IDLE names where it is going.
    RunTimelineSnapshot idle;
    RunTimelineSnapshot resetting = gathering;
    resetting.phase = "RESET";
    events = RunTimelineEvents(idle, resetting);
    check("a run opening is one row", events.size() == 1);
    check("a run opening names the portal and run",
          !events.empty() &&
              events[0].detail == "IDLE -> RESET (ragefire, run 2 of campaign 8)");

    // A run that ends resets the coordinator; the row belongs to the run that ended.
    RunTimelineSnapshot clearing = gathering;
    clearing.phase = "CLEARING";
    clearing.runId = 151533;
    events = RunTimelineEvents(clearing, idle);
    check("a run ending is one row", events.size() == 1);
    check("a run ending is stamped with the ended run",
          !events.empty() && events[0].runId == 151533 && events[0].runNumber == 2);
    check("a run ending lands in IDLE", !events.empty() && events[0].phase == "IDLE");

    // Decisions inside CLEARING: a boss, a skip, a stall - each once.
    RunTimelineSnapshot later = clearing;
    later.clearEncounters = 0x3;
    later.clearSkips = 1;
    later.stalledReason = "no boss credited in 10 minutes";
    RunTimelineSnapshot earlier = clearing;
    earlier.clearEncounters = 0x1;
    events = RunTimelineEvents(earlier, later);
    check("three decisions are three rows", events.size() == 3);
    check("a boss credited says one of two",
          events.size() == 3 && events[0].kind == "boss" &&
              events[0].detail == "1 encounter credited, 2 in all (mask 1 -> 3)");
    check("a skip is recorded", events.size() == 3 && events[1].kind == "dc_skip");
    check("a stall carries its reason as the detail",
          events.size() == 3 && events[2].kind == "stalled" &&
              events[2].detail == "no boss credited in 10 minutes");
    check("the same stall is not written twice", RunTimelineEvents(later, later).empty());

    // A mask that loses bits is not a boss credited.
    RunTimelineSnapshot fewer = clearing;
    fewer.clearEncounters = 0x1;
    RunTimelineSnapshot more = clearing;
    more.clearEncounters = 0x3;
    check("a mask losing bits writes nothing", RunTimelineEvents(more, fewer).empty());

    // A flag the coordinator raises once per stretch is written once.
    RunTimelineSnapshot accepted = clearing;
    accepted.dcAcceptedAll = true;
    events = RunTimelineEvents(clearing, accepted);
    check("dc on accepted for all is one row",
          events.size() == 1 && events[0].kind == "dc_on_all");
    check("a flag going down writes nothing", RunTimelineEvents(accepted, clearing).empty());

    // A fresh coordinator after a re-arm zeroes every counter: that is not a
    // decision, it is a new run, and only the phase change is written.
    RunTimelineSnapshot rearmed;
    rearmed.phase = "REPAIRING";
    rearmed.campaignId = 8;
    rearmed.runNumber = 3;
    rearmed.portal = "ragefire";
    events = RunTimelineEvents(later, rearmed);
    check("a re-arm is only its phase row", events.size() == 1 && events[0].kind == "phase");
    check("a re-arm row belongs to the new run", events.size() == 1 && events[0].runNumber == 3);

    // Adoption fills in the run id without it being a different run.
    RunTimelineSnapshot staged = gathering;
    staged.phase = "STAGED_INSIDE";
    RunTimelineSnapshot adopted = staged;
    adopted.runId = 42;
    adopted.stagingRearms = 1;
    events = RunTimelineEvents(staged, adopted);
    check("an id filled in keeps the run's decisions",
          events.size() == 1 && events[0].kind == "staging_rearm" && events[0].runId == 42);

    // The first reset attempt is the phase itself; only a retry is news.
    RunTimelineSnapshot reset1 = resetting;
    reset1.resetAttempts = 1;
    check("the first reset attempt is not a row", RunTimelineEvents(resetting, reset1).empty());
    RunTimelineSnapshot reset2 = resetting;
    reset2.resetAttempts = 2;
    events = RunTimelineEvents(reset1, reset2);
    check("a reset retry is a row", events.size() == 1 && events[0].kind == "reset_retry");

    // Detail fits the column.
    std::string const long_text(600, 'x');
    std::string const cut = RunTimelineDetail(long_text);
    check("a long detail is cut to the column", cut.size() == 500);
    check("a cut detail says so", cut.substr(497) == "...");
    check("a short detail is untouched", RunTimelineDetail("abc") == "abc");

    // dc on memory.
    std::map<std::string, DcOnMark> before;
    std::map<std::string, DcOnMark> after;
    check("no dc on memory writes nothing", DcOnTimelineEvents(before, after).empty());

    DcOnMark onAccepted;
    onAccepted.runId = 7;
    onAccepted.accepted = true;
    onAccepted.issuedAt = 1000;
    after["Zug"] = onAccepted;
    auto dc = DcOnTimelineEvents(before, after);
    check("an accepted dc on is one row", dc.size() == 1 && dc[0].kind == "dc_on" &&
                                                dc[0].name == "Zug" && dc[0].runId == 7);
    check("the same dc on is not written twice", DcOnTimelineEvents(after, after).empty());

    DcOnMark refused;
    refused.runId = 7;
    refused.issuedAt = 1000;
    refused.loggedRefused = true;
    std::map<std::string, DcOnMark> r1{{"Oz", refused}};
    dc = DcOnTimelineEvents({}, r1);
    check("a refusal is one row", dc.size() == 1 && dc[0].kind == "dc_on_refused");
    DcOnMark retried = refused;
    retried.issuedAt = 1060;
    std::map<std::string, DcOnMark> r2{{"Oz", retried}};
    check("a refused retry is not another row", DcOnTimelineEvents(r1, r2).empty());
    DcOnMark healed = retried;
    healed.issuedAt = 1120;
    healed.accepted = true;
    healed.loggedRefused = false;
    std::map<std::string, DcOnMark> r3{{"Oz", healed}};
    dc = DcOnTimelineEvents(r2, r3);
    check("a refusal that heals is one dc_on row", dc.size() == 1 && dc[0].kind == "dc_on");

    DcOnMark reissued = onAccepted;
    reissued.issuedAt = 2000;
    std::map<std::string, DcOnMark> again{{"Zug", reissued}};
    check("an accepted re-issue in the same run is not another row",
          DcOnTimelineEvents(after, again).empty());
    DcOnMark nextRun = onAccepted;
    nextRun.runId = 8;
    std::map<std::string, DcOnMark> next{{"Zug", nextRun}};
    dc = DcOnTimelineEvents(after, next);
    check("an acceptance for a new run is a row", dc.size() == 1 && dc[0].kind == "dc_on");

    DcOnMark down = onAccepted;
    down.stoodDown = true;
    std::map<std::string, DcOnMark> d{{"Zug", down}};
    dc = DcOnTimelineEvents(after, d);
    check("a dc off is one row", dc.size() == 1 && dc[0].kind == "dc_off");
    std::map<std::string, DcOnMark> rearmed_dc{{"Zug", onAccepted}};
    dc = DcOnTimelineEvents(d, rearmed_dc);
    check("armed again after a dc off is a row", dc.size() == 1 && dc[0].kind == "dc_on");

    if (failures == 0)
        std::printf("ok test_run_timeline\n");
    return failures;
}
