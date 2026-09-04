/*
 * Wailing Caverns traversal policy, without AzerothCore or guessed geometry.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonTraversalAction;
using OverseerDecisions::DungeonTraversalFacts;
using OverseerDecisions::DungeonTraversalKind;
using OverseerDecisions::DungeonTraversalPhase;
using OverseerDecisions::DungeonTraversalState;
using OverseerDecisions::DungeonTraversalStep;
using OverseerDecisions::WailingTraversalStep;
using OverseerDecisions::WailingTraversalStepDecision;

namespace
{
int failures = 0;

void Check(char const* what, DungeonTraversalAction got, DungeonTraversalAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

DungeonTraversalFacts Measured()
{
    DungeonTraversalFacts facts;
    facts.approachMeasured = true;
    facts.destinationMeasured = true;
    facts.destinationNavmesh = true;
    facts.destinationSafe = true;
    facts.actionReady = true;
    return facts;
}

void MissingGeometryWaits()
{
    DungeonTraversalFacts facts;
    Check("unmeasured approach waits",
          DungeonTraversalStep(DungeonTraversalKind::Jump, facts, {}),
          DungeonTraversalAction::WaitForMeasurement);
    facts.approachMeasured = true;
    Check("unmeasured destination waits",
          DungeonTraversalStep(DungeonTraversalKind::Drop, facts, {}),
          DungeonTraversalAction::WaitForMeasurement);
}

void UnsafeGeometryAborts()
{
    DungeonTraversalFacts facts = Measured();
    facts.destinationNavmesh = false;
    Check("unreachable destination aborts",
          DungeonTraversalStep(DungeonTraversalKind::Jump, facts, {}),
          DungeonTraversalAction::Abort);
    facts = Measured();
    facts.destinationSafe = false;
    Check("unsafe destination aborts",
          DungeonTraversalStep(DungeonTraversalKind::Drop, facts, {}),
          DungeonTraversalAction::Abort);
}

void NonWalkingActionsNeverDegradeToWalking()
{
    DungeonTraversalFacts facts = Measured();
    facts.actionReady = false;
    Check("jump needs an executor",
          DungeonTraversalStep(DungeonTraversalKind::Jump, facts, {}),
          DungeonTraversalAction::Abort);
    Check("drop needs an executor",
          DungeonTraversalStep(DungeonTraversalKind::Drop, facts, {}),
          DungeonTraversalAction::Abort);
    facts.actionReady = true;
    Check("measured jump",
          DungeonTraversalStep(DungeonTraversalKind::Jump, facts, {}),
          DungeonTraversalAction::Jump);
    Check("measured drop",
          DungeonTraversalStep(DungeonTraversalKind::Drop, facts, {}),
          DungeonTraversalAction::Drop);
}

void TerminalStatesStayTerminal()
{
    DungeonTraversalFacts facts = Measured();
    DungeonTraversalState state;
    state.phase = DungeonTraversalPhase::Complete;
    Check("complete stays complete", DungeonTraversalStep(DungeonTraversalKind::Jump, facts, state),
          DungeonTraversalAction::Complete);
    state.phase = DungeonTraversalPhase::Aborted;
    Check("aborted stays aborted", DungeonTraversalStep(DungeonTraversalKind::Drop, facts, state),
          DungeonTraversalAction::Abort);
}

void WailingRequiresBothExplicitJumps()
{
    DungeonTraversalFacts facts = Measured();
    Check("wailing first jump", WailingTraversalStepDecision(
                                     WailingTraversalStep::FirstJump, facts, {}),
          DungeonTraversalAction::Jump);
    Check("wailing second jump", WailingTraversalStepDecision(
                                      WailingTraversalStep::SecondJump, facts, {}),
          DungeonTraversalAction::Jump);
    facts.destinationMeasured = false;
    Check("first jump waits for measurement", WailingTraversalStepDecision(
                                                   WailingTraversalStep::FirstJump,
                                                   facts, {}),
          DungeonTraversalAction::WaitForMeasurement);
}
}  // namespace

int main()
{
    MissingGeometryWaits();
    UnsafeGeometryAborts();
    NonWalkingActionsNeverDegradeToWalking();
    TerminalStatesStayTerminal();
    WailingRequiresBothExplicitJumps();
    return failures ? 1 : 0;
}
