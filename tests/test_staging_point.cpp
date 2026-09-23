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

using OverseerDecisions::AreaTriggerShape;
using OverseerDecisions::DungeonStagingPoint;
using OverseerDecisions::DungeonStagingStandoffYards;
using OverseerDecisions::InsideAreaTrigger;
using OverseerDecisions::StagingGroundBelievable;
using OverseerDecisions::StagingPoint;
using OverseerDecisions::StagingPointCheck;
using OverseerDecisions::StagingPointRefusal;
using OverseerDecisions::StagingPointUsable;
using OverseerDecisions::StagingPointVerdict;

namespace
{

int failures = 0;

// The adapter's DUNGEON_STAGING_STANDOFF_YARDS and DUNGEON_BARRIER_RADIUS_YARDS.
constexpr float STANDOFF = 20.f;
constexpr float BARRIER = 10.f;

// A box row, in the column order of the areatrigger table after the map.
AreaTriggerShape Box(float x, float y, float z, float length, float width, float height,
                     float orientation)
{
    AreaTriggerShape shape;
    shape.x = x;
    shape.y = y;
    shape.z = z;
    shape.length = length;
    shape.width = width;
    shape.height = height;
    shape.orientation = orientation;
    return shape;
}

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

// THE THREE SCARLET WINGS ADDED ALONGSIDE THE GRAVEYARD ROW. Each door and each
// way-back-out landing point is copied from the same areatrigger and
// areatrigger_teleport rows DungeonPortals() quotes next to `scarlet-library`,
// `scarlet-armory` and `scarlet-cathedral`, so this is the same regression
// protection the three rows above already have: a change to the derivation that
// moved any of these would be a bug dressed up as a fix.
//
// `scarlet-armory`'s entry door, areatrigger 612, is the one radius in the
// table that is not 8 - it is 6 - and that fact plays no part in this
// derivation at all: DungeonStagingPoint never reads a radius, only the two
// triggers' positions. The radius is exercised at the crossing instead, by
// ArrivalReachesTrigger and the world's own IsInAreaTriggerRadius, neither of
// which this file can reach without a running core.
void TheThreeScarletWingsDeriveTheirOwnStagingPoints()
{
    // areatrigger 614 -> areatrigger_teleport 608's landing point, on map 0.
    CheckPoint("scarlet-library",
               DungeonStagingPoint(2859.73f, -824.91f, 2870.9f, -820.16f, 160.33f, 20.f),
               2878.1350f, -817.0834f, 160.33f);

    // areatrigger 612 -> areatrigger_teleport 606's landing point, on map 0.
    CheckPoint("scarlet-armory",
               DungeonStagingPoint(2877.98f, -839.267f, 2884.45f, -822.01f, 160.33f, 20.f),
               2885.0012f, -820.5399f, 160.33f);

    // areatrigger 610 -> areatrigger_teleport 604's landing point, on map 0.
    CheckPoint("scarlet-cathedral",
               DungeonStagingPoint(2925.18f, -820.545f, 2906.14f, -813.77f, 160.33f, 20.f),
               2906.3373f, -813.8402f, 160.33f);
}

// THE THIRTEEN DOORS ADDED FOR EVERY CLASSIC FIVE-PLAYER DUNGEON (#548). Each
// door and each way-back-out landing point is copied from the areatrigger and
// areatrigger_teleport rows DungeonPortals() quotes beside its keyword, so a
// change to the derivation that moved any of them fails here, the same as the
// rows above. None of these points has been walked yet; what is pinned is the
// arithmetic, not that the ground there is standable.
void TheClassicDoorsDeriveTheirOwnStagingPoints()
{
    // areatrigger 257 -> areatrigger_teleport 259's landing point, on map 1.
    CheckPoint("blackfathom",
               DungeonStagingPoint(4252.37f, 756.974f, 4247.74f, 745.879f, -24.5299f, 20.f),
               4244.6680f, 738.5166f, -24.5299f);

    // areatrigger 244 -> areatrigger_teleport 242's landing point, on map 1.
    CheckPoint("razorfen-kraul",
               DungeonStagingPoint(-4456.7f, -1655.99f, -4464.92f, -1666.24f, 81.8928f, 20.f),
               -4469.2124f, -1671.5927f, 81.8928f);

    // areatrigger 442 -> areatrigger_teleport 444's landing point, on map 1.
    CheckPoint("razorfen-downs",
               DungeonStagingPoint(-4666.52f, -2536.82f, -4658.12f, -2526.35f, 81.492f, 20.f),
               -4654.0044f, -2521.2202f, 81.492f);

    // areatrigger 324 -> areatrigger_teleport 322's landing point, on map 0.
    CheckPoint("gnomeregan",
               DungeonStagingPoint(-5161.33f, 939.623f, -5163.33f, 927.623f, 257.188f, 20.f),
               -5164.6182f, 919.8951f, 257.188f);

    // areatrigger 523 -> areatrigger_teleport 525's landing point, on map 0.
    // The landing is due -Y of the door, so the point is too; it falls between
    // the trigger and the locked Workshop Door the row's comment names.
    CheckPoint("gnomeregan-depot",
               DungeonStagingPoint(-4858.27f, 785.03f, -4858.27f, 756.435f, 244.923f, 20.f),
               -4858.2700f, 765.0300f, 244.923f);

    // areatrigger 286 -> areatrigger_teleport 288's landing point, on map 0.
    CheckPoint("uldaman",
               DungeonStagingPoint(-6053.73f, -2954.63f, -6066.73f, -2955.63f, 209.776f, 20.f),
               -6073.6709f, -2956.1638f, 209.776f);

    // areatrigger 902 -> areatrigger_teleport 882's landing point, on map 0.
    CheckPoint("uldaman-back",
               DungeonStagingPoint(-6606.48f, -3762.19f, -6620.48f, -3765.19f, 266.226f, 20.f),
               -6626.0361f, -3766.3806f, 266.226f);

    // areatrigger 924 -> areatrigger_teleport 922's landing point, on map 1.
    // Radius 20, so this point is on the trigger's own rim; see the row.
    CheckPoint("zulfarrak",
               DungeonStagingPoint(-6773.49f, -2889.77f, -6796.49f, -2890.77f, 8.88063f, 20.f),
               -6793.4712f, -2890.6387f, 8.88063f);

    // areatrigger 446 -> areatrigger_teleport 448's landing point, on map 0.
    CheckPoint("sunken-temple",
               DungeonStagingPoint(-10162.7f, -3998.65f, -10175.1f, -3995.15f, -112.9f, 20.f),
               -10181.9482f, -3993.2168f, -112.9f);

    // areatrigger 1466 -> areatrigger_teleport 1472's landing point, on map 0.
    CheckPoint("blackrock-depths",
               DungeonStagingPoint(-7176.63f, -937.667f, -7179.63f, -923.667f, 166.416f, 20.f),
               -7180.8203f, -918.1110f, 166.416f);

    // areatrigger 1468 -> areatrigger_teleport 1470's landing point, on map 0.
    CheckPoint("lower-blackrock-spire",
               DungeonStagingPoint(-7518.19f, -1239.13f, -7524.7f, -1228.41f, 287.204f, 20.f),
               -7528.5713f, -1222.0354f, 287.204f);

    // areatrigger 2216 -> areatrigger_teleport 2221's landing point, on map 0.
    // The landing is at the OTHER door, 672 yards away, so the bearing is the
    // one the row's comment warns about; the arithmetic is still pinned.
    //
    // AND 2216 IS A BOX, so since #577 it stands off by its half-diagonal plus
    // the gather circle rather than by a flat 20: sqrt(11.415^2 + 4.0415^2)
    // + 10 = 22.109. See TheBoxDoorsDeriveTheirOwnStagingPoints below.
    CheckPoint("stratholme-live",
               DungeonStagingPoint(3392.46f, -3396.77f, 3235.46f, -4050.6f, 108.45f,
                                   DungeonStagingStandoffYards(
                                       Box(3392.46f, -3396.77f, 143.073f, 22.83f, 8.083f,
                                           34.69f, 0.f),
                                       STANDOFF, BARRIER)),
               3387.2978f, -3418.2682f, 108.45f);

    // areatrigger 2214 -> areatrigger_teleport 2221's landing point, on map 0.
    CheckPoint("stratholme-undead",
               DungeonStagingPoint(3237.46f, -4060.6f, 3235.46f, -4050.6f, 108.45f, 20.f),
               3233.5376f, -4040.9885f, 108.45f);
}

// THE TEN BOX DOORS (#577). Every number is a row read out of the world
// database and quoted beside the door in DungeonPortals(): the entry trigger's
// own position and box, and where its row's exit trigger lands on the outside
// map. The standoff is not written down either: it is derived from the box by
// DungeonStagingStandoffYards, the same call the adapter makes.
void TheBoxDoorsDeriveTheirOwnStagingPoints()
{
    struct Door
    {
        char const* keyword;
        AreaTriggerShape entry;
        float backX, backY, backZ;
        float standoff;
        float wantX, wantY;
    };
    Door const doors[] = {
        // 2230 -> 2226's landing
        {"ragefire", Box(1818.4f, -4427.26f, -10.4478f, 21.69f, 11.83f, 21.22f, 0.576f),
         1813.49f, -4418.58f, -18.57f, 22.3532f, 1807.3943f, -4407.8039f},
        // 3133 -> 3131's landing
        {"maraudon-orange", Box(-1484.07f, 2617.57f, 75.7144f, 24.69f, 15.78f, 35.19f, 4.538f),
         -1471.07f, 2618.57f, 76.1944f, 24.6510f, -1459.4916f, 2619.4606f},
        // 3134 -> 3126's landing
        {"maraudon-purple", Box(-1181.98f, 2861.95f, 85.2581f, 31.69f, 17.33f, 34.19f, 3.211f),
         -1186.98f, 2875.95f, 85.7258f, 28.0595f, -1191.4174f, 2888.3748f},
        // 2567 -> 2568's landing
        {"scholomance", Box(1282.05f, -2548.73f, 85.3994f, 10.56f, 13.03f, 21.67f, 0.4712f),
         1275.05f, -2552.03f, 90.3994f, 20.f, 1263.9595f, -2557.2584f},
        // 3185 -> 3196's landing
        {"dire-maul-east-east", Box(-4028.21f, 123.966f, 26.8109f, 9.833f, 4.583f, 15.69f, 0.4712f),
         -4030.21f, 127.966f, 26.8109f, 20.f, -4037.1543f, 141.8545f},
        // 3183 -> 3194's landing
        {"dire-maul-east-west", Box(-3730.48f, 933.975f, 160.973f, 9.389f, 12.78f, 19.67f, 0.f),
         -3737.48f, 934.975f, 160.973f, 20.f, -3750.2790f, 936.8034f},
        // 3184 -> 3195's landing
        {"dire-maul-east-south", Box(-3981.58f, 771.193f, 160.962f, 10.31f, 5.972f, 20.22f, 0.f),
         -3980.58f, 776.193f, 161.006f, 20.f, -3977.6577f, 790.8046f},
        // 3187 -> 3191's landing
        {"dire-maul-west-north", Box(-3741.96f, 1249.18f, 160.217f, 7.861f, 10.33f, 18.69f, 0.f),
         -3747.96f, 1249.18f, 160.217f, 20.f, -3761.96f, 1249.18f},
        // 3186 -> 3190's landing
        {"dire-maul-west-south", Box(-3837.79f, 1250.23f, 160.223f, 8.194f, 9.083f, 18.36f, 0.f),
         -3831.79f, 1250.23f, 160.223f, 20.f, -3817.79f, 1250.23f},
        // 3189 -> 3193's landing
        {"dire-maul-north", Box(-3520.65f, 1068.72f, 161.128f, 9.861f, 11.86f, 23.22f, 0.f),
         -3520.65f, 1077.72f, 161.138f, 20.f, -3520.65f, 1088.72f},
    };

    for (Door const& door : doors)
    {
        float const standoff = DungeonStagingStandoffYards(door.entry, STANDOFF, BARRIER);
        float const off = standoff - door.standoff;
        if (off > 0.002f || off < -0.002f)
        {
            std::printf("FAIL %s standoff: got %.4f, wanted %.4f\n", door.keyword,
                        static_cast<double>(standoff), static_cast<double>(door.standoff));
            ++failures;
        }
        StagingPoint const point = DungeonStagingPoint(door.entry.x, door.entry.y, door.backX,
                                                       door.backY, door.backZ, standoff);
        CheckPoint(door.keyword, point, door.wantX, door.wantY, door.backZ);

        // AND THE POINT IS OUTSIDE THE DOOR, which is what the standoff is for.
        // Asked of the core's own box test at the landing's height and at the
        // door's: a staging point inside the trigger it stages for is a party
        // gathering on the doormat.
        CheckBool(door.keyword,
                  InsideAreaTrigger(door.entry, point.x, point.y, door.entry.z, 0.f), false);
        CheckBool(door.keyword,
                  InsideAreaTrigger(door.entry, point.x, point.y, door.backZ, 0.f), false);
    }
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
    TheThreeScarletWingsDeriveTheirOwnStagingPoints();
    TheClassicDoorsDeriveTheirOwnStagingPoints();
    TheBoxDoorsDeriveTheirOwnStagingPoints();
    ADoorWithNoCorridorIsRefused();
    TheOriginIsNeverAStagingPoint();
    APointOffTheWorldIsRefused();
    AGroundHeightFarFromTheDoorIsDisbelieved();
    EveryRefusalSaysSomething();
    return failures ? 1 : 0;
}
