/*
 * The hearth verb clears a moving flag that nothing is driving.
 *
 * Measured on the dev realm: a member's hearth rows were refused with
 * "character is moving" eight times running, with the hold placed each time and
 * the member never standing, so the zone it kept dying in was never left. A
 * moving flag with no spline in flight and an idle (or no) motion generator is
 * a stale flag: nothing is walking the member, so there is nothing to stop and
 * the cast is owed. A member that really is moving keeps the refusal.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::HearthMovingIsStale;
using OverseerDecisions::MoveGenerator;

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    Check(HearthMovingIsStale(false, MoveGenerator::Idle),
          "no spline and an idle generator is a stale flag");
    Check(HearthMovingIsStale(false, MoveGenerator::Unsampled),
          "no spline and no motion master at all is a stale flag");
    Check(!HearthMovingIsStale(true, MoveGenerator::Idle),
          "a spline in flight is a real walk, even under an idle generator");
    Check(!HearthMovingIsStale(true, MoveGenerator::Point), "a point walk is real");
    Check(!HearthMovingIsStale(false, MoveGenerator::Point),
          "a generator that is driving the member is not stale without a spline either");
    Check(!HearthMovingIsStale(false, MoveGenerator::Follow), "following is a real walk");
    Check(!HearthMovingIsStale(false, MoveGenerator::Chase), "chasing is a real walk");
    Check(!HearthMovingIsStale(false, MoveGenerator::Flee), "fleeing is a real walk");
    Check(!HearthMovingIsStale(false, MoveGenerator::Thrown), "a knockback is a real move");
    Check(!HearthMovingIsStale(false, MoveGenerator::Other), "an unnamed generator is not guessed at");

    if (failures)
        return EXIT_FAILURE;
    std::printf("hearth stale moving: ok\n");
    return EXIT_SUCCESS;
}
