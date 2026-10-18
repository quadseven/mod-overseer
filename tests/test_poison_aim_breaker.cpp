/*
 * The poison-aim circuit breaker (#824).
 *
 * The live failure this pins. A family leader held `at:36:...`, a point inside
 * an instance, while standing outside it. The travel drive refuses an `at:` aim
 * whose map is not the character's own and releases it; the release gate then
 * skips the column write, because an unclaimed `at:` aim is foreign. The column
 * kept the aim and the next poll did the same thing again, for ever.
 *
 * PoisonAimStep counts refusals of one identical unclaimed `at:` aim from one
 * map and trips once the loop has proved itself, so the caller clears it. The
 * foreign-aim fence itself is untouched: one refusal, a few refusals, a claimed
 * aim, a different aim or a map change never trip it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::PoisonAimLimits;
using OverseerDecisions::PoisonAimState;
using OverseerDecisions::PoisonAimStep;
using OverseerDecisions::POISON_AIM_LIMITS;

namespace
{

int failures = 0;

void Expect(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

std::string const kPoison = "at:36:-14.4,-393.4,63.8";
uint32_t const kOutside = 0;
int64_t const kPoll = 15;  // the travel drive's poll, TRAVEL_POLL_MS

// Feed `count` refusals one poll apart starting at `start`; return how many of
// them tripped the breaker.
int Feed(PoisonAimState& state, std::string const& aim, bool claimed, uint32_t mapId,
         int64_t start, int count, PoisonAimLimits const& limits = POISON_AIM_LIMITS)
{
    int trips = 0;
    for (int i = 0; i < count; ++i)
        if (PoisonAimStep(state, aim, claimed, mapId, start + i * kPoll, limits))
            ++trips;
    return trips;
}

// THE DEFECT ITSELF. Before #824 nothing counted, so an identical unresolvable
// foreign aim was refused for ever. Refused every poll for long enough, it
// trips exactly once.
void AnUnbrokenLoopTrips()
{
    PoisonAimState state;
    int const polls = 40;  // ten minutes of the loop
    int const trips = Feed(state, kPoison, false, kOutside, 1000, polls);
    Expect("an unbroken loop of one foreign at: aim trips the breaker", trips >= 1, true);

    // And it trips when both bounds are met, not before.
    PoisonAimState counted;
    int firstTrip = -1;
    for (int i = 0; i < polls && firstTrip < 0; ++i)
        if (PoisonAimStep(counted, kPoison, false, kOutside, 1000 + i * kPoll))
            firstTrip = i;
    int64_t const seconds = static_cast<int64_t>(firstTrip) * kPoll;
    Expect("the trip waits for the refusal count",
           firstTrip + 1 >= static_cast<int>(POISON_AIM_LIMITS.refusals), true);
    Expect("the trip waits for the minimum duration",
           seconds >= POISON_AIM_LIMITS.minSeconds, true);
    // The state resets on a trip, so a writer that puts the same aim back is
    // counted from nothing again rather than cleared on its first refusal.
    Expect("the state is reset by a trip", counted.refusals == 0, true);
    Expect("a re-written poison aim is not cleared on its first refusal",
           PoisonAimStep(counted, kPoison, false, kOutside, 1000 + polls * kPoll), false);
}

// THE FENCE STAYS UP FOR AN AIM THAT IS NOT YET A LOOP.
void AShortRunDoesNotTrip()
{
    PoisonAimState state;
    Expect("one refusal never trips",
           PoisonAimStep(state, kPoison, false, kOutside, 1000), false);
    PoisonAimState some;
    Expect("a handful of refusals never trips",
           Feed(some, kPoison, false, kOutside, 1000, 10) == 0, true);
    // Many refusals packed into a short time (a fast poller) still wait for
    // the minimum duration.
    PoisonAimState fast;
    int trips = 0;
    for (int i = 0; i < 100; ++i)
        if (PoisonAimStep(fast, kPoison, false, kOutside, 1000 + i))
            ++trips;
    Expect("a hundred refusals in a hundred seconds do not trip", trips == 0, true);
}

// ONLY AN UNCLAIMED `at:` AIM COUNTS.
void OnlyAnUnclaimedPointAimCounts()
{
    PoisonAimState claimed;
    Expect("a claimed at: aim is never the breaker's (the release clears it)",
           Feed(claimed, kPoison, true, kOutside, 1000, 60) == 0, true);
    char const* const others[] = { "repair", "vendor", "banker", "auctioneer",
                                   "trigger:78", "profession trainer", "" };
    for (char const* aim : others)
    {
        PoisonAimState state;
        if (Feed(state, aim, false, kOutside, 1000, 60) != 0)
        {
            std::printf("FAIL '%s': a non-at: aim must never trip the breaker\n", aim);
            ++failures;
        }
    }
}

// ANYTHING THAT SAYS THE LOOP IS NOT A LOOP STARTS THE COUNT AGAIN.
void ProgressResetsTheCount()
{
    int const almost = 19;  // one short of the default count
    // A different aim: the writer moved on.
    {
        PoisonAimState state;
        Feed(state, kPoison, false, kOutside, 1000, almost);
        int const trips = Feed(state, "at:0:-9460.0,62.0,56.0", false, kOutside,
                               1000 + almost * kPoll, almost);
        Expect("a different aim restarts the count", trips == 0, true);
    }
    // A map change: a crossing is under way.
    {
        PoisonAimState state;
        Feed(state, kPoison, false, kOutside, 1000, almost);
        int const trips = Feed(state, kPoison, false, 1, 1000 + almost * kPoll, almost);
        Expect("standing on another map restarts the count", trips == 0, true);
    }
    // A long gap: something else walked the character in between.
    {
        PoisonAimState state;
        Feed(state, kPoison, false, kOutside, 1000, almost);
        int64_t const later = 1000 + almost * kPoll + POISON_AIM_LIMITS.gapSeconds + 1;
        int const trips = Feed(state, kPoison, false, kOutside, later, almost);
        Expect("a gap past gapSeconds restarts the count", trips == 0, true);
    }
    // A claim in between: the book took the column.
    {
        PoisonAimState state;
        Feed(state, kPoison, false, kOutside, 1000, almost);
        PoisonAimStep(state, kPoison, true, kOutside, 1000 + almost * kPoll);
        int const trips = Feed(state, kPoison, false, kOutside, 1000 + (almost + 1) * kPoll,
                               almost);
        Expect("a claim in between restarts the count", trips == 0, true);
    }
}

// THE LIMITS ARE THE CALLER'S TO PASS. A tighter set trips sooner.
void LimitsAreHonoured()
{
    PoisonAimLimits tight;
    tight.refusals = 3;
    tight.minSeconds = 30;
    tight.gapSeconds = 60;
    PoisonAimState state;
    Expect("refusal 1 of 3", PoisonAimStep(state, kPoison, false, kOutside, 0, tight), false);
    Expect("refusal 2 of 3", PoisonAimStep(state, kPoison, false, kOutside, 15, tight), false);
    Expect("refusal 3 of 3 at 30s trips", PoisonAimStep(state, kPoison, false, kOutside, 30, tight),
           true);
}

}  // namespace

int main()
{
    AnUnbrokenLoopTrips();
    AShortRunDoesNotTrip();
    OnlyAnUnclaimedPointAimCounts();
    ProgressResetsTheCount();
    LimitsAreHonoured();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a poisoned at: aim is cleared once it proves a loop, and only then\n");
    return EXIT_SUCCESS;
}
