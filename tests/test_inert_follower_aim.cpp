/*
 * A follower's counter-keyword aim that nothing walks is cleared (#1610).
 *
 * Only the leader carries `new rpg`, so a follower aimed at `vendor` is told it
 * "does not carry `new rpg` - nothing walks it anywhere", and the aim stood in
 * the column for ever: the release gate fenced it as a foreign aim, the catch-up
 * walk refused the same follower because "errand 'vendor' is outstanding", and
 * the errand budget was billed on every poll.
 *
 * After the aim has been refused as inert for INERT_FOLLOWER_AIM_SECONDS the
 * release goes through, for a counter keyword only. A profession errand, a
 * positional aim and a travel guard are never cleared by this rule.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

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
    Expect("a positional aim", InertFollowerAimMayBeCleared("at:0:1.0,2.0,3.0", 0, bound * 5), false);
    Expect("a travel guard", InertFollowerAimMayBeCleared("guard:0:1.0,2.0,3.0", 0, bound * 5), false);
    Expect("an empty column", InertFollowerAimMayBeCleared("", 0, bound * 5), false);

    // The gate: inert lets a counter keyword through, and only that.
    Expect("gate clears an inert vendor aim", TravelReleaseGate(Facts(true, 0, "vendor")).empty(), true);
    Expect("gate clears an inert repair aim", TravelReleaseGate(Facts(true, 0, "repair")).empty(), true);
    Expect("gate still fences a vendor aim not known inert", TravelReleaseGate(Facts(false, 0, "vendor")).empty(), false);
    Expect("gate still fences an inert positional aim", TravelReleaseGate(Facts(true, 0, "at:0:1.0,2.0,3.0")).empty(), false);
    Expect("gate still fences a profession errand", TravelReleaseGate(Facts(true, 197, "vendor")).empty(), false);

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
