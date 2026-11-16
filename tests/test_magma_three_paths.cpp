/*
 * The stride, the follow step and the catch-up walk each refuse to step into
 * world-model magma (#1573 criterion 1, #1557).
 *
 * All three movement paths prove their ground with one routine: the travel
 * stride (the bearing fan) and the catch-up walk (a travel aim at the leader
 * position, so it goes through the same fan) call GroundHolds, and the follow
 * step (FollowStepHolds, also used to hand a catch-up back) calls it too.
 * GroundHolds is a thin call to OverseerDecisions::WalkFooting, so this test
 * drives that walk over a WMO lake fixture, once per path, with the inputs
 * each path gives it.
 *
 * THE FIXTURE (heights are the test own, shaped after the Molten Core
 * approach): a walkway at z 181.9 for y <= 0; for y > 0 a lake whose magma
 * surface stands at z 175, 6.9 yards under the walkway. That is inside the
 * drop bound on purpose, so the DROP rule cannot refuse the stride and only
 * the liquid rule can. The bed is out of the core reach (the case #1573
 * found), so the fixture reports the ground as unmeasured, exactly as
 * Map::GetWaterOrGroundLevel does there.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; the adapter is
 * pinned as text, as the sibling tests do, because it cannot be compiled
 * without a core.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

constexpr float WALKWAY_Z = 181.9f;
constexpr float MAGMA_Z = 175.f;

// The adapter numbers (TRAVEL_GROUND_* in src/mod_overseer.cpp).
constexpr FootingWalkLimits LIMITS = {4.f, 10.f, 50.f, 8.f};
constexpr float STEP_YARDS = 60.f;
constexpr float MIN_STEP = 8.f;
constexpr float SPELL_DISTANCE = 28.5f;

// `fixed` is the code under test (the probe plan). `!fixed` reproduces the
// pre-fix gate, which asked for the liquid type only when the bed was found,
// and is here to prove the fixture can see the bug.
bool Surface(bool fixed, float, float y, float, float& out, bool& lethal)
{
    lethal = false;
    if (y <= 0.f)
    {
        out = WALKWAY_Z;
        return true;
    }
    out = MAGMA_Z;
    bool const groundKnown = false;   // the bed is out of the core reach
    if (fixed)
    {
        LiquidProbePlan const plan = LethalLiquidProbe(MAGMA_Z, 0.f, groundKnown);
        lethal = plan.ask && StepLiquidIsLethal(plan.depthYards, DEATH_LIQUID_MAGMA);
    }
    return true;
}

FootingWalkResult Walk(bool fixed, float fx, float fy, float tx, float ty)
{
    return WalkFooting(fx, fy, WALKWAY_Z, tx, ty, LIMITS,
                       [fixed](float x, float y, float from, float& out, bool& lethal)
                       { return Surface(fixed, x, y, from, out, lethal); });
}

// The travel stride: one bearing of the fan, as DriveTravel takes it.
bool StrideTaken(bool fixed, float fx, float fy, float aimX, float aimY, float bearingDelta,
                 float& endY)
{
    float const dx = aimX - fx;
    float const dy = aimY - fy;
    float const span = std::sqrt(dx * dx + dy * dy);
    float const angle = std::atan2(dy, dx) + bearingDelta;
    float const reach = std::fmin(span, STEP_YARDS);
    FootingWalkResult const r = Walk(fixed, fx, fy, fx + std::cos(angle) * reach,
                                     fy + std::sin(angle) * reach);
    if (!ProvenStepIsWorthTaking(r.holds, r.provedYards, MIN_STEP))
        return false;
    float const walk = r.holds ? reach : r.provedYards;
    endY = fy + std::sin(angle) * walk;
    return true;
}

// The follow step: one straight step of SpellDistance toward the master.
bool FollowStepHoldsAt(bool fixed, float fx, float fy, float leaderX, float leaderY)
{
    float const dx = leaderX - fx;
    float const dy = leaderY - fy;
    float const span = std::sqrt(dx * dx + dy * dy);
    float const t = std::fmin(1.f, SPELL_DISTANCE / span);
    return Walk(fixed, fx, fy, fx + dx * t, fy + dy * t).holds;
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}

}  // namespace

int main()
{
    static constexpr float FAN[] = {0.f, 0.5236f, -0.5236f, 1.0472f, -1.0472f};

    // PATH 1, THE STRIDE. Standing 3 yards from the shore, aimed straight into
    // the lake: every bearing first strides into magma, so none is a step.
    float endY = 0.f;
    bool anyStride = false;
    for (float d : FAN)
        anyStride = anyStride || StrideTaken(true, 0.f, -3.f, 0.f, 40.f, d, endY);
    Check("stride: no bearing of the fan steps toward the magma", anyStride, false);
    // Farther back, a stride may be taken, but only the ground that was proved:
    // it ends on the walkway and never reaches the shore magma.
    bool const farStride = StrideTaken(true, 0.f, -20.f, 0.f, 40.f, 0.f, endY);
    Check("stride: a long stride is cut short at the shore", farStride && endY <= 0.f, true);
    // The fixture does expose the bug: the pre-fix gate walks straight in.
    Check("stride: the pre-fix gate would have stepped into the magma",
          StrideTaken(false, 0.f, -3.f, 0.f, 40.f, 0.f, endY) && endY > 0.f, true);

    // PATH 2, THE FOLLOW STEP. The master stands across the lake; the step
    // upstream would draw is 28.5 yards long and lands in magma.
    Check("follow step: refused over magma", FollowStepHoldsAt(true, 0.f, -3.f, 0.f, 60.f), false);
    Check("follow step: the pre-fix gate would have allowed it",
          FollowStepHoldsAt(false, 0.f, -3.f, 0.f, 60.f), true);
    Check("follow step: still allowed along dry ground",
          FollowStepHoldsAt(true, 0.f, -40.f, 0.f, -5.f), true);

    // PATH 3, THE CATCH-UP WALK. A catch-up is a travel aim at the leader
    // position, so it takes the stride fan at a far aim, and its hand-back
    // asks the follow step. The leader stands 120 yards across the lake.
    bool anyCatchUp = false;
    for (float d : FAN)
        anyCatchUp = anyCatchUp || StrideTaken(true, 0.f, -3.f, 0.f, 120.f, d, endY);
    Check("catch-up: no bearing toward a leader across the magma is a step", anyCatchUp, false);
    Check("catch-up: the hand-back is refused while the leader is over magma",
          FollowStepHoldsAt(true, 0.f, -3.f, 10.f, 12.f), false);

    // SHORT SPANS (1 to 3.9 yards, under the 4 yard sample interval). The walk
    // used to take a fast path for these that sampled nothing, so a dry bot a
    // few yards from the shore stepped straight in. The destination and the
    // midpoint are sampled for liquid now, for every path.
    static constexpr float SHORT[][2] = {{0.5f, 1.f}, {1.f, 2.f}, {2.f, 3.5f}, {0.1f, 3.9f}};
    for (auto const& pair : SHORT)
    {
        // Standing `over` yards short of the shore, aimed `reach` yards on, so
        // the whole step is shorter than a sample and ends in magma.
        float const over = pair[0];
        float const reach = pair[1];
        FootingWalkResult const r = Walk(true, 0.f, -over, 0.f, -over + reach);
        char what[96];
        std::snprintf(what, sizeof what, "short stride (%.1f yd) into magma is refused", reach);
        Check(what, r.holds, false);
        std::snprintf(what, sizeof what, "short stride (%.1f yd) takes no step", reach);
        Check(what, ProvenStepIsWorthTaking(r.holds, r.provedYards, MIN_STEP), false);
        std::snprintf(what, sizeof what, "short follow step (%.1f yd) into magma is refused", reach);
        Check(what, FollowStepHoldsAt(true, 0.f, -over, 0.f, -over + reach), false);
        std::snprintf(what, sizeof what, "short catch-up hand-back (%.1f yd) over magma is refused", reach);
        Check(what, FollowStepHoldsAt(true, 0.f, -over, 0.f, -over + reach), false);
    }
    Check("a short step along dry ground still holds", Walk(true, 0.f, -20.f, 0.f, -17.f).holds, true);
    Check("a bot already in magma may take a short step",
          Walk(true, 0.f, 5.f, 0.f, 7.f).holds, true);

    // The adapter has to route all three through the one walk.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::size_t const holds = text.find("static bool GroundHolds(Player* bot, float toX");
    std::size_t const holdsEnd = text.find("static bool NothingInTheWay(", holds);
    std::size_t const follow = text.find("static bool FollowStepHolds(");
    if (holds == std::string::npos || holdsEnd == std::string::npos ||
        follow == std::string::npos)
    {
        std::printf("FAIL the adapter footing functions are not where the test expects\n");
        return EXIT_FAILURE;
    }
    Check("GroundHolds is the shared walk",
          Has(text.substr(holds, holdsEnd - holds), "OverseerDecisions::WalkFooting("), true);
    Check("the stride fan asks GroundHolds",
          Has(text, "bool const holds = GroundHolds(bot, sx, sy, footing, &proved);"), true);
    Check("the follow step asks GroundHolds",
          Has(text.substr(follow, 2400), "GroundHolds(p, toX, toY, footing)"), true);
    std::size_t const first = text.find("FollowStepHolds(p, leader)");
    std::size_t const second =
        first == std::string::npos ? first : text.find("FollowStepHolds(p, leader)", first + 1);
    Check("the catch-up asks the follow step on both its hand-back and its start",
          second != std::string::npos, true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
