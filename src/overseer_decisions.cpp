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
        // BEFORE "wrong map", because being inside IS a wrong map and is the
        // one wrong map that means something specific: the member is ahead of
        // the party rather than lost behind it. See DungeonRunMemberState.
        else if (member.inside)
            why = "already inside";
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

StagingNudge StagingWatchdog(StagingStallState& state, float distanceFromStage,
                             bool measurable, time_t now,
                             RatchetLimits const& limits)
{
    if (!measurable)
    {
        // Held, not read. `best` is deliberately left alone: a member that
        // fought its way forward and then came back out of combat nearer than
        // it has ever been should count that as progress, and a member that was
        // pushed backwards should not have its mark spoiled by the push.
        state.progress.since = now;
        return StagingNudge::Nothing;
    }

    RatchetVerdict const verdict = Ratchet(state.progress, distanceFromStage, now, limits);
    if (verdict.progressed)
    {
        // IT IS COMING. The clock has already been restarted by the ratchet;
        // what is undone here is the ladder, so a member that closes the gap
        // after two nudges is watched from the bottom again rather than being
        // one bad patch away from being given up on.
        state.escalated = 0;
        state.gaveUp = false;
        return StagingNudge::Nothing;
    }
    if (!verdict.stalled)
        return StagingNudge::Nothing;

    if (state.escalated >= STAGING_NUDGE_STEPS)
    {
        if (state.gaveUp)
            return StagingNudge::Nothing;
        state.gaveUp = true;
        return StagingNudge::GiveUp;
    }

    // THE CLOCK RESTARTS ON EVERY RUNG, so the rung just climbed is given a
    // whole patience window to work in before the next one is tried. Without
    // this the three of them and the give-up would all fire on consecutive
    // polls, which is not an escalation - it is one reaction spelled four ways.
    state.progress.since = now;
    unsigned const rung = state.escalated++;
    if (rung == 0)
        return StagingNudge::Restrategy;
    if (rung == 1)
        return StagingNudge::Reaim;
    return StagingNudge::ClearMovement;
}

namespace
{

bool Wants(std::vector<unsigned> const& wanted, unsigned skill)
{
    for (unsigned const id : wanted)
        if (id == skill)
            return true;
    return false;
}

bool Holds(std::vector<ProfessionHolding> const& held, unsigned skill)
{
    for (ProfessionHolding const& holding : held)
        if (holding.skill == skill)
            return true;
    return false;
}

// The three primaries that FEED other people's crafts rather than consuming
// their own supply. Spelled out here and nowhere else in this module, because
// nothing in the core answers it: SkillLineEntry has a category, and every one
// of these shares it with the eight crafting primaries, so there is no lookup
// to defer to. Three numbers with a reason beside them is the honest form of a
// fact the data does not carry.
//
// USED FOR ORDER AND NOTHING ELSE. Which professions a character ends up with
// is the roster's decision and this has no vote in it; this only decides which
// of two skills the roster ALREADY chose gets taken first, which is why being
// wrong here would cost a delay and not a profession.
bool Feeds(unsigned skill)
{
    return skill == 182     // herbalism
        || skill == 186     // mining
        || skill == 393;    // skinning
}

}  // namespace

ProfessionStep NextProfessionStep(std::vector<unsigned> const& wanted,
                                  std::vector<ProfessionHolding> const& held,
                                  unsigned maxPrimary)
{
    ProfessionStep step;

    if (wanted.empty())
        return step;                                    // rule 1: no opinion

    // Rules 2 and 4 in one pass. `take` is the skill a free slot would be
    // filled with, and a gatherer displaces a crafter for it once - the second
    // gatherer does not displace the first, so a `wanted` in a fixed order
    // always produces the same answer.
    unsigned missing = 0;
    unsigned take = 0;
    for (unsigned const id : wanted)
    {
        if (Holds(held, id))
            continue;
        ++missing;
        if (!take || (Feeds(id) && !Feeds(take)))
            take = id;
    }

    // RULE 2, AND THE WHOLE OF THE IDEMPOTENCE. Nothing the roster asked for is
    // absent, so there is nothing to make room for, so nothing can be
    // destroyed. A character that reaches its assigned pair leaves through here
    // on every poll for the rest of its life.
    if (!missing)
        return step;

    // RULE 3. Room before ruin.
    if (held.size() < static_cast<std::vector<ProfessionHolding>::size_type>(maxPrimary))
    {
        step.kind = ProfessionStepKind::Take;
        step.skill = take;
        return step;
    }

    // RULE 5. Cheapest first, and only among the ones the roster did not ask
    // for. The id is the tie-break purely so that two skills of equal value
    // cannot make two polls disagree about which one dies.
    for (ProfessionHolding const& holding : held)
    {
        if (Wants(wanted, holding.skill))
            continue;
        if (step.kind != ProfessionStepKind::Nothing &&
            (holding.value > step.cost ||
             (holding.value == step.cost && holding.skill > step.skill)))
            continue;

        step.kind = ProfessionStepKind::GiveUp;
        step.skill = holding.skill;
        step.cost = holding.value;
    }

    // Falls out as Nothing when every held skill is one the roster also wants:
    // the end state asks for more primaries than a character may hold, and the
    // answer to that is to do nothing loudly rather than to pick a victim.
    return step;

bool GiveHeldOff(GiveRefusalBook& book, std::string const& key, time_t now,
                 time_t backoffSeconds, time_t forgetSeconds, std::string& reason)
{
    bool held = false;

    for (auto it = book.begin(); it != book.end();)
    {
        // Cold entries go on the way past, whether or not they are the one
        // being asked about. This is the only walk of the book there is, so it
        // is the only place the sweep can happen.
        if (it->second.since == 0 || now - it->second.since >= forgetSeconds)
        {
            it = book.erase(it);
            continue;
        }

        if (it->first == key && now - it->second.since < backoffSeconds)
        {
            reason = it->second.reason;
            held = true;
        }
        ++it;
    }

    return held;
}

bool NoteGiveRefusal(GiveRefusalBook& book, std::string const& key,
                     std::string const& reason, time_t now)
{
    GiveRefusal& memory = book[key];
    // NEW means "not the same wall as last time": either nothing was
    // remembered here at all, or the give is being refused for a different
    // reason than it was, which is a change worth a line of log even though
    // the outcome is the same refusal.
    bool const worthSaying = memory.since == 0 || memory.reason != reason;
    memory.reason = reason;
    memory.since = now;
    return worthSaying;
}

}  // namespace OverseerDecisions
