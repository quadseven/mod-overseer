/*
 * A character standing in lava wades out, and no hold keeps it there.
 *
 * THE FINDING (a public tracker ticket on repeated deaths at the Blackrock
 * Mountain graveyard exit in Burning Steppes). Over seven days one patch of
 * lava beside that graveyard held 118 of the zone's lava deaths across ten
 * characters. Every one of the 118 points lies over a polygon the navmesh
 * tags MAGMA, which a bot's path filter excludes, so the walk did not route
 * through the lava. And the death rows say the character did not walk into it
 * either: the position sampled seconds before each death equals the death
 * position, and the movement generator on every row is idle. A character
 * that is placed in lava, or revived in it, and then stands still burns to
 * death in about seven seconds; the revival hold stands a character still for
 * twenty, and the hold register pins it where it is on every tick.
 *
 * THE RULE. A hold never pins a character whose feet are in lethal liquid,
 * the revival hold is not taken there, and a living character in lethal
 * liquid walks to the nearest dry ground it can measure.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else. The second
 * half reads src/mod_overseer.cpp as text, because the adapter cannot be
 * compiled without a core: it pins that each place a character can be held
 * asks the rule.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::HeldStillFacts;
using OverseerDecisions::PickWadeOut;
using OverseerDecisions::RetakeTheHold;
using OverseerDecisions::WadeOutPick;

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

void CheckNear(char const* what, float got, float want, float tolerance)
{
    if (std::fabs(got - want) <= tolerance)
        return;
    std::printf("FAIL %s: got %.2f, wanted %.2f\n", what, got, want);
    ++failures;
}

bool Has(std::string const& text, char const* needle, std::size_t from = 0)
{
    return text.find(needle, from) != std::string::npos;
}

// A lake: lava inside a disc, dry ground outside it. `shoreZ` is the dry
// surface, `lavaZ` the lava's. A point with no ground in reach answers false.
struct Lake
{
    float cx, cy, radius;
    float lavaZ = 128.f;
    float shoreZ = 134.f;
    // A hole in the world where nothing can be measured.
    bool hole = false;
    float holeX = 0.f, holeY = 0.f, holeRadius = 0.f;

    bool operator()(float x, float y, float /*from*/, float& out, bool& lethal) const
    {
        lethal = false;
        if (hole && std::hypot(x - holeX, y - holeY) <= holeRadius)
            return false;
        if (std::hypot(x - cx, y - cy) <= radius)
        {
            out = lavaZ;
            lethal = true;
            return true;
        }
        out = shoreZ;
        return true;
    }
};

HeldStillFacts Held(bool inLava)
{
    HeldStillFacts f;
    f.present = true;
    f.pastDeadline = false;
    f.walkingOnPurpose = false;
    f.inCombat = false;
    f.inFlight = false;
    f.driftYards = 30.f;
    f.feetInLethalLiquid = inLava;
    return f;
}

}  // namespace

