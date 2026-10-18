/*
 * The Draenei boat off Azuremyst had no berth (#1030). The dev realm logged
 * "'Ship, Night Elf (Elune's Blessing)' (181646) has 2 stop(s), 1 of them with
 * a surveyed berth level with its deck", and every outgrown Draenei's
 * cross-to-map map:1 was refused as having no route.
 *
 * WHY: both survey walks into the Valaar's Berth mooring node end at the water
 * line, z 0.57, under the pier, and the nearest deck is 3.74 above that. The
 * pier itself is where the world spawns two Draenei Commoners. LevelBerths
 * takes those spawns as candidates, only where no surveyed point is level, and
 * still puts them through PickBerth's reach and deck-step test.
 *
 * Every number below is read off the shipped data: the mod-playerbots travel
 * survey (node 1967 and its walk legs from nodes 2340 and 3711), the creature
 * spawns on map 530 within 40 yards of the mooring (19171, guids 121268 and
 * 121269), and the Elune's Blessing's crew on its own map 586.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <vector>

using OverseerDecisions::BerthCandidate;
using OverseerDecisions::BerthLimits;
using OverseerDecisions::LevelBerths;
using OverseerDecisions::PierSpawn;

namespace
{

int failures = 0;

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckAt(char const* what, std::vector<BerthCandidate> const& got, std::size_t i, float x,
             float y, float z)
{
    if (i >= got.size())
    {
        std::printf("FAIL %s: only %zu berth(s)\n", what, got.size());
        ++failures;
        return;
    }
    BerthCandidate const& b = got[i];
    if (std::fabs(b.x - x) > 0.01f || std::fabs(b.y - y) > 0.01f || std::fabs(b.z - z) > 0.01f)
    {
        std::printf("FAIL %s: got (%.2f, %.2f, %.2f), wanted (%.2f, %.2f, %.2f)\n", what, b.x,
                    b.y, b.z, x, y, z);
        ++failures;
    }
}

BerthLimits Berth()
{
    BerthLimits l;
    l.reachYards = 40.f;
    l.deckStepYards = 3.f;
    return l;
}

// The mooring node, (-4265, -11317.2, 0), and the deck levels it gives: the
// mooring's z plus each crew member's height on the ship.
float const MOORING_X = -4265.f;
float const MOORING_Y = -11317.2f;
std::vector<float> const ELUNES_BLESSING_DECKS = {16.6206f, 6.11067f, 6.11039f,
                                                  4.33103f, 4.31055f, 11.7088f};

// The last points before the mooring on both walks in: the water line.
std::vector<BerthCandidate> const VALAARS_SURVEY = {
    {-4263.74f, -11321.0f, 0.572327f},
    {-4262.48f, -11324.8f, 0.572327f},
    {-4259.96f, -11332.4f, 0.572327f},
};

PierSpawn Commoner(float x, float y, float z)
{
    PierSpawn s;
    s.x = x;
    s.y = y;
    s.z = z;
    s.groundWalker = true;
    s.hostileToCrew = false;
    return s;
}

std::vector<PierSpawn> ValaarsPier()
{
    return {Commoner(-4269.71f, -11336.5f, 5.80931f), Commoner(-4270.06f, -11333.1f, 5.57545f)};
}

void TheWaterLineIsStillNoBerth()
{
    CheckInt("the survey alone has no berth at Valaar's Berth",
             static_cast<long>(LevelBerths(VALAARS_SURVEY, {}, MOORING_X, MOORING_Y,
                                           ELUNES_BLESSING_DECKS, Berth())
                                   .size()),
             0);
}

void ThePierTheCommonersStandOnIsTheBerth()
{
    std::vector<BerthCandidate> const got = LevelBerths(
        VALAARS_SURVEY, ValaarsPier(), MOORING_X, MOORING_Y, ELUNES_BLESSING_DECKS, Berth());
    CheckInt("both commoners stand level with the deck", static_cast<long>(got.size()), 2);
    // 16.9 yards from the mooring before 20.1.
    CheckAt("the nearer commoner first", got, 0, -4270.06f, -11333.1f, 5.57545f);
    CheckAt("then the farther", got, 1, -4269.71f, -11336.5f, 5.80931f);
}

// A spawn is ground only when the creature walks, and a place to stand only
// when it would not meet the crew as an enemy; both still pass PickBerth.
void OnlyWalkersTheCrewWelcomesAndThatAreLevel()
{
    PierSpawn flier = Commoner(-4270.06f, -11333.1f, 5.57545f);
    flier.groundWalker = false;
    PierSpawn hostile = Commoner(-4270.06f, -11333.1f, 5.57545f);
    hostile.hostileToCrew = true;
    PierSpawn inTheWater = Commoner(-4262.f, -11325.f, 0.4f);
    PierSpawn upTheHill = Commoner(-4300.f, -11360.f, 6.f);  // 55 yards off
    CheckInt("a flier, a hostile, the water and a far hill are no berth",
             static_cast<long>(LevelBerths(VALAARS_SURVEY, {flier, hostile, inTheWater, upTheHill},
                                           MOORING_X, MOORING_Y, ELUNES_BLESSING_DECKS, Berth())
                                   .size()),
             0);
    CheckInt("no crew, no deck, no berth even with the commoners",
             static_cast<long>(
                 LevelBerths(VALAARS_SURVEY, ValaarsPier(), MOORING_X, MOORING_Y, {}, Berth())
                     .size()),
             0);
}

// Ratchet's survey reaches its pier, so a spawn nearer the mooring changes
// nothing: the berths a working crossing already uses are the ones it keeps.
void ASurveyedBerthIsNeverReplacedByASpawn()
{
    std::vector<BerthCandidate> const ratchet = {
        {-1005.02f, -3845.60f, 0.614098f},
        {-996.542f, -3818.65f, 6.04727f},
        {-1002.67f, -3826.93f, 5.44743f},
    };
    std::vector<float> const decks = {6.1f, 11.6f, 12.0f, 14.1f, 17.9f, 18.3f};
    std::vector<BerthCandidate> const got = LevelBerths(
        ratchet, {Commoner(-1004.f, -3838.f, 6.f)}, -1005.61f, -3841.65f, decks, Berth());
    CheckInt("only the two surveyed pier points", static_cast<long>(got.size()), 2);
    CheckAt("the pier's end first", got, 0, -1002.67f, -3826.93f, 5.44743f);
    CheckAt("then the pier further back", got, 1, -996.542f, -3818.65f, 6.04727f);
}

}  // namespace

int main()
{
    TheWaterLineIsStillNoBerth();
    ThePierTheCommonersStandOnIsTheBerth();
    OnlyWalkersTheCrewWelcomesAndThatAreLevel();
    ASurveyedBerthIsNeverReplacedByASpawn();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a stop the survey walked past the pier of takes its berth from the walkers "
                "standing on it, level with the deck, and never in place of a surveyed one\n");
    return 0;
}
