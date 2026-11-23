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
    for (std::time_t t = 1030; t < 1100; t += 30)
        AskLeaderIntent(state, Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor"), t,
                        LIMITS);
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
    // Past the dwell, as in the log, and both ask every poll.
    for (std::time_t t = 1030; t < 1000 + 3600; t += 30)
    {
        v = AskLeaderIntent(state, errand, t, LIMITS);
        Check("the errand keeps the leader", v.answer == LeaderIntentAnswer::Renewed);
        v = AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "far member"), t,
                            LIMITS);
        Check("the fetch waits behind the errand", v.answer == LeaderIntentAnswer::Deferred);
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
    for (std::time_t t = 1000; t <= 1500; t += 30)
        AskLeaderIntent(hearth, Req(LeaderIntentKind::Hearth, "home errand", "inn"), t, LIMITS);
    v = AskLeaderIntent(hearth,
                        Req(LeaderIntentKind::EconomyErrand, "travel column", "vendor", true), 1500,
                        LIMITS);
    Check("the family's trip home still outranks it", v.answer == LeaderIntentAnswer::Deferred);

    LeaderIntentState run;
    for (std::time_t t = 1000; t <= 1500; t += 30)
        AskLeaderIntent(run, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), t, LIMITS);
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

// The measured cycle of a leader that cannot path (off the mesh): each walk is
// released for no progress after 66s, the trip is claimed again 5s later, three
// releases back off for 15 minutes, and the lapse used to hand back a fresh
// count of three. Returns the walks begun in an hour; `arrivedAfterMove` says
// whether the walk the character makes once it has been moved off the spot is
// let through to arrive.
unsigned WalksInAnHourFromOneSpot(bool giveFeet, bool& arrivedAfterMove)
{
    TravelNoProgressBackoffState state;
    TravelFeet const feet{-6265.f, -97.f};
    unsigned walks = 0;
    std::time_t t = 0;
    while (t < 3600)
    {
        ++walks;
        t += 66;
        auto const action =
            TravelNoProgressBackoffStep(state, t, 3, 900, "", giveFeet ? &feet : nullptr);
        t += action == TravelNoProgressBackoffAction::Backoff ? 900 : 5;
    }
    // The stall recovery then moves the character far from that spot. Its next
    // walk is not held by the old count, and it arrives (no further release).
    TravelFeet const moved{-618.f, -4251.f};
    t += 900;
    auto const action = TravelNoProgressBackoffStep(state, t, 3, 900, "", giveFeet ? &moved : nullptr);
    arrivedAfterMove = action == TravelNoProgressBackoffAction::Continue;
    return walks;
}

void AWalkThatNeverPathsDoesNotTakeTheLeaderNineTimesAnHour()
{
    bool arrived = false;
    unsigned const before = WalksInAnHourFromOneSpot(false, arrived);
    Check("the measured cycle: ten or more walks an hour without the spot", before >= 9);
    unsigned const after = WalksInAnHourFromOneSpot(true, arrived);
    Check("with the spot known only the first backoff buys three walks", after <= 6 && after + 3 <= before);
    Check("and once the character is moved its walk is let through to arrive", arrived);
}

void AWalkerThatMovedStartsTheCountAgain()
{
    TravelNoProgressBackoffState state;
    TravelFeet const here{0.f, 0.f};
    TravelFeet const elsewhere{500.f, 0.f};
    TravelNoProgressBackoffStep(state, 100, 3, 900, "", &here);
    TravelNoProgressBackoffStep(state, 160, 3, 900, "", &here);
    TravelNoProgressBackoffStep(state, 220, 3, 900, "", &here);
    Check("lapsed on the same spot backs off at once",
          TravelNoProgressBackoffStep(state, 1200, 3, 900, "", &here) ==
              TravelNoProgressBackoffAction::Backoff);
    TravelNoProgressBackoffStep(state, 2200, 3, 900, "", &here);
    Check("lapsed after moving counts afresh",
          TravelNoProgressBackoffStep(state, 3200, 3, 900, "", &elsewhere) ==
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
    AWalkThatNeverPathsDoesNotTakeTheLeaderNineTimesAnHour();
    AWalkerThatMovedStartsTheCountAgain();
    if (failures)
        return 1;
    std::printf("town trip cycle: ok\n");
    return 0;
}
