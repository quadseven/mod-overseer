/*
 * Where the party waits, and what is not a place to wait.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The world adapter reads two areatriggers out of the world database and hands
 * their numbers here; the numbers below are the ones a realm actually holds,
 * copied from the rows DungeonPortals() quotes, so the arithmetic is exercised
 * on real doors without a realm.
 *
 * THE REGRESSION THIS FILE EXISTS FOR is the last section: a staging point of
 * (0, 0, 0) must never be usable, however it got there. Measured live, a run
 * aimed its leader at `at:1:0,0,0` and walked him at the middle of Kalimdor for
 * the length of its backstop, because three zero-initialised floats reached a
 * travel errand without anything ever asking whether they had been resolved.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::DungeonStagingPoint;
using OverseerDecisions::StagingGroundBelievable;
using OverseerDecisions::StagingPoint;
using OverseerDecisions::StagingPointCheck;
using OverseerDecisions::StagingPointRefusal;
using OverseerDecisions::StagingPointUsable;
using OverseerDecisions::StagingPointVerdict;

namespace
{

int failures = 0;

char const* Name(StagingPointVerdict verdict)
{
    switch (verdict)
    {
        case StagingPointVerdict::Usable:         return "Usable";
        case StagingPointVerdict::Unresolved:     return "Unresolved";
        case StagingPointVerdict::OffTheMap:      return "OffTheMap";
        case StagingPointVerdict::NoApproachAxis: return "NoApproachAxis";
    }
    return "?";
}

void CheckVerdict(char const* what, StagingPointVerdict got, StagingPointVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// Two hundredths of a yard. The inputs are floats read out of a database and
// the sums are done in double, so the answer is exact to far better than this;
// the tolerance is here so the test is about the derivation rather than about
// the last bit of a float.
void CheckPoint(char const* what, StagingPoint const& got, float wantX, float wantY,
                float wantZ)
{
    if (got.verdict != StagingPointVerdict::Usable)
    {
        std::printf("FAIL %s: verdict %s, wanted Usable\n", what, Name(got.verdict));
        ++failures;
        return;
    }

    float const dx = got.x - wantX;
    float const dy = got.y - wantY;
    float const dz = got.z - wantZ;
    float const tolerance = 0.02f;
    bool const close = dx > -tolerance && dx < tolerance && dy > -tolerance &&
                       dy < tolerance && dz > -tolerance && dz < tolerance;
    if (close)
        return;

    std::printf("FAIL %s: got (%.4f, %.4f, %.4f), wanted (%.4f, %.4f, %.4f)\n", what,
                double(got.x), double(got.y), double(got.z), double(wantX),
                double(wantY), double(wantZ));
    ++failures;
}

// THE THREE PORTALS THAT WORKED BEFORE THIS CHANGE, AND MUST STILL. Their
// staging points are what this module already walks parties to, so a change to
// the derivation that moved any of them would be a regression dressed up as a
// fix. Each expected value is the door's own position, twenty yards back along
// the vector to where the way back out lands.
void TheWorkingPortalsDeriveWhatTheyAlreadyDerive()
{
    // areatrigger 78 -> areatrigger_teleport 119's landing point, on map 0.
    // The module's own comment records this axis as (0.016, -0.9999) over 12.82
    // yards, and the point below is that axis walked twenty yards.
    CheckPoint("deadmines",
               DungeonStagingPoint(-11208.5f, 1685.34f, -11208.3f, 1672.52f, 24.66f, 20.f),
               -11208.188f, 1665.3424f, 24.66f);

    // areatrigger 145 -> areatrigger_teleport 194's landing point, on map 0.
    CheckPoint("shadowfang",
               DungeonStagingPoint(-229.49f, 1576.35f, -232.796f, 1568.28f, 76.8909f, 20.f),
               -237.0718f, 1557.8428f, 76.8909f);

    // areatrigger 101 -> areatrigger_teleport 503's landing point, on map 0.
    // The shortest way back out in the table, at 3.88 yards, which is why the
    // standoff cannot simply BE the landing point.
    CheckPoint("stockades",
               DungeonStagingPoint(-8761.85f, 848.557f, -8764.83f, 846.075f, 87.4842f, 20.f),
               -8777.2178f, 835.7574f, 87.4842f);
}

// THE PORTAL THIS WAS FILED FOR. areatrigger 228 -> areatrigger_teleport 226's
// landing point, both on map 1. Nothing about the row or the arithmetic was
// ever wrong: given these four numbers the derivation answers a real place in
// the Barrens, which is what makes "the coordinator aimed at (0,0,0)" a story
// about a derivation that never ran rather than one that failed.
void TheWailingCavernsPortalDerivesARealPlace()
{
    StagingPoint const point =
        DungeonStagingPoint(-753.596f, -2212.78f, -740.059f, -2214.23f, 16.1374f, 20.f);
    CheckPoint("wailing", point, -733.7098f, -2214.9101f, 16.1374f);
    CheckBool("wailing is usable", StagingPointUsable(point.x, point.y, point.z), true);
}

// A landing point on top of the door names no direction to stand off along.
void ADoorWithNoCorridorIsRefused()
{
    CheckVerdict("landing on the door itself",
                 DungeonStagingPoint(100.f, 200.f, 100.f, 200.f, 50.f, 20.f).verdict,
                 StagingPointVerdict::NoApproachAxis);

    // Half a yard apart is inside the same refusal: a bearing taken over half a
    // yard of measurement noise is a guess with a decimal point on it.
    CheckVerdict("landing half a yard from the door",
                 DungeonStagingPoint(100.f, 200.f, 100.5f, 200.f, 50.f, 20.f).verdict,
                 StagingPointVerdict::NoApproachAxis);

    // And one yard and a bit is not, so the refusal has an edge rather than
    // being a mood.
    CheckVerdict("landing just over a yard from the door",
                 DungeonStagingPoint(100.f, 200.f, 101.5f, 200.f, 50.f, 20.f).verdict,
                 StagingPointVerdict::Usable);
}

// THE MEASURED DEFECT, AS A TEST. (0, 0, 0) is inside the world grid and passes
// every bounds check there is, so it has to be refused by name or it is the
// most plausible-looking wrong answer this module can produce.
void TheOriginIsNeverAStagingPoint()
{
    CheckVerdict("three zeroes", StagingPointCheck(0.f, 0.f, 0.f),
                 StagingPointVerdict::Unresolved);
    CheckBool("three zeroes are not usable", StagingPointUsable(0.f, 0.f, 0.f), false);

    // The exact aim that was measured: `at:1:0,0,0`.
    CheckBool("the aim that was measured", StagingPointUsable(0.f, 0.f, 0.f), false);

    // Two zeroes and a real height is the same sentinel. Every distance this
    // module measures against a staging point is a 2D one, so an x and y of
    // zero is what "nobody resolved this" looks like whatever the z says - and
    // a coordinator that had half-filled the struct would be worse, not better.
    CheckVerdict("two zeroes and a real z", StagingPointCheck(0.f, 0.f, 16.1374f),
                 StagingPointVerdict::Unresolved);

    // A derivation that arithmetically lands on the origin is refused too,
    // rather than returned as a success. Door twenty yards along +X of the
    // origin, way back out at the origin: the axis is a clean -X and the
    // standoff walks exactly onto the sentinel.
    CheckVerdict("a derivation that lands on the origin",
                 DungeonStagingPoint(20.f, 0.f, 0.f, 0.f, 0.f, 20.f).verdict,
                 StagingPointVerdict::Unresolved);

    // A real place a few yards away from it is fine, so the refusal is about
    // the sentinel and not about a neighbourhood.
    CheckBool("five yards off the origin", StagingPointUsable(5.f, 5.f, 0.f), true);
}

// Off the world grid, or not a number at all. A NaN fails every comparison, so
// the same positive bounds test that rejects an impossible coordinate rejects
// an arithmetic accident.
void APointOffTheWorldIsRefused()
{
    CheckVerdict("east of the grid", StagingPointCheck(20000.f, 100.f, 50.f),
                 StagingPointVerdict::OffTheMap);
    CheckVerdict("west of the grid", StagingPointCheck(-20000.f, 100.f, 50.f),
                 StagingPointVerdict::OffTheMap);
    CheckVerdict("north of the grid", StagingPointCheck(100.f, 99999.f, 50.f),
                 StagingPointVerdict::OffTheMap);

    // The two sentinels a height query returns when it has nothing. They are
    // refused by the grid bound rather than by a list of magic numbers this
    // module would have to keep in step with a core.
    CheckVerdict("the invalid-height sentinel", StagingPointCheck(100.f, 100.f, -100000.f),
                 StagingPointVerdict::OffTheMap);
    CheckVerdict("the other invalid-height sentinel",
                 StagingPointCheck(100.f, 100.f, -200000.f),
                 StagingPointVerdict::OffTheMap);

    float const zero = 0.f;
    float const nan = zero / zero;
    float const infinity = 1.f / zero;
    CheckVerdict("a NaN", StagingPointCheck(nan, 100.f, 50.f),
                 StagingPointVerdict::OffTheMap);
    CheckVerdict("an infinity", StagingPointCheck(100.f, infinity, 50.f),
                 StagingPointVerdict::OffTheMap);
}

// A staging point one short walk from a doorway is on the same floor as that
// doorway, so a height reading tens of yards from it is another surface.
void AGroundHeightFarFromTheDoorIsDisbelieved()
{
    // Deadmines: the way back out lands at 24.66 against the door's 25.7612,
    // which is the shape of a believable answer.
    CheckBool("a quarter of a yard below the door",
              StagingGroundBelievable(24.66f, 25.7612f, 15.f), true);

    // The clifftop over the Moonbrook shaft, twenty-seven yards up, which is
    // the surface the old derivation walked a party onto.
    CheckBool("the clifftop over the shaft",
              StagingGroundBelievable(52.98f, 25.7612f, 15.f), false);

    // And the sentinels, which fail the same test for the same reason and so
    // need no naming of their own.
    CheckBool("the invalid-height sentinel",
              StagingGroundBelievable(-100000.f, 25.7612f, 15.f), false);
    CheckBool("the other invalid-height sentinel",
              StagingGroundBelievable(-200000.f, 25.7612f, 15.f), false);

    // Symmetric: a floor above the door is disbelieved exactly as far away as a
    // floor below it.
    CheckBool("fifteen yards under, at the edge",
              StagingGroundBelievable(10.7612f, 25.7612f, 15.f), true);
    CheckBool("fifteen and a bit yards under",
              StagingGroundBelievable(10.5f, 25.7612f, 15.f), false);
}

// Every refusal has words to print, because the contract this whole change is
// about is "a run that cannot work out where to wait does not start AND SAYS
// WHY". A verdict with an empty reason would satisfy half of it.
void EveryRefusalSaysSomething()
{
    StagingPointVerdict const all[] = {
        StagingPointVerdict::Usable, StagingPointVerdict::Unresolved,
        StagingPointVerdict::OffTheMap, StagingPointVerdict::NoApproachAxis};
    for (StagingPointVerdict verdict : all)
        CheckBool(Name(verdict), !StagingPointRefusal(verdict).empty(), true);
}

}  // namespace

int main()
{
    TheWorkingPortalsDeriveWhatTheyAlreadyDerive();
    TheWailingCavernsPortalDerivesARealPlace();
    ADoorWithNoCorridorIsRefused();
    TheOriginIsNeverAStagingPoint();
    APointOffTheWorldIsRefused();
    AGroundHeightFarFromTheDoorIsDisbelieved();
    EveryRefusalSaysSomething();
    return failures ? 1 : 0;
}