int main()
{
    // THE HOLD REGISTER. A drifted character is normally pinned back to where
    // it was held; in lava it is not, or the pin is what kills it.
    Check("a drifted character on dry ground is re-taken", RetakeTheHold(Held(false), 3.f), true);
    Check("a character in lava is never re-taken", RetakeTheHold(Held(true), 3.f), false);
    Check("a character in lava is not re-taken at zero slack either",
          RetakeTheHold(Held(true), 0.f), false);

    // THE WADE. A character 25 yards inside a lake of radius 40 walks to the
    // nearest shore, and the pick is dry, outside the lake, and no farther
    // than one stride past the shore.
    Lake const lake{0.f, 0.f, 40.f};
    WadeOutPick const pick = PickWadeOut(15.f, 20.f, 128.f, 60.f, 4.f, 16, lake);
    Check("a character in a lake finds dry ground", pick.found, true);
    float const fromCentre = std::hypot(pick.x, pick.y);
    Check("the pick is outside the lava", fromCentre > 40.f, true);
    float const shore = 40.f - std::hypot(15.f, 20.f);   // 15 yards to the shore
    CheckNear("the pick is about as far as the shore", pick.yards, shore, 4.5f);
    CheckNear("the pick carries the dry surface's height", pick.z, 134.f, 0.01f);

    // The nearest dry ground wins over a farther one: a character 5 yards from
    // the west shore is not sent across the lake.
    WadeOutPick const nearShore = PickWadeOut(-35.f, 0.f, 128.f, 60.f, 4.f, 16, lake);
    Check("a character near the shore finds dry ground", nearShore.found, true);
    Check("it is sent to the near shore, not across the lake", nearShore.x < -39.f, true);
    CheckNear("and the walk is short", nearShore.yards, 8.f, 4.1f);

    // Nothing within reach: reported as not found, never a made-up point.
    Lake const wide{0.f, 0.f, 500.f};
    Check("a lake wider than the search yields no pick",
          PickWadeOut(0.f, 0.f, 128.f, 60.f, 4.f, 16, wide).found, false);

    // Ground that cannot be measured is not dry ground.
    Lake holey{0.f, 0.f, 40.f};
    holey.hole = true;
    holey.holeX = 0.f;
    holey.holeY = 0.f;
    holey.holeRadius = 500.f;
    Check("unmeasurable points are not chosen",
          PickWadeOut(0.f, 0.f, 128.f, 60.f, 4.f, 16, holey).found, false);

    // Already dry: the first ring answers, and a caller never asks this of a
    // dry character, but the answer is still well formed.
    Check("a dry point has a pick within the first stride",
          PickWadeOut(100.f, 0.f, 134.f, 60.f, 4.f, 16, lake).found, true);

    // Degenerate settings answer not found rather than looping or dividing.
    Check("zero bearings finds nothing",
          PickWadeOut(0.f, 0.f, 128.f, 60.f, 4.f, 0, lake).found, false);
    Check("a zero stride finds nothing",
          PickWadeOut(0.f, 0.f, 128.f, 60.f, 0.f, 16, lake).found, false);

    // THE ADAPTER ASKS THE RULE, from each place that can hold a character.
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::printf("FAIL cannot read src/mod_overseer.cpp\n");
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::size_t const hold = text.find("void HoldAfterRevival(Player* bot, PlayerbotAI* botAI");
    std::size_t const holdEnd = text.find("void ReleaseRevivalHold(", hold);
    if (hold == std::string::npos || holdEnd == std::string::npos)
    {
        std::printf("FAIL HoldAfterRevival is not where the test expects it\n");
        return EXIT_FAILURE;
    }
    std::string const holdBody = text.substr(hold, holdEnd - hold);
    std::size_t const lavaAsk = holdBody.find("FeetInLethalLiquid(bot)");
    std::size_t const stay = holdBody.find("+stay");
    Check("the revival hold asks whether the feet are in lava", lavaAsk != std::string::npos, true);
    Check("the revival hold still takes the character's mover", stay != std::string::npos, true);
    Check("and asks before it takes the character's mover", lavaAsk < stay, true);

    std::size_t const sweep = text.find("static void KeepHeldCharactersStill()");
    std::size_t const sweepEnd = text.find("RetakeTheHold(facts", sweep);
    std::string const sweepBody = sweep == std::string::npos || sweepEnd == std::string::npos
                                      ? std::string()
                                      : text.substr(sweep, sweepEnd - sweep);
    Check("the hold sweep reads the feet before it re-takes a hold",
          Has(sweepBody, "facts.feetInLethalLiquid = "), true);

    Check("the wade-out drive exists", Has(text, "void DriveWadeOutOfLava()"), true);
    Check("and runs on the one-second terrain poll",
          Has(text, "DriveBelowTerrainRecovery();\n            DriveWadeOutOfLava();"), true);

    std::size_t const drive = text.find("void DriveWadeOutOfLava()");
    std::size_t const driveEnd = drive == std::string::npos ? drive : text.find("\n    }\n", drive);
    std::string const driveBody =
        drive == std::string::npos ? std::string() : text.substr(drive, driveEnd - drive);
    Check("the drive walks without a generated path, which would cross lava again",
          Has(driveBody, "/*generatePath*/ false"), true);
    Check("the drive picks through the shared decision",
          Has(driveBody, "OverseerDecisions::PickWadeOut("), true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("lava wade-out: all checks passed\n");
    return EXIT_SUCCESS;
}
