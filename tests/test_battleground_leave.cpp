/*
 * A member leaves a battleground queue (counterpart of bg-queue).
 *
 * WHAT IS PINNED HERE:
 *
 *   - The row is `bg-leave [<av|wsg|ab>]` on kind='guild', routed on its
 *     first word. No argument means every queue; anything else is malformed.
 *   - A character inside a battleground is refused, never pulled out.
 *   - A character that is not queued is not a failure ("already").
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}
}  // namespace

int main()
{
    Check("routed on first word", IsBattlegroundLeaveRow("bg-leave wsg"), true);
    Check("bare verb routed", IsBattlegroundLeaveRow("  bg-leave "), true);
    Check("bg-queue is not a leave", IsBattlegroundLeaveRow("bg-queue wsg"), false);
    Check("the queue verb ignores a leave", IsBattlegroundQueueRow("bg-leave wsg"), false);
    Check("the finder run ignores a leave", IsGuildFinderRow("bg-leave"), false);

    struct Case
    {
        char const* row;
        char const* key;
        uint32_t type;
    };
    for (Case const& c : {Case{"bg-leave av", "av", 1}, Case{"bg-leave wsg", "wsg", 2},
                          Case{"bg-leave ab", "ab", 3}})
    {
        BattlegroundLeaveRequest const r = ParseBattlegroundLeaveRequest(c.row);
        Check(c.row, r.valid && !r.all, true);
        CheckText(c.row, r.key, c.key);
        Check(c.row, r.bgTypeId == c.type, true);
    }

    BattlegroundLeaveRequest const all = ParseBattlegroundLeaveRequest("bg-leave");
    Check("no argument leaves every queue", all.valid && all.all && all.bgTypeId == 0, true);

    for (char const* bad : {"bg-leave 2v2", "bg-leave AV", "bg-leave av wsg", "bg-queue av"})
    {
        BattlegroundLeaveRequest const r = ParseBattlegroundLeaveRequest(bad);
        Check(bad, r.valid, false);
        Check(bad, r.error.empty(), false);
    }

    BattlegroundLeaveFacts facts;
    Check("not queued is already",
          DecideBattlegroundLeave(facts) == BattlegroundLeaveOutcome::Already, true);
    facts.queued = true;
    Check("queued is left", DecideBattlegroundLeave(facts) == BattlegroundLeaveOutcome::Left,
          true);
    facts.inBattleground = true;
    Check("inside a battleground is refused",
          DecideBattlegroundLeave(facts) == BattlegroundLeaveOutcome::Refused, true);
    facts.queued = false;
    Check("inside and unqueued is still refused",
          DecideBattlegroundLeave(facts) == BattlegroundLeaveOutcome::Refused, true);
    CheckText("the refusal names the reason", BattlegroundLeaveRefusal(), "inside a battleground");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a member leaves a battleground queue\n");
    return EXIT_SUCCESS;
}
