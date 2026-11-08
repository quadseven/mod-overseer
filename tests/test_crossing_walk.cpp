/*
 * A party still walking to its door is not given up on (#1414).
 *
 * Measured on the dev realm over 72 hours: every Uldaman EXIT began 376 to 406
 * yards from areatrigger 288, every Maraudon EXIT 861 to 1124 yards from 3131,
 * and the crossing backstop gave 33 of them up exactly five minutes in with
 * nobody through, while the party was still walking. One Uldaman party was
 * routed 1249 yards round to the door and had closed from 376 to 349 yards in
 * its first 15 seconds when the backstop fired.
 *
 * These tests drive the crossing's own ratchet (the count through the door,
 * five minutes of patience) beside the new walk ratchet, poll by poll, the way
 * DriveDungeonCrossing does.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <vector>

using OverseerDecisions::CROSSING_WALK_CEILING_SECONDS;
using OverseerDecisions::CrossingFarthestWalker;
using OverseerDecisions::CrossingWalkKeepsClock;
using OverseerDecisions::CrossingWalkState;
using OverseerDecisions::DoorAimHeight;
using OverseerDecisions::DoorAimOnTheFloor;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::Ratchet;
using OverseerDecisions::RatchetLimits;
using OverseerDecisions::RatchetReading;
using OverseerDecisions::RatchetState;
using OverseerDecisions::RatchetVerdict;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

// The crossing's own rule, as mod_overseer.cpp declares it
// (DUNGEON_CROSSING_RATCHET): a count, no margin, five minutes.
constexpr time_t BACKSTOP = 5 * 60;
constexpr RatchetLimits CROSSING{RatchetReading::CountAchieved, 0.f, BACKSTOP};
constexpr time_t POLL = 5;  // DUNGEON_RUN_POLL_MS

// One crossing, as DriveDungeonCrossing keeps it.
struct Crossing
{
    RatchetState count;
    CrossingWalkState walk;

    explicit Crossing(time_t start)
    {
        // Every place that opens a crossing does exactly this.
        count.best = 0.f;
        count.since = start;
    }

    // One poll: the walk first, then the count. Returns the count's verdict.
    RatchetVerdict Poll(time_t now, float farthest, float through)
    {
        if (CrossingWalkKeepsClock(walk, count.since, farthest, now))
            count.since = now;
        return Ratchet(count, through, now, CROSSING);
    }
};

DungeonRunEntryState Member(char const* name, float distance, bool through = false)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = true;
    state.through = through;
    state.distanceFromDoor = distance;
    return state;
}

void UldamanPartyStillWalkingIsKept()
{
    // Grug's party at the last boss, 380 yards out, closing at 2 yards a second
    // along a route that winds (the straight distance only falls on average).
    time_t const start = 1000;
    Crossing crossing(start);
    bool stalled = false;
    float farthest = 380.f;
    time_t now = start;
    for (; now <= start + 8 * 60 && farthest > 0.f; now += POLL)
    {
        stalled = stalled || crossing.Poll(now, farthest, 0.f).stalled;
        farthest -= 2.f * POLL;
    }
    Check("a party closing on the door is not given up while it walks", !stalled);
    Check("the walk reached the door inside eight minutes", farthest <= 0.f);
}

void WithoutTheWalkTheSamePartyIsGivenUp()
{
    // The behaviour before #1414, kept here as the measurement it replaces:
    // counting only crossings, the same walk is given up at five minutes.
    time_t const start = 1000;
    RatchetState count;
    count.since = start;
    bool stalled = false;
    time_t stalledAt = 0;
    for (time_t now = start; now <= start + 8 * 60; now += POLL)
    {
        if (!stalled && Ratchet(count, 0.f, now, CROSSING).stalled)
        {
            stalled = true;
            stalledAt = now;
        }
    }
    Check("crossings alone give the walk up", stalled);
    Check("crossings alone give it up just past five minutes",
          stalledAt > start + BACKSTOP && stalledAt <= start + BACKSTOP + POLL);
}

void AMemberStuckBehindAWallStillStalls()
{
    // Four members reach the doorstep; the fifth stands 128 yards out with
    // every bearing toward the door refused, and never moves.
    time_t const start = 1000;
    Crossing crossing(start);
    time_t stalledAt = 0;
    for (time_t now = start; now <= start + 15 * 60; now += POLL)
    {
        std::vector<DungeonRunEntryState> states{
            Member("Grug", 2.f), Member("Grog", 3.f), Member("Ugga", 1.f),
            Member("Og", 4.f), Member("Bork", 128.f)};
        if (crossing.Poll(now, CrossingFarthestWalker(states), 0.f).stalled)
        {
            stalledAt = now;
            break;
        }
    }
    Check("a party whose farthest member stopped is still given up", stalledAt != 0);
    Check("and on the ordinary five minutes",
          stalledAt > start + BACKSTOP && stalledAt <= start + BACKSTOP + 2 * POLL);
}

void AWalkThatNeverArrivesHitsTheCeiling()
{
    // Ten more yards found every minute, for ever: the ceiling ends it.
    time_t const start = 1000;
    Crossing crossing(start);
    time_t stalledAt = 0;
    float farthest = 5000.f;
    for (time_t now = start; now <= start + 60 * 60; now += POLL)
    {
        if ((now - start) % 60 == 0)
            farthest -= 11.f;
        if (crossing.Poll(now, farthest, 0.f).stalled)
        {
            stalledAt = now;
            break;
        }
    }
    Check("a walk that never arrives is given up", stalledAt != 0);
    Check("no sooner than the ceiling",
          stalledAt > start + CROSSING_WALK_CEILING_SECONDS);
    Check("and within five minutes of the last walk progress after it",
          stalledAt <= start + CROSSING_WALK_CEILING_SECONDS + BACKSTOP + 60 + POLL);
}

void AMemberCrossingStartsTheWalkAfresh()
{
    time_t const start = 1000;
    Crossing crossing(start);
    // Walk 19 minutes, nearly to the ceiling.
    time_t now = start;
    float farthest = 2000.f;
    for (; now < start + 19 * 60; now += POLL)
    {
        crossing.Poll(now, farthest, 0.f);
        farthest -= 1.f;
    }
    // One member crosses: real progress, and the walk is measured again from
    // that moment with a fresh ceiling.
    crossing.Poll(now, farthest, 1.f);
    time_t const crossedAt = now;
    bool stalled = false;
    for (now += POLL; now <= crossedAt + 10 * 60; now += POLL)
    {
        farthest -= 1.f;
        stalled = stalled || crossing.Poll(now, farthest, 1.f).stalled;
    }
    Check("after a member crosses, the others walking on are kept", !stalled);
    Check("the walk was re-measured from the crossing",
          crossing.walk.walkingSince == crossedAt);
}

void NobodyToMeasureKeepsNothing()
{
    CrossingWalkState walk;
    Check("no walker on the door's map does not restart the clock",
          !CrossingWalkKeepsClock(walk, 1000, -1.f, 1005));
    std::vector<DungeonRunEntryState> states{
        Member("Zug", 0.f, true), Member("Zork", -1.f), Member("Oz", -1.f, true)};
    Check("members through or off the map are not walkers",
          CrossingFarthestWalker(states) < 0.f);
    std::vector<DungeonRunEntryState> mixed{
        Member("Zug", 900.f, true), Member("Zork", 40.f), Member("Oz", 870.f)};
    Check("the farthest walker is the slowest member still walking",
          CrossingFarthestWalker(mixed) == 870.f);
}

void TheDoorAimSaysWhatTheProbeFound()
{
    // Uldaman's way out, areatrigger 288: radius 10 at z -39.2339. A floor
    // 9.3 yards under the middle is found but does not fit a 5 yard arrival
    // inside the sphere, so the aim keeps the row's z and says why.
    DoorAimHeight const refused = DoorAimOnTheFloor(-39.2339f, true, -48.5f, 5.f, 10.f);
    Check("a floor too far below is not used", !refused.grounded);
    Check("but the probe's answer is kept", refused.probed);
    Check("with the height it found", refused.probedZ == -48.5f);

    DoorAimHeight const none = DoorAimOnTheFloor(-39.2339f, false, 0.f, 5.f, 10.f);
    Check("no floor found says so", !none.probed && !none.grounded);

    DoorAimHeight const used = DoorAimOnTheFloor(-39.2339f, true, -43.f, 5.f, 10.f);
    Check("a floor that fits is used and reported", used.grounded && used.probed &&
                                                      used.probedZ == -43.f);
}
}  // namespace

int main()
{
    UldamanPartyStillWalkingIsKept();
    WithoutTheWalkTheSamePartyIsGivenUp();
    AMemberStuckBehindAWallStillStalls();
    AWalkThatNeverArrivesHitsTheCeiling();
    AMemberCrossingStartsTheWalkAfresh();
    NobodyToMeasureKeepsNothing();
    TheDoorAimSaysWhatTheProbeFound();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_crossing_walk: all passed\n");
    return 0;
}
