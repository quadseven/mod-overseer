/*
 * The definitions for src/overseer_decisions.h. See that header for why these
 * live outside mod_overseer.cpp at all.
 *
 * This file includes its own header FIRST and then nothing, which is the point
 * of it: if a core type ever gets into one of these decisions, this
 * translation unit stops compiling here rather than compiling anyway inside
 * the module's own. The comments explaining each decision are in the header,
 * next to the declaration a caller reads.
 */

#include "overseer_decisions.h"

namespace OverseerDecisions
{

bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          float radiusYards)
{
    if (members.empty())
        return false;

    for (DungeonRunMemberState const& member : members)
    {
        if (!member.seen)
            return false;
        if (!member.alive)
            return false;
        if (member.inCombat)
            return false;
        if (member.distanceFromStage < 0.f || member.distanceFromStage > radiusYards)
            return false;
    }
    return true;
}

std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      float radiusYards)
{
    std::string blockers;
    for (DungeonRunMemberState const& member : members)
    {
        std::string why;
        if (!member.seen)
            why = "not seen";
        else if (!member.alive)
            why = "dead";
        else if (member.inCombat)
            why = "in combat";
        else if (member.distanceFromStage < 0.f)
            why = "wrong map";
        else if (member.distanceFromStage > radiusYards)
            why = std::to_string(static_cast<int>(member.distanceFromStage)) + "y away";
        else
            continue;

        if (!blockers.empty())
            blockers += ", ";
        blockers += member.name + " (" + why + ")";
    }
    return blockers;
}

bool DungeonRunEntryReady(std::vector<DungeonRunEntryState> const& members,
                          float doorstepYards)
{
    if (members.empty())
        return false;

    for (DungeonRunEntryState const& member : members)
    {
        if (member.through)
            continue;
        if (!member.seen)
            return false;
        if (!member.alive)
            return false;
        if (member.inCombat)
            return false;
        if (member.distanceFromDoor < 0.f || member.distanceFromDoor > doorstepYards)
            return false;
    }
    return true;
}

bool DungeonRunAllThrough(std::vector<DungeonRunEntryState> const& members)
{
    if (members.empty())
        return false;

    for (DungeonRunEntryState const& member : members)
        if (!member.through)
            return false;
    return true;
}

std::string DungeonRunEntryBlockers(std::vector<DungeonRunEntryState> const& members,
                                    float doorstepYards)
{
    std::string blockers;
    for (DungeonRunEntryState const& member : members)
    {
        if (member.through)
            continue;

        std::string why;
        if (!member.seen)
            why = "not seen";
        else if (!member.alive)
            why = "dead";
        else if (member.inCombat)
            why = "in combat";
        else if (member.distanceFromDoor < 0.f)
            why = "wrong map";
        else if (member.distanceFromDoor > doorstepYards)
            why = std::to_string(static_cast<int>(member.distanceFromDoor)) + "y from the door";
        else
            why = "at the door, not through";

        if (!blockers.empty())
            blockers += ", ";
        blockers += member.name + " (" + why + ")";
    }
    return blockers;
}

bool RatchetProgressed(float reading, float best, RatchetLimits const& limits)
{
    switch (limits.reading)
    {
        case RatchetReading::DistanceToTarget:
            // `!best` is "no reading yet" here and not "arrived" - see the
            // enum. It is also exactly the test the travel backstop this came
            // from was already making against its own `closest`.
            return !best || reading < best - limits.margin;
        case RatchetReading::CountAchieved:
        case RatchetReading::DistanceFromLastMark:
            // The same comparison for both, which is not a coincidence worth
            // tidying away: they differ in what the mark BECOMES on progress,
            // below, not in what beats it. A mark the caller moves is always
            // measured from zero.
            return reading > best + limits.margin;
    }
    return false;
}

RatchetVerdict Ratchet(RatchetState& state, float reading, time_t now,
                       RatchetLimits const& limits)
{
    RatchetVerdict verdict;
    verdict.progressed = RatchetProgressed(reading, state.best, limits);

    if (verdict.progressed)
    {
        state.best =
            limits.reading == RatchetReading::DistanceFromLastMark ? 0.f : reading;
        state.since = now;
        return verdict;
    }

    // A patience of zero means the caller counts, and a `since` of zero means
    // no clock has been started yet. Neither can stall, and neither is a
    // degenerate case to be papered over: they are two sites saying, in the
    // only place it can be said once, that they do not want this half.
    verdict.stalled = limits.patienceSeconds != 0 && state.since != 0 &&
                      now - state.since > limits.patienceSeconds;
    return verdict;
}

}  // namespace OverseerDecisions
