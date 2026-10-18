/*
 * Dungeon runs: lockouts, group forming, and honest refusals (#963).
 *
 * A character's saved instance state - whether it is bound, when the lockout
 * expires, which bosses it has down - is the data the client's own instance
 * lockout window shows. These functions decide, from that data alone, whether a
 * run may be proposed and, when it may not, what refusal sentence a chat line
 * carries ("saved until Tuesday"). Nothing here touches a world, a bot, or a
 * database: it compiles src/overseer_decisions.cpp and this file and nothing
 * else, with no include path into a core.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::InstanceLockoutIsLive;
using OverseerDecisions::InstanceResetsTooSoon;
using OverseerDecisions::InstanceRunAnswer;
using OverseerDecisions::InstanceRunMayProceed;
using OverseerDecisions::InstanceRunVerdict;
using OverseerDecisions::SavedInstance;
using OverseerDecisions::SavedInstanceRefusal;
using OverseerDecisions::SecondsUntilInstanceReset;
using OverseerDecisions::WeekdayName;

namespace
{

int failures = 0;

void Check(char const* what, bool held)
{
    if (held)
        return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

void CheckString(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
}

void CheckInt(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: got %lld, want %lld\n", what, got, want);
}

// 1970-01-06 was a Tuesday: five days after the Thursday epoch.
std::time_t const tuesdayReset = 5 * 86400;
std::time_t const wednesdayReset = 6 * 86400;
std::time_t const nowLongPast = 100 * 86400;   // well after the reset
std::time_t const nowBefore = 1000;             // well before the reset

SavedInstance savedToTuesday()
{
    SavedInstance save;
    save.mapId = 36;
    save.difficulty = 1;
    save.saved = true;
    save.instanceId = 7;
    save.resetAt = tuesdayReset;
    save.completedEncounters = 0;
    save.cleared = false;
    return save;
}

SavedInstance savedButResetPassed()
{
    SavedInstance save = savedToTuesday();
    save.resetAt = nowLongPast - 86400;   // the reset already happened
    return save;
}

SavedInstance notSaved()
{
    SavedInstance save;
    save.mapId = 36;
    save.difficulty = 1;
    save.saved = false;
    save.resetAt = tuesdayReset;
    return save;
}

void TheWeekdayNameNamesTheDayTheLockoutExpires()
{
    Check("1970-01-06 reads Tuesday", WeekdayName(tuesdayReset) == "Tuesday");
    Check("1970-01-07 reads Wednesday", WeekdayName(wednesdayReset) == "Wednesday");
    Check("the epoch reads Thursday", WeekdayName(0) == "Thursday");
}

void ALiveLockoutIsSeenAsLive()
{
    Check("a saved instance whose reset is in the future is live",
          InstanceLockoutIsLive(savedToTuesday(), nowBefore));
    Check("a saved instance whose reset has passed is not live",
          !InstanceLockoutIsLive(savedButResetPassed(), nowLongPast));
    Check("an unsaved character is not live", !InstanceLockoutIsLive(notSaved(), nowBefore));
    Check("a zero reset is not live",
          !InstanceLockoutIsLive(SavedInstance{}, nowBefore));
}

void SecondsUntilTheResetCountsDown()
{
    CheckInt("seconds until a future reset", SecondsUntilInstanceReset(savedToTuesday(), nowBefore),
             static_cast<long long>(tuesdayReset - nowBefore));
    Check("seconds after a passed reset is negative",
          SecondsUntilInstanceReset(savedButResetPassed(), nowLongPast) < 0);
    CheckInt("an unsaved character reads zero", SecondsUntilInstanceReset(notSaved(), nowBefore), 0);
    CheckInt("a zero reset reads zero", SecondsUntilInstanceReset(SavedInstance{}, nowBefore), 0);
}

void AnInstanceThatResetsSoonIsRefused()
{
    std::time_t const justBefore = tuesdayReset - 600; // ten minutes before reset
    // A reset ten minutes out is within a twenty-minute soon bound.
    Check("a reset within the soon bound is too soon",
          InstanceResetsTooSoon(savedToTuesday(), justBefore, 20 * 60));
    // A reset five days out is not.
    Check("a reset beyond the soon bound is not too soon",
          !InstanceResetsTooSoon(savedToTuesday(), nowBefore, 20 * 60));
    // An unsaved character is never refused for a soon reset.
    Check("an unsaved character is not too soon", !InstanceResetsTooSoon(notSaved(), justBefore, 20 * 60));
    // A passed reset is not too soon.
    Check("a passed reset is not too soon",
          !InstanceResetsTooSoon(savedButResetPassed(), nowLongPast, 20 * 60));
}

void TheRefusalNamesTheWeekday()
{
    CheckString("a live lockout refuses with the weekday",
                SavedInstanceRefusal(savedToTuesday(), nowBefore),
                "saved until Tuesday");
    CheckString("no refusal when the lockout has passed",
                SavedInstanceRefusal(savedButResetPassed(), nowLongPast),
                "");
    CheckString("no refusal for an unsaved character",
                SavedInstanceRefusal(notSaved(), nowBefore),
                "");
}

void AProposalMayProceedWhenTheCharacterIsFree()
{
    InstanceRunAnswer const free = InstanceRunMayProceed(notSaved(), nowBefore, 20 * 60);
    Check("an unsaved character may proceed", free.verdict == InstanceRunVerdict::Proceed);
    Check("and carries no reason", free.reason.empty());

    InstanceRunAnswer const resetPassed =
        InstanceRunMayProceed(savedButResetPassed(), nowLongPast, 20 * 60);
    Check("a character whose lockout passed may proceed",
          resetPassed.verdict == InstanceRunVerdict::Proceed);
    Check("and carries no reason", resetPassed.reason.empty());
}

void AProposalIsRefusedWhenTheCharacterIsSaved()
{
    InstanceRunAnswer const saved =
        InstanceRunMayProceed(savedToTuesday(), nowBefore, 20 * 60);
    Check("a saved character is refused as saved",
          saved.verdict == InstanceRunVerdict::Saved);
    CheckString("and the refusal names the weekday", saved.reason, "saved until Tuesday");
}

void AProposalIsRefusedWhenTheLockoutResetsSoon()
{
    InstanceRunAnswer const soon =
        InstanceRunMayProceed(savedToTuesday(), nowBefore, 20 * 60);
    // tuesdayReset (5 days) is far beyond twenty minutes, so this is "saved",
    // not "resets soon". Force a soon reset by moving now close to the reset.
    std::time_t const justBefore = tuesdayReset - 600; // ten minutes before reset
    InstanceRunAnswer const reallySoon =
        InstanceRunMayProceed(savedToTuesday(), justBefore, 20 * 60);
    Check("a lockout resetting within the soon bound is refused as resets soon",
          reallySoon.verdict == InstanceRunVerdict::ResetsSoon);
}

}  // namespace

int main()
{
    TheWeekdayNameNamesTheDayTheLockoutExpires();
    ALiveLockoutIsSeenAsLive();
    SecondsUntilTheResetCountsDown();
    AnInstanceThatResetsSoonIsRefused();
    TheRefusalNamesTheWeekday();
    AProposalMayProceedWhenTheCharacterIsFree();
    AProposalIsRefusedWhenTheCharacterIsSaved();
    AProposalIsRefusedWhenTheLockoutResetsSoon();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the dungeon lockout decisions hold\n");
    return EXIT_SUCCESS;
}
