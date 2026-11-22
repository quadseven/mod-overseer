/*
 * The town-trip loop: a family leader started its walk to the vendor, a fetch
 * or regroup for a far member took it off, the errand ended, the bridge wrote
 * it again, and nobody ever sold. Two rules end it:
 *
 *  1. A vendor errand the bridge wrote after it left the held member behind
 *     outranks a fetch or a regroup, and takes the leader at once.
 *  2. A no-progress backoff that lapses does not restart its count while the
 *     same urgency still stands.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

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

LeaderIntentRequest Req(LeaderIntentKind kind, char const* owner, char const* target,
                        bool leftBehind = false)
{
    LeaderIntentRequest request;
    request.kind = kind;
    request.owner = owner;
    request.target = target;
    request.why = "test";
    request.leftMemberBehind = leftBehind;
    return request;
}

LeaderIntentLimits const LIMITS{};

// The measured cycle, replayed with the old behaviour (flag off): the errand
// takes the leader, the fetch takes it back after the dwell, the errand ends.
void WithoutTheBoundTheFetchTakesTheLeaderBack()
{
    LeaderIntentState state;
    auto v = AskLeaderIntent(state, Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor"),
                             1000, LIMITS);
    Check("the errand starts", v.answer == LeaderIntentAnswer::Granted);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), 1100,
                        LIMITS);
    Check("an unbounded fetch takes the leader off the errand",
          v.answer == LeaderIntentAnswer::Granted && v.replaced == LeaderIntentKind::EconomyErrand);
}

void ABridgeErrandOutranksAFetchForTheMemberItLeftBehind()
{
    LeaderIntentState state;
    LeaderIntentRequest const errand =
        Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true);
    auto v = AskLeaderIntent(state, errand, 1000, LIMITS);
    Check("the errand starts", v.answer == LeaderIntentAnswer::Granted);
    // Well past the dwell, as in the log, and the fetch asks every poll.
    for (std::time_t t = 1100; t < 1000 + 3600; t += 30)
    {
        v = AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), t,
                            LIMITS);
        Check("the fetch waits behind the errand", v.answer == LeaderIntentAnswer::Deferred);
        v = AskLeaderIntent(state, errand, t, LIMITS);
        Check("the errand keeps the leader", v.answer == LeaderIntentAnswer::Renewed);
    }
    Check("the leader never changed hands again", state.changes == 1);
}

void ABridgeErrandTakesTheLeaderFromAFetchAtOnce()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), 1000, LIMITS);
    auto v = AskLeaderIntent(
        state, Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true), 1005,
        LIMITS);
    Check("it takes the leader inside the fetch's dwell",
          v.answer == LeaderIntentAnswer::Granted && v.replaced == LeaderIntentKind::FetchMember);

    LeaderIntentState regroup;
    AskLeaderIntent(regroup, Req(LeaderIntentKind::Regroup, "regroup", ""), 1000, LIMITS);
    v = AskLeaderIntent(regroup,
                        Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true), 1005,
                        LIMITS);
    Check("and from a regroup", v.answer == LeaderIntentAnswer::Granted);
}

void WhatIsNotTheBridgesBoundStillRanksAsBefore()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), 1000, LIMITS);
    auto v = AskLeaderIntent(state, Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor"),
                             1005, LIMITS);
    Check("an errand with no bound waits behind a fetch", v.answer == LeaderIntentAnswer::Deferred);

    LeaderIntentState hearth;
    AskLeaderIntent(hearth, Req(LeaderIntentKind::Hearth, "home errand", "inn"), 1000, LIMITS);
    v = AskLeaderIntent(hearth,
                        Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true), 1500,
                        LIMITS);
    Check("the family's trip home still outranks it", v.answer == LeaderIntentAnswer::Deferred);

    LeaderIntentState run;
    AskLeaderIntent(run, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 1000, LIMITS);
    v = AskLeaderIntent(run, Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true),
                        1500, LIMITS);
    Check("and so does a dungeon run", v.answer == LeaderIntentAnswer::Deferred);
}

// The whole cycle to arrival: take-over, fetch, ended, repeat - then arrival.
void TheCycleEndsInArrival()
{
    LeaderIntentState state;
    LeaderIntentRequest const errand =
        Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true);
    unsigned takeOvers = 0;
    bool arrived = false;
    for (std::time_t t = 0; t < 40 * 60 && !arrived; t += 30)
    {
        // The fetch asks first each poll, as the drive order does.
        AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), t,
                        LIMITS);
        auto const v = AskLeaderIntent(state, errand, t, LIMITS);
        if (v.answer == LeaderIntentAnswer::Granted)
            ++takeOvers;
        if (v.answer != LeaderIntentAnswer::Deferred && t >= 10 * 60)
            arrived = EndLeaderIntent(state, LeaderIntentKind::EconomyErrand, "travel column",
                                      LeaderIntentEnd::Completed, t, LIMITS);
    }
    Check("the errand took the leader once", takeOvers == 1);
    Check("and ended by arrival", arrived);
}

void TheBackoffCountDoesNotRestartWhileTheSameUrgencyStands()
{
    TravelNoProgressBackoffState state;
    std::string const urgency = "7|fetch|far member";
    Check("release 1", TravelNoProgressBackoffStep(state, 100, 3, 900, urgency) ==
                           TravelNoProgressBackoffAction::Continue);
    Check("release 2", TravelNoProgressBackoffStep(state, 160, 3, 900, urgency) ==
                           TravelNoProgressBackoffAction::Continue);
    Check("release 3 backs off", TravelNoProgressBackoffStep(state, 220, 3, 900, urgency) ==
                                     TravelNoProgressBackoffAction::Backoff);
    Check("held while it lasts", TravelNoProgressBackoffStep(state, 500, 3, 900, urgency) ==
                                     TravelNoProgressBackoffAction::Backoff);
    // Lapsed, same urgency: the next release backs off, not a fresh count of 1.
    Check("the same urgency does not buy three fresh releases",
          TravelNoProgressBackoffStep(state, 1200, 3, 900, urgency) ==
              TravelNoProgressBackoffAction::Backoff);
}

void ADifferentOrNoUrgencyStartsTheCountAgain()
{
    TravelNoProgressBackoffState state;
    TravelNoProgressBackoffStep(state, 100, 3, 900, "7|fetch|a");
    TravelNoProgressBackoffStep(state, 160, 3, 900, "7|fetch|a");
    TravelNoProgressBackoffStep(state, 220, 3, 900, "7|fetch|a");
    Check("another urgency starts again",
          TravelNoProgressBackoffStep(state, 1200, 3, 900, "6|regroup|") ==
              TravelNoProgressBackoffAction::Continue);

    TravelNoProgressBackoffState plain;
    TravelNoProgressBackoffStep(plain, 100, 3, 900);
    TravelNoProgressBackoffStep(plain, 160, 3, 900);
    TravelNoProgressBackoffStep(plain, 220, 3, 900);
    Check("no urgency starts again, as it always did",
          TravelNoProgressBackoffStep(plain, 1200, 3, 900) ==
              TravelNoProgressBackoffAction::Continue);
}

}  // namespace

int main()
{
    WithoutTheBoundTheFetchTakesTheLeaderBack();
    ABridgeErrandOutranksAFetchForTheMemberItLeftBehind();
    ABridgeErrandTakesTheLeaderFromAFetchAtOnce();
    WhatIsNotTheBridgesBoundStillRanksAsBefore();
    TheCycleEndsInArrival();
    TheBackoffCountDoesNotRestartWhileTheSameUrgencyStands();
    ADifferentOrNoUrgencyStartsTheCountAgain();
    if (failures)
        return 1;
    std::printf("town trip cycle: ok\n");
    return 0;
}
