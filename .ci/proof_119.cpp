// THROWAWAY-BRANCH ONLY. Not part of the fix for #119 and never merged: this
// file exists on the verification branch so the defect and its repair can be
// SHOWN rather than argued about, on a runner that has a compiler. A permanent
// harness for the pure decisions is #116's job, not this PR's.
//
// It drives OverseerDecisions::Ratchet directly with the four limit constants
// mod_overseer.cpp declares, copied here rather than included because
// mod_overseer.cpp cannot be compiled without a whole core. Compile it against
// the fixed sources and against the ones on the branch this is stacked on, and
// compare the seven verdicts.
#include "overseer_decisions.h"

#include <cstdio>
#include <ctime>
#include <vector>

using namespace OverseerDecisions;

// TRAVEL_RATCHET, DUNGEON_CROSSING_RATCHET and FOLLOW_STALL_RATCHET, verbatim.
static RatchetLimits const TRAVEL{RatchetReading::DistanceToTarget, 10.0f, 20 * 60};
static RatchetLimits const CROSSING{RatchetReading::CountAchieved, 0.0f, 5 * 60};
static RatchetLimits const FOLLOW{RatchetReading::DistanceFromLastMark, 8.0f, 15 * 60};

// Poll the ratchet once every fifteen simulated seconds and return the poll at
// which it first said "stalled", or -1 if it never did.
//
// THE CLOCK IS SEEDED BEFORE THE FIRST POLL, because all three real callers
// seed it and a driver that does not is measuring its own omission. A `since`
// of zero means "no clock has been started" and can never stall, by design:
// DriveTravel sets state.progress.since when the errand changes,
// DriveDungeonRun sets coord.crossing.since at both phase transitions, and
// KeepRosterFollowing sets stall.progress.since the first time it sees a
// character. Without this the crossing and follower controls below report
// "never" for a reason that has nothing to do with what is being tested.
static int FirstStall(RatchetLimits const& limits, std::vector<float> const& readings)
{
    time_t now = 1000000;
    RatchetState state;
    state.since = now;
    for (size_t i = 0; i < readings.size(); ++i)
    {
        if (Ratchet(state, readings[i], now, limits).stalled)
            return static_cast<int>(i);
        now += 15;
    }
    return -1;
}

static void Say(char const* what, RatchetLimits const& limits,
                std::vector<float> const& readings)
{
    int const at = FirstStall(limits, readings);
    if (at < 0)
        std::printf("%s=never\n", what);
    else
        std::printf("%s=yes at poll %d, %d simulated minutes in\n", what, at, at * 15 / 60);
}

int main()
{
    // THE BUG. Walks in from 100 yards and then stands ON the aimed point.
    // GetDistance2d subtracts the subject's own size and clamps at zero
    // (Object.cpp:1323-1327), so this is the reading of a character wedged on
    // a portal it cannot step through or holding position at a trainer that
    // cannot teach it - both of which reach the ratchet by design.
    std::vector<float> arriveThenStand;
    for (float d = 100.f; d > 0.f; d -= 20.f)
        arriveThenStand.push_back(d);
    for (int i = 0; i < 400; ++i)
        arriveThenStand.push_back(0.f);

    // CONTROL. Stops 40 yards out and never reaches zero. Already bounded
    // before this fix, and must stay bounded identically.
    std::vector<float> stopShort;
    for (float d = 100.f; d > 40.f; d -= 20.f)
        stopShort.push_back(d);
    for (int i = 0; i < 400; ++i)
        stopShort.push_back(40.f);

    // CONTROL. Still closing, twenty yards a poll. Must never be given up on.
    std::vector<float> walking;
    for (int i = 0; i < 100; ++i)
        walking.push_back(3000.f - 20.f * i);

    // CONTROL, AND THE REGRESSION THAT MATTERS MOST. A crossing where nobody
    // ever gets through reads zero on every poll, and zero is a REAL count
    // rather than an absent one. It must STILL stall on both builds: had the
    // travel fix leaked into this reading, the first poll would count as
    // progress and this backstop would go the way the travel one had gone.
    std::vector<float> noneThrough(400, 0.f);

    // CONTROL. One more member through every ten polls, well inside patience.
    std::vector<float> crossingSteadily;
    for (int i = 0; i < 400; ++i)
        crossingSteadily.push_back(static_cast<float>(i / 10));

    // CONTROL. A follower that never gets away from its own mark, and one that
    // clears the jitter margin every poll.
    std::vector<float> notMoving(400, 0.f);
    std::vector<float> moving(400, 20.f);

    Say("travel_arrive_then_stand_stalls", TRAVEL, arriveThenStand);
    Say("travel_stop_40y_short_stalls", TRAVEL, stopShort);
    Say("travel_still_walking_stalls", TRAVEL, walking);
    Say("crossing_nobody_through_stalls", CROSSING, noneThrough);
    Say("crossing_steady_progress_stalls", CROSSING, crossingSteadily);
    Say("follow_not_moving_stalls", FOLLOW, notMoving);
    Say("follow_moving_stalls", FOLLOW, moving);
    return 0;
}
