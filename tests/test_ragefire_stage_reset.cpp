/*
 * Two Ragefire Chasm failures that named the wrong thing (dev realm,
 * 2026-09-25/26).
 *
 * 1. Four runs in a row closed "GATHERING held for more than 12 minutes and
 *    never opened - Uzza (73y out)". The yards were to the approach corridor's
 *    start on the pass, not to the staging point; the leader stood 70 yards
 *    (flat) from the staging point and never moved. The close reason now says
 *    which point it measured.
 * 2. Five attempts in a row closed "'Uzza' is still bound to instance 10 on
 *    map 389 after the reset, so the instance was not empty when it was
 *    asked", each asking three times in ten seconds and naming nobody. The
 *    reset now waits while the core's own list of players in that copy is not
 *    empty, and names them.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ApproachGap;
using OverseerDecisions::ApproachWhere;
using OverseerDecisions::ApproachWhereOnLeg;
using OverseerDecisions::InstanceOccupiedBlocker;

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

bool Contains(std::string const& text, std::string const& part)
{
    return text.find(part) != std::string::npos;
}

void TheCorridorLegSaysItIsNotTheStagingPoint()
{
    ApproachGap gap;
    gap.measured = true;
    gap.horizontalYards = 73.4f;
    gap.verticalYards = 0.4f;

    std::string const onCorridor = ApproachWhereOnLeg(gap, true);
    Check("the corridor leg keeps the yards", Contains(onCorridor, "73y out"));
    Check("the corridor leg names the corridor's start",
          Contains(onCorridor, "from the start of the approach corridor"));
    Check("the corridor leg says it is not the staging point",
          Contains(onCorridor, "not the staging point"));

    Check("the direct leg reads exactly as before",
          ApproachWhereOnLeg(gap, false) == ApproachWhere(gap));
    Check("the direct leg never mentions the corridor",
          !Contains(ApproachWhereOnLeg(gap, false), "corridor"));
}

void AnEmptyInstanceMayBeReset()
{
    Check("nobody inside is no blocker",
          InstanceOccupiedBlocker(389, 10, {}, {"Zug", "Uzza"}).empty());
}

void AnOccupiedInstanceWaitsAndNamesEveryone()
{
    std::vector<std::string> const family = {"Zug", "Oz", "Uzza", "Zork", "Zrog"};
    std::string const why = InstanceOccupiedBlocker(389, 10, {"Zork", "Stranger"}, family);
    Check("an occupied instance is a blocker", !why.empty());
    Check("it names the copy", Contains(why, "instance 10 of map 389"));
    Check("it counts who is inside", Contains(why, "2 player(s)"));
    Check("a family member is marked as family", Contains(why, "Zork (family)"));
    Check("anybody else is marked as not of the family",
          Contains(why, "Stranger (not of this family)"));
}

}   // namespace

int main()
{
    TheCorridorLegSaysItIsNotTheStagingPoint();
    AnEmptyInstanceMayBeReset();
    AnOccupiedInstanceWaitsAndNamesEveryone();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ragefire staging words and reset occupancy: all checks passed\n");
    return 0;
}
