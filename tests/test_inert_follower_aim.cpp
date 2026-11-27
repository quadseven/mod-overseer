/*
 * A follower's counter-keyword aim that nothing walks is cleared (#1610).
 *
 * Only the leader carries `new rpg`, so a follower aimed at `vendor` is told it
 * "does not carry `new rpg` - nothing walks it anywhere", and the aim stood in
 * the column for ever: the release gate fenced it as a foreign aim, the catch-up
 * walk refused the same follower because "errand 'vendor' is outstanding", and
 * the errand budget was billed on every poll.
 *
 * After the refusal bound, a counter keyword or unclaimed point can be
 * released. An active claim, a changed aim, a profession errand and a guard
 * keep their protection (#1723).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"
#include "overseer_movement_ownership.h"

#include <cstdio>
#include <string>

using OverseerDecisions::InertFollowerAimMayBeCleared;
using OverseerDecisions::TravelReleaseGate;
using OverseerDecisions::TravelReleaseGateFacts;

namespace
{
int failures = 0;

void Expect(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

TravelReleaseGateFacts Facts(bool inert, uint32_t learn, char const* standing)
{
    TravelReleaseGateFacts f;
    f.inertFollowerAim = inert;
    f.learnSkill = learn;
    f.standing = standing;
    return f;
}

void RetireOnlyTheRefusedAim()
{
    char const* const name = "Follower";
    char const* const point = "at:0:1,2,3";
    char const* const next = "at:0:4,5,6";
    time_t const now = 1000000;
    OverseerMovement::InMemoryAimColumn column;
    OverseerMovement::MovementOwnership wheel{column, 45};

    wheel.Claim(name, point, OverseerDecisions::TravelOwner::Run, false, now);
    wheel.ReleaseInertFollowerAim(name, point, now);
    Expect("inert release leaves a run's exact aim and claim", column.columns[name] == point &&
           wheel.HoldsClaim(name, point) && !wheel.WithinHandbackGrace(name, now), true);

    wheel.ReleaseVanished(name, now - 45);
    column.columns[name] = next;
    wheel.ReleaseInertFollowerAim(name, point, now);
    Expect("inert release leaves a replacement point", column.columns[name] == next &&
           !wheel.WithinHandbackGrace(name, now), true);

    column.columns[name] = point;
    column.learnSkills[name] = 197;
    wheel.ReleaseInertFollowerAim(name, point, now);
    Expect("inert release leaves a pending profession errand", column.columns[name] == point &&
           !wheel.WithinHandbackGrace(name, now), true);

    column.learnSkills.clear();
    column.disabled.insert(name);
    wheel.ReleaseInertFollowerAim(name, point, now);
    Expect("a failed clear leaves the aim and hand-back clock alone", column.columns[name] == point &&
           !wheel.WithinHandbackGrace(name, now), true);

    column.disabled.erase(name);
    wheel.ReleaseInertFollowerAim(name, point, now);
    Expect("inert release clears the exact unclaimed point", column.columns[name].empty(), true);
    Expect("a successful inert release stamps the hand-back clock",
           wheel.WithinHandbackGrace(name, now), true);
    Expect("the catch-up walk can claim the released column",
           wheel.Claim(name, next, OverseerDecisions::TravelOwner::CatchUp, false, now).Holds(), true);
}
}  // namespace

int main()
{
    int64_t const bound = OverseerDecisions::INERT_FOLLOWER_AIM_SECONDS;

    // The bound.
    Expect("not yet inert for the bound", InertFollowerAimMayBeCleared("vendor", 0, bound - 1), false);
    Expect("inert for the bound", InertFollowerAimMayBeCleared("vendor", 0, bound), true);
    Expect("inert for longer", InertFollowerAimMayBeCleared("repair", 0, bound * 5), true);
    Expect("never inert", InertFollowerAimMayBeCleared("vendor", 0, 0), false);

    // What it never clears.
    Expect("a profession errand", InertFollowerAimMayBeCleared("vendor", 197, bound * 5), false);
    Expect("a point below the bound", InertFollowerAimMayBeCleared("at:0:1,2,3", 0, bound - 1), false);
    Expect("a point at the bound", InertFollowerAimMayBeCleared("at:0:1,2,3", 0, bound), true);
    Expect("a travel guard", InertFollowerAimMayBeCleared("guard:0:1.0,2.0,3.0", 0, bound * 5), false);
    Expect("an empty column", InertFollowerAimMayBeCleared("", 0, bound * 5), false);

    // The gate allows only the bounded inert release.
    Expect("gate clears an inert vendor aim", TravelReleaseGate(Facts(true, 0, "vendor")).empty(), true);
    Expect("gate clears an inert repair aim", TravelReleaseGate(Facts(true, 0, "repair")).empty(), true);
    Expect("gate still fences a vendor aim not known inert", TravelReleaseGate(Facts(false, 0, "vendor")).empty(), false);
    Expect("gate clears an inert positional aim", TravelReleaseGate(Facts(true, 0, "at:0:1.0,2.0,3.0")).empty(), true);
    Expect("gate fences a positional aim not known inert", TravelReleaseGate(Facts(false, 0, "at:0:1.0,2.0,3.0")).empty(), false);
    Expect("gate still fences a profession errand", TravelReleaseGate(Facts(true, 197, "vendor")).empty(), false);
    Expect("gate fences a point with a pending profession errand", TravelReleaseGate(Facts(true, 197, "at:0:1,2,3")).empty(), false);
    Expect("gate fences an inert guard", TravelReleaseGate(Facts(true, 0, "guard:0:1,2,3")).empty(), false);

    RetireOnlyTheRefusedAim();

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
