/*
 * A spawn walk to a quest giver or taker that has walked off its spawn point
 * follows the creature instead of stopping at the empty point
 * (mod-overseer#1392).
 *
 * A spawn walk arrives at the spawn row's own position. A giver that patrols
 * is not there: on the dev realm a member "reached" a patrolling giver at 2.9
 * yards from its spawn point, and five seconds later the take row found the
 * living giver 112.5 yards off and refused "no giver of that quest in reach".
 * All 27 such refusals of 2026-11-11 to 2026-11-15 came within 10 seconds of
 * a `reached` line. The decision is pinned here; the adapter's wiring is
 * pinned in the source because it reads a live creature.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::string Read(char const* path)
{
    std::ifstream source(path);
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
}

SpawnWalkGiverFacts Giver(float yards, uint32_t followedMs = 0)
{
    SpawnWalkGiverFacts facts;
    facts.questHolderCreature = true;
    facts.giverSeen = true;
    facts.giverYards = yards;
    facts.followedMs = followedMs;
    return facts;
}

void AWanderingGiverIsFollowed()
{
    // The live case: at the spawn point, the living giver 112.5 yards off.
    Expect(SpawnWalkGiverStepFor(Giver(112.5f)) == SpawnWalkGiverStep::Follow,
           "a living giver 112.5 yards from its spawn point is followed, not left");
    Expect(SpawnWalkGiverStepFor(Giver(24.2f)) == SpawnWalkGiverStep::Follow,
           "a giver just outside the take reach is followed");
    Expect(SpawnWalkGiverStepFor(Giver(SPAWN_WALK_GIVER_STAND_YARDS + 0.5f)) ==
               SpawnWalkGiverStep::Follow,
           "the walk closes to the stand distance, so the giver's next steps stay in reach");
}

void TheWalkEndsBesideTheGiver()
{
    Expect(SpawnWalkGiverStepFor(Giver(SPAWN_WALK_GIVER_STAND_YARDS)) ==
               SpawnWalkGiverStep::AtTheGiver &&
               SpawnWalkGiverStepFor(Giver(0.2f)) == SpawnWalkGiverStep::AtTheGiver,
           "within the stand distance of the living giver the walk has arrived");
    Expect(SpawnWalkGiverStepFor(Giver(2.f, SPAWN_WALK_GIVER_FOLLOW_SECONDS * 1000u)) ==
               SpawnWalkGiverStep::AtTheGiver,
           "beside the giver is arrived even when the follow clock has run out");
    Expect(SPAWN_WALK_GIVER_STAND_YARDS > 0.f &&
               SPAWN_WALK_GIVER_STAND_YARDS < SPAWN_WALK_ARRIVE_YARDS / 2.f,
           "the stand distance leaves more than half the take reach for the giver to walk on");
}

void TheFollowIsBounded()
{
    Expect(SpawnWalkGiverStepFor(Giver(60.f, SPAWN_WALK_GIVER_FOLLOW_SECONDS * 1000u)) ==
               SpawnWalkGiverStep::Place,
           "a giver not caught in the follow seconds ends the walk at the place, as before");
    Expect(SpawnWalkGiverStepFor(Giver(60.f, SPAWN_WALK_GIVER_FOLLOW_SECONDS * 1000u - 1u)) ==
               SpawnWalkGiverStep::Follow,
           "the follow runs up to its bound");
    Expect(SPAWN_WALK_GIVER_FOLLOW_SECONDS > 0 && SPAWN_WALK_GIVER_FOLLOW_SECONDS <= 120,
           "the follow is bounded in seconds");
    SpawnWalkGiverFacts faltering = Giver(60.f);
    faltering.walkFaltering = true;
    Expect(SpawnWalkGiverStepFor(faltering) == SpawnWalkGiverStep::Place,
           "a walk whose own clocks are nearly spent ends at the place, never as an error");
    Expect(SpawnWalkGiverStepFor(Giver(SPAWN_WALK_GIVER_SEEN_YARDS + 1.f)) ==
               SpawnWalkGiverStep::Place,
           "a giver farther than the seen distance is not chased across the map");
}

void EverythingElseIsStillAPlace()
{
    SpawnWalkGiverFacts node = Giver(112.5f);
    node.questHolderCreature = false;
    Expect(SpawnWalkGiverStepFor(node) == SpawnWalkGiverStep::Place,
           "a node, a mob or a quest object is a place: there once at its spawn point");
    SpawnWalkGiverFacts gone;
    gone.questHolderCreature = true;
    Expect(SpawnWalkGiverStepFor(gone) == SpawnWalkGiverStep::Place,
           "a giver that is dead or not in the world is not followed");
    SpawnWalkGiverFacts unread = Giver(-1.f);
    Expect(SpawnWalkGiverStepFor(unread) == SpawnWalkGiverStep::Place,
           "an unread distance never counts as beside the giver");
}

void TheAdapterFollowsTheLivingGiver()
{
    std::string const src = Read("src/mod_overseer.cpp");
    std::size_t const fn = src.find("static bool SpawnWalkAtTheGiver(");
    Expect(fn != std::string::npos, "the adapter has the giver step");
    Expect(src.find("GetCreatureBySpawnIdStore()", fn) != std::string::npos &&
               src.find("SpawnIsQuestHolder(false, ev.mailboxEntry)", fn) != std::string::npos &&
               src.find("OverseerDecisions::SpawnWalkGiverStepFor(facts)", fn) !=
                   std::string::npos,
           "it reads the spawn's own living creature and asks the decision");

    std::size_t const resolve = src.find("void ResolveMailWalks(uint32 elapsedMs)");
    std::size_t const inReach =
        src.find("DestinationInReach(bot, ev, ev.reachedName, ev.reachedYards);", resolve);
    std::size_t const step = src.find("SpawnWalkAtTheGiver(bot, check, elapsedMs)", resolve);
    std::size_t const judge = src.find("D::JudgeMailWalk(facts);", resolve);
    Expect(resolve != std::string::npos && inReach != std::string::npos &&
               step != std::string::npos && inReach < step && step < judge,
           "the walk loop asks it after the place is reached and before the walk is judged");

    Expect(src.find("bool const surveyed = ev.far && !viaPad && !ev.followsGiver;") !=
               std::string::npos,
           "a walk following a giver walks straight legs, not the survey to the old point");
}

}  // namespace

int main()
{
    AWanderingGiverIsFollowed();
    TheWalkEndsBesideTheGiver();
    TheFollowIsBounded();
    EverythingElseIsStillAPlace();
    TheAdapterFollowsTheLivingGiver();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("quest giver follow: ok");
    return 0;
}
