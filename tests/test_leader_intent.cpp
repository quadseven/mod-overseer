/*
 * One movement owner per family leader.
 *
 * Measured on the dev realm, 2026-09-26 02:07-02:14 UTC: while the operator
 * watched the leader's stream and called his walking "stutter steps", at least
 * six rules took turns steering one leader. The bridge handed him `follow`,
 * the regroup hold took it off, the hold was placed and lifted 30 s apart, a
 * banker errand was released 15 s after it was issued, the quest rule granted
 * `new rpg` and took it off a minute later, and a walk back for a member
 * 6,400 yards away was started and refused inside a minute. About 22
 * movement handoffs in ten minutes.
 *
 * These tests hold the book every rule now asks first, and replay the
 * requests of that window through it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::AskLeaderIntent;
using OverseerDecisions::EndLeaderIntent;
using OverseerDecisions::LeaderIntentAnswer;
using OverseerDecisions::LeaderIntentEnd;
using OverseerDecisions::LeaderIntentFree;
using OverseerDecisions::LeaderIntentHeldBy;
using OverseerDecisions::LeaderIntentKind;
using OverseerDecisions::LeaderIntentKindWord;
using OverseerDecisions::LeaderIntentLimits;
using OverseerDecisions::LeaderIntentPreference;
using OverseerDecisions::LeaderIntentRequest;
using OverseerDecisions::LeaderIntentsOnTheTable;
using OverseerDecisions::LeaderIntentState;
using OverseerDecisions::LeaderIntentVerdict;
using OverseerDecisions::LeaderIntentWalksUnderNewRpg;
using OverseerDecisions::ParseLeaderIntentKind;
using OverseerDecisions::WaiveLeaderIntentDwell;

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

LeaderIntentRequest Req(LeaderIntentKind kind, char const* owner, char const* target = "")
{
    LeaderIntentRequest request;
    request.kind = kind;
    request.owner = owner;
    request.target = target;
    request.why = "test";
    return request;
}

LeaderIntentLimits const LIMITS{};  // dwell 90, flap 90, stale 75, operator 600

void AFreeLeaderIsGrantedAndRenewed()
{
    LeaderIntentState state;
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 100,
                        LIMITS);
    Check("a free leader is granted", v.answer == LeaderIntentAnswer::Granted);
    Check("nothing was replaced", v.replaced == LeaderIntentKind::None);
    Check("one change", state.changes == 1);

    v = AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 115,
                        LIMITS);
    Check("the same ask renews", v.answer == LeaderIntentAnswer::Renewed);
    Check("a renewal is not a change", state.changes == 1);

    v = AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "vendor"), 130,
                        LIMITS);
    Check("a new target from the same owner retargets",
          v.answer == LeaderIntentAnswer::Retargeted);
    Check("a retarget is not a change", state.changes == 1);
    Check("the retarget is recorded", state.target == "vendor");
}

void ALowerRuleWaitsForTheCurrentOne()
{
    LeaderIntentState state;
    // Asked every party poll, well past the dwell: a lower rank still waits,
    // because it waits for the current intent to END, not for its dwell.
    for (int t = 0; t <= 480; t += 30)
        AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), t, LIMITS);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 500,
                        LIMITS);
    Check("a lower rank is deferred", v.answer == LeaderIntentAnswer::Deferred);
    Check("the holder is named", v.holder == "regroup");
    Check("the deferral is said the first time", v.sayIt);
    AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 510, LIMITS);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 515,
                        LIMITS);
    Check("still deferred", v.answer == LeaderIntentAnswer::Deferred);
    Check("the same deferral is not said twice", !v.sayIt);
    Check("the regroup still holds him",
          LeaderIntentHeldBy(state, LeaderIntentKind::Regroup, "regroup", 515, LIMITS));
}

void AHigherRuleWaitsForTheDwell()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 1000,
                    LIMITS);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "Og"), 1030, LIMITS);
    Check("a higher rank inside the dwell is deferred",
          v.answer == LeaderIntentAnswer::Deferred);
    Check("it is told how long the dwell has left", v.waitSeconds == 60);
    AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 1060,
                    LIMITS);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "Og"), 1090, LIMITS);
    Check("a higher rank after the dwell is granted", v.answer == LeaderIntentAnswer::Granted);
    Check("it names what it replaced", v.replaced == LeaderIntentKind::TownErrand &&
                                           v.replacedOwner == "travel column");
    Check("two changes", state.changes == 2);
}

void DeathOrCombatWaivesTheDwell()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 0,
                    LIMITS);
    WaiveLeaderIntentDwell(state);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::Hearth, "home errand", "inn"), 5, LIMITS);
    Check("combat or death lets a higher rank in at once",
          v.answer == LeaderIntentAnswer::Granted);
}

void TheCampaignAndTheOperatorNeverWait()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::Hearth, "home errand", "inn"), 0, LIMITS);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 1, LIMITS);
    Check("the campaign's run takes him inside the dwell",
          v.answer == LeaderIntentAnswer::Granted);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::OperatorOrder, "operator", "stay"), 2,
                        LIMITS);
    Check("an operator order takes him from the run", v.answer == LeaderIntentAnswer::Granted);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 3, LIMITS);
    Check("the run waits for the operator", v.answer == LeaderIntentAnswer::Deferred);
    // The operator's order has no rule renewing it and still holds him well
    // past the ordinary staleness.
    v = AskLeaderIntent(state, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 300,
                        LIMITS);
    Check("the operator order outlives an ordinary staleness",
          v.answer == LeaderIntentAnswer::Deferred);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 603,
                        LIMITS);
    Check("and ends after its own lifetime", v.answer == LeaderIntentAnswer::Granted);
}

void AnOwnerThatStopsAskingLetsGo()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 0, LIMITS);
    Check("held while fresh", !LeaderIntentFree(state, 60, LIMITS));
    Check("free once stale", LeaderIntentFree(state, 76, LIMITS));
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::QuestDrive, "quest drive"), 76, LIMITS);
    Check("anything is granted over a stale intent", v.answer == LeaderIntentAnswer::Granted);
    Check("a stale intent is not reported as replaced", v.replaced == LeaderIntentKind::None);
}

void OnlyTheOwnerEndsItsIntent()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 0,
                    LIMITS);
    Check("another rule cannot end it",
          !EndLeaderIntent(state, LeaderIntentKind::TownErrand, "fetch", LeaderIntentEnd::Failed,
                           10, LIMITS));
    Check("nor can the owner end a kind it does not hold",
          !EndLeaderIntent(state, LeaderIntentKind::Regroup, "travel column",
                           LeaderIntentEnd::Failed, 10, LIMITS));
    Check("the owner ends it",
          EndLeaderIntent(state, LeaderIntentKind::TownErrand, "travel column",
                          LeaderIntentEnd::Completed, 10, LIMITS));
    Check("free after the end", LeaderIntentFree(state, 11, LIMITS));
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "vendor"), 12,
                        LIMITS);
    Check("a completed errand leaves no cooldown", v.answer == LeaderIntentAnswer::Granted);
}

void AnEarlyChangeOfMindCoolsOff()
{
    LeaderIntentState state;
    // The regroup hold of the measured window: placed, lifted 30 s later
    // because the heuristic changed its mind, placed again 90 s after that.
    AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Grog"), 0, LIMITS);
    EndLeaderIntent(state, LeaderIntentKind::Regroup, "regroup", LeaderIntentEnd::Abandoned, 30,
                    LIMITS);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 60, LIMITS);
    Check("the same kind is cooled off after an early abandon",
          v.answer == LeaderIntentAnswer::Deferred && v.waitSeconds == 60);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 61,
                        LIMITS);
    Check("another kind is not", v.answer == LeaderIntentAnswer::Granted);
    LeaderIntentState run;
    AskLeaderIntent(run, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 0, LIMITS);
    EndLeaderIntent(run, LeaderIntentKind::DungeonRun, "dungeon run", LeaderIntentEnd::Abandoned,
                    10, LIMITS);
    v = AskLeaderIntent(run, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 20, LIMITS);
    Check("the campaign is never cooled off", v.answer == LeaderIntentAnswer::Granted);
    LeaderIntentState late;
    AskLeaderIntent(late, Req(LeaderIntentKind::Regroup, "regroup", "Grog"), 0, LIMITS);
    AskLeaderIntent(late, Req(LeaderIntentKind::Regroup, "regroup", "Grog"), 60, LIMITS);
    EndLeaderIntent(late, LeaderIntentKind::Regroup, "regroup", LeaderIntentEnd::Abandoned, 120,
                    LIMITS);
    v = AskLeaderIntent(late, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 121, LIMITS);
    Check("an abandon after the dwell is not a flap", v.answer == LeaderIntentAnswer::Granted);
}

void JevsPickOutranksTheHeuristicsButNotTheCampaign()
{
    LeaderIntentPreference jev;
    jev.kind = LeaderIntentKind::TownErrand;
    jev.target = "banker";
    jev.until = 1000;
    LeaderIntentState state;
    for (int t = 0; t <= 195; t += 15)
        AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), t,
                        LIMITS, jev);
    LeaderIntentVerdict v =
        AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 200, LIMITS, jev);
    Check("Jev's errand keeps the leader from a regroup",
          v.answer == LeaderIntentAnswer::Deferred);
    v = AskLeaderIntent(state, Req(LeaderIntentKind::DungeonRun, "dungeon run", "door"), 201,
                        LIMITS, jev);
    Check("the campaign still takes him", v.answer == LeaderIntentAnswer::Granted);

    LeaderIntentState other;
    for (int t = 0; t <= 195; t += 15)
        AskLeaderIntent(other, Req(LeaderIntentKind::TownErrand, "travel column", "vendor"), t,
                        LIMITS, jev);
    v = AskLeaderIntent(other, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 200, LIMITS, jev);
    Check("a pick names its target: another errand is not raised",
          v.answer == LeaderIntentAnswer::Granted);

    LeaderIntentState expired;
    for (int t = 1000; t <= 1095; t += 15)
        AskLeaderIntent(expired, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), t,
                        LIMITS, jev);
    v = AskLeaderIntent(expired, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 1100, LIMITS,
                        jev);
    Check("an expired pick falls back to the static order",
          v.answer == LeaderIntentAnswer::Granted);
}

void TheTableListsWhatIsAsked()
{
    LeaderIntentState state;
    AskLeaderIntent(state, Req(LeaderIntentKind::Regroup, "regroup", "Ugga"), 0, LIMITS);
    AskLeaderIntent(state, Req(LeaderIntentKind::TownErrand, "travel column", "banker"), 5,
                    LIMITS);
    AskLeaderIntent(state, Req(LeaderIntentKind::FetchMember, "fetch", "Og"), 10, LIMITS);
    std::vector<LeaderIntentRequest> table = LeaderIntentsOnTheTable(state, 20, LIMITS);
    Check("three on the table", table.size() == 3);
    Check("highest rank first", table.size() == 3 &&
                                    table[0].kind == LeaderIntentKind::FetchMember &&
                                    table[1].kind == LeaderIntentKind::Regroup &&
                                    table[2].kind == LeaderIntentKind::TownErrand);
    table = LeaderIntentsOnTheTable(state, 82, LIMITS);
    Check("asks age off the table", table.size() == 1 &&
                                        table[0].kind == LeaderIntentKind::FetchMember);
}

void WordsRoundTrip()
{
    for (int k = 0; k <= static_cast<int>(LeaderIntentKind::OperatorOrder); ++k)
    {
        LeaderIntentKind const kind = static_cast<LeaderIntentKind>(k);
        Check("every kind round-trips through its word",
              ParseLeaderIntentKind(LeaderIntentKindWord(kind)) == kind);
    }
    Check("unknown words are none", ParseLeaderIntentKind("teleport") == LeaderIntentKind::None);
    Check("words are case-blind", ParseLeaderIntentKind(" Errand ") ==
                                      LeaderIntentKind::TownErrand);
    Check("regroup is held still", !LeaderIntentWalksUnderNewRpg(LeaderIntentKind::Regroup));
    Check("an errand walks", LeaderIntentWalksUnderNewRpg(LeaderIntentKind::TownErrand));
}

// THE MEASURED WINDOW, REPLAYED. Each line is a request the module's rules
// made for the leader in the worldserver log of 2026-09-26 02:07-02:14 UTC,
// at the second it was made, in the new world where every rule asks first.
// The old world carried out every one of them: 22 movement handoffs in ten
// minutes by count of the log's own lines. Two things change the requests
// themselves, and both are in this change: the bridge's `follow` pushes are
// refused (LeaderCommandRefusal, not an intent at all), and the banker walk
// is not released at 15 s on "5 tries" because the errand reads progress
// over a window - so it keeps asking instead of failing.
struct Event
{
    int t;  // seconds after 02:07:00
    bool end;
    LeaderIntentKind kind;
    char const* owner;
    char const* target;
    LeaderIntentEnd how;
};

void TheMeasuredWindowSettles()
{
    using K = LeaderIntentKind;
    using E = LeaderIntentEnd;
    std::vector<Event> events = {
        // The regroup for Grog was already holding him when the window opens.
        {7, false, K::Regroup, "regroup", "Grog", E::Completed},
        {37, false, K::Regroup, "regroup", "Grog", E::Completed},
        // 02:08:46 the banker errand is in the column and asks each 15 s poll.
        {106, false, K::TownErrand, "travel column", "banker", E::Completed},
        {67, false, K::Regroup, "regroup", "Grog", E::Completed},
        {97, false, K::Regroup, "regroup", "Grog", E::Completed},
        {121, false, K::TownErrand, "travel column", "banker", E::Completed},
        {127, false, K::Regroup, "regroup", "Grog", E::Completed},
        {136, false, K::TownErrand, "travel column", "banker", E::Completed},
        {151, false, K::TownErrand, "travel column", "banker", E::Completed},
        {157, false, K::Regroup, "regroup", "Grog", E::Completed},
        {166, false, K::TownErrand, "travel column", "banker", E::Completed},
        {181, false, K::TownErrand, "travel column", "banker", E::Completed},
        {187, false, K::Regroup, "regroup", "Grog", E::Completed},
        {196, false, K::TownErrand, "travel column", "banker", E::Completed},
        {211, false, K::TownErrand, "travel column", "banker", E::Completed},
        {217, false, K::Regroup, "regroup", "Grog", E::Completed},
        {226, false, K::TownErrand, "travel column", "banker", E::Completed},
        {241, false, K::TownErrand, "travel column", "banker", E::Completed},
        {247, false, K::Regroup, "regroup", "Grog", E::Completed},
        {256, false, K::TownErrand, "travel column", "banker", E::Completed},
        {271, false, K::TownErrand, "travel column", "banker", E::Completed},
        {277, false, K::Regroup, "regroup", "Grog", E::Completed},
        // 02:11:37 Grog is back within 25 yards: the regroup is done.
        {277 + 30, true, K::Regroup, "regroup", "Grog", E::Completed},
        {301, false, K::TownErrand, "travel column", "banker", E::Completed},
        // 02:11:41 onward the banker walk runs; it no longer fails at 15 s.
        {316, false, K::TownErrand, "travel column", "banker", E::Completed},
        // 02:12:07 the fetch for Og: the column holds a foreign errand, so the
        // fetch is not started (its claim would be refused) - no request.
        {331, false, K::TownErrand, "travel column", "banker", E::Completed},
        {346, false, K::TownErrand, "travel column", "banker", E::Completed},
        {361, false, K::TownErrand, "travel column", "banker", E::Completed},
        // 02:13:07 a regroup for Ugga, 4,863 yards back.
        {367, false, K::Regroup, "regroup", "Ugga", E::Completed},
        {376, false, K::TownErrand, "travel column", "banker", E::Completed},
        {391, false, K::TownErrand, "travel column", "banker", E::Completed},
        // 02:13:37 the regroup gives Ugga up: nobody walks back under a
        // catch-up aim. Not the holder, so this end changes nothing.
        {397, true, K::Regroup, "regroup", "Ugga", E::Abandoned},
        {406, false, K::TownErrand, "travel column", "banker", E::Completed},
        {421, false, K::TownErrand, "travel column", "banker", E::Completed},
    };
    std::stable_sort(events.begin(), events.end(),
                     [](Event const& a, Event const& b) { return a.t < b.t; });
    LeaderIntentState state;
    for (Event const& e : events)
    {
        if (e.end)
            EndLeaderIntent(state, e.kind, e.owner, e.how, e.t, LIMITS);
        else
            AskLeaderIntent(state, Req(e.kind, e.owner, e.target), e.t, LIMITS);
    }
    std::printf("measured window: 22 handoffs before, %u intent change(s) after\n",
                state.changes);
    Check("the replayed window changes intent a handful of times, not dozens",
          state.changes <= 3);
    Check("the leader ends the window on the banker walk",
          state.kind == LeaderIntentKind::TownErrand);
}

}  // namespace

int main()
{
    AFreeLeaderIsGrantedAndRenewed();
    ALowerRuleWaitsForTheCurrentOne();
    AHigherRuleWaitsForTheDwell();
    DeathOrCombatWaivesTheDwell();
    TheCampaignAndTheOperatorNeverWait();
    AnOwnerThatStopsAskingLetsGo();
    OnlyTheOwnerEndsItsIntent();
    AnEarlyChangeOfMindCoolsOff();
    JevsPickOutranksTheHeuristicsButNotTheCampaign();
    TheTableListsWhatIsAsked();
    WordsRoundTrip();
    TheMeasuredWindowSettles();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("one movement owner per family leader holds\n");
    return EXIT_SUCCESS;
}
