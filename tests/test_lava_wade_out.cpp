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
 * THE REPEAT (#1638). The nearest dry sample can be a rock inside the lava
 * field. A character was sent to one, stood in lava again seconds later, and
 * was sent to the next, five times in 80 seconds, 4 and 8 yards at a time. A
 * wade that follows another within a minute searches further out and asks for
 * dry ground round the point, and every point is read once more from its own
 * height before it is chosen.
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
using OverseerDecisions::PickWadeOutRepeat;
using OverseerDecisions::RetakeTheHold;
using OverseerDecisions::WadeOutPick;
using OverseerDecisions::WadeRepeats;

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

// A lava field with dry rocks standing in it: lava inside the disc except on
// a rock, dry ground outside the disc.
struct Rock
{
    float x, y, radius;
};
struct RockyField
{
    float radius;
    Rock rocks[3];
    float lavaZ = 128.f;
    float rockZ = 128.5f;
    float shoreZ = 129.f;

    bool OnRock(float x, float y, float slack = 0.f) const
    {
        for (Rock const& rock : rocks)
            if (std::hypot(x - rock.x, y - rock.y) <= rock.radius + slack)
                return true;
        return false;
    }

    bool operator()(float x, float y, float /*from*/, float& out, bool& lethal) const
    {
        lethal = false;
        if (std::hypot(x, y) > radius)
        {
            out = shoreZ;
            return true;
        }
        if (OnRock(x, y))
        {
            out = rockZ;
            return true;
        }
        out = lavaZ;
        lethal = true;
        return true;
    }
};

// A lake with a shelf over its east side: read from the feet the shelf is the
// first surface found and it is dry, read from above the shelf a second lava
// flow lies over it. Everything else is the lake.
struct ShelfLake
{
    Lake lake;
    float shelfZ = 136.f;
    mutable int readsFromAboveTheShelf = 0;

    bool operator()(float x, float y, float from, float& out, bool& lethal) const
    {
        if (std::hypot(x - 4.f, y) <= 1.f)
        {
            if (from > shelfZ + 5.f)
            {
                ++readsFromAboveTheShelf;
                out = shelfZ + 8.f;
                lethal = true;
                return true;
            }
            lethal = false;
            out = shelfZ;
            return true;
        }
        return lake(x, y, from, out, lethal);
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

    // THE RE-CHECK (#1638). A point is read once more from above its own
    // height, the way the feet check reads a character standing on it. A point
    // that is lava on that read is passed over for the next dry one.
    ShelfLake const shelf{Lake{0.f, 0.f, 40.f}};
    WadeOutPick const pastShelf = PickWadeOut(0.f, 0.f, 128.f, 60.f, 4.f, 16, shelf);
    Check("the shelf was read again from above its own height",
          shelf.readsFromAboveTheShelf > 0, true);
    Check("a lake with a shelf that is lava on re-check still finds dry ground",
          pastShelf.found, true);
    Check("and the pick is the shore, not the shelf",
          std::hypot(pastShelf.x, pastShelf.y) > 40.f, true);

    // THE REPEAT WINDOW (#1638).
    Check("a first wade is no repeat", WadeRepeats(false, 0, 60, 0, 3) == 0, true);
    Check("a wade 12 s after the last is one repeat", WadeRepeats(true, 12, 60, 0, 3) == 1, true);
    Check("a wade on the window's last second still counts",
          WadeRepeats(true, 60, 60, 0, 3) == 1, true);
    Check("each repeat inside the window adds one", WadeRepeats(true, 12, 60, 1, 3) == 2, true);
    Check("the count stops at the cap", WadeRepeats(true, 12, 60, 3, 3) == 3, true);
    Check("a wade after the window starts over", WadeRepeats(true, 61, 60, 2, 3) == 0, true);
    Check("a clock that ran backwards starts over", WadeRepeats(true, -5, 60, 2, 3) == 0, true);

    // TWO WADES FROM ONE FIELD (#1638). The field is 40 yards across with
    // rocks in it, one of them a stride from the character and two more on the
    // rings the second search starts from. The first wade may take the near
    // rock: it is the nearest dry ground measured. The character stands in
    // lava again 12 seconds later, a yard or two away, and the second wade
    // must not be sent to a rock: its aim is outside the field.
    RockyField const field{40.f, {{4.f, 0.f, 1.5f}, {11.f, 1.f, 1.5f}, {3.f, 13.f, 1.5f}}};
    unsigned const firstRepeats = WadeRepeats(false, 0, 60, 0, 3);
    WadeOutPick const first =
        PickWadeOutRepeat(0.f, 0.f, 128.f, 60.f, 4.f, 16, firstRepeats, field);
    Check("the first wade finds dry ground", first.found, true);
    CheckNear("the first wade is the plain nearest pick", first.yards, 4.f, 0.01f);
    unsigned const secondRepeats = WadeRepeats(true, 12, 60, firstRepeats, 3);
    WadeOutPick const second =
        PickWadeOutRepeat(3.f, 1.f, 128.f, 60.f, 4.f, 16, secondRepeats, field);
    Check("the second wade finds dry ground", second.found, true);
    Check("the second wade's aim is outside the lava field",
          std::hypot(second.x, second.y) > 40.f, true);
    Check("the second wade's aim is not a rock in the field",
          field.OnRock(second.x, second.y, 2.f), false);
    Check("the second wade starts its search further out", second.yards >= 8.f, true);
    CheckNear("and its aim has a stride of dry ground measured round it", second.clearYards, 4.f,
              0.01f);
    // The stateless pick is what repeated: from the second standing point it
    // answers a rock in the field again, which is the finding.
    WadeOutPick const stateless = PickWadeOut(3.f, 1.f, 128.f, 60.f, 4.f, 16, field);
    Check("without the repeat the same point is sent to dry ground inside the field",
          stateless.found && std::hypot(stateless.x, stateless.y) < 40.f, true);

    // A third wade in the window asks for more room still, and still lands
    // outside the field.
    WadeOutPick const third = PickWadeOutRepeat(
        3.f, 1.f, 128.f, 60.f, 4.f, 16, WadeRepeats(true, 9, 60, secondRepeats, 3), field);
    Check("a third wade finds dry ground outside the field",
          third.found && std::hypot(third.x, third.y) > 40.f, true);
    CheckNear("with two strides of dry ground round it", third.clearYards, 8.f, 0.01f);

    // No shore in reach: a repeat steps back down to the nearest dry point
    // rather than leaving the character standing in lava.
    RockyField const sea{500.f, {{4.f, 0.f, 1.5f}, {4.f, 0.f, 1.5f}, {4.f, 0.f, 1.5f}}};
    WadeOutPick const onlyRock = PickWadeOutRepeat(0.f, 0.f, 128.f, 60.f, 4.f, 16, 2, sea);
    Check("a repeat with no shore in reach still takes the one rock", onlyRock.found, true);
    CheckNear("and says no dry ground was measured round it", onlyRock.clearYards, 0.f, 0.01f);
    Check("a repeat with nothing dry in reach finds nothing",
          PickWadeOutRepeat(0.f, 0.f, 128.f, 60.f, 4.f, 16, 3, wide).found, false);
    Check("a repeat with a zero stride finds nothing",
          PickWadeOutRepeat(0.f, 0.f, 128.f, 60.f, 0.f, 16, 3, lake).found, false);

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
          Has(driveBody, "OverseerDecisions::PickWadeOutRepeat("), true);
    // THE REPEAT MEMORY (#1638) outlives the wade: it is written when the
    // character is sent, read through the shared decision, and dropped only
    // once the window has passed, not the moment the feet read dry.
    Check("the drive counts repeats through the shared decision",
          Has(driveBody, "OverseerDecisions::WadeRepeats("), true);
    Check("the drive remembers each wade it issues",
          Has(driveBody, "_wadeLast[name] = {now, repeats};"), true);
    Check("the drive keeps that memory for the repeat window after the feet read dry",
          Has(driveBody, "now - last->second.first > WADE_REPEAT_WINDOW_SECONDS)\n"
                         "                    _wadeLast.erase(last);"),
          true);
    Check("and drops it nowhere else", driveBody.find("_wadeLast.erase(") ==
                                           driveBody.rfind("_wadeLast.erase("), true);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("lava wade-out: all checks passed\n");
    return EXIT_SUCCESS;
}
