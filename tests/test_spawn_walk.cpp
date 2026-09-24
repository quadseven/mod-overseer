/*
 * The guild jobs' walks, decided without a world: a walk to a named spawn, and
 * a walk to any vendor that buys.
 *
 * WHAT IT IS FOR. A guild member's job is to farm: a maintenance member gathers
 * at a field its skill can open, and a guild warlock waits near the door it
 * summons at and grinds there between summons. The site chooses the place from
 * the world's own spawn table and names the spawn; the module walks the bot
 * there and lets it go. Selling what nobody in the guild needs takes a vendor
 * that buys, whatever it sells. What is pinned here:
 *
 *   - The `walk-to-spawn` grammar: exactly one of `gameobject:` and
 *     `creature:`, a non-zero id, an optional cap, each key once.
 *   - `walk-to-vendor any` beside `walk-to-vendor item:`, and never both.
 *   - Which refusals and endings name the spawn, that the other walks keep
 *     their literals, and which of the spawn's refusals are worth asking again.
 *   - When a walker is there.
 *   - In the adapter source: the dispatch on kind='job', the spawn read from the
 *     core's own spawn table, the hold lifted on arrival, and the vendor flag
 *     that rules a vendor out of a sale.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings; the source checks read src/mod_overseer.cpp as text.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace OverseerDecisions;
namespace E = OverseerDecisions::ErrandWalkRefusal;
namespace M = OverseerDecisions::MailWalkRefusal;
namespace S = OverseerDecisions::SpawnWalkRefusal;

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

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    if (std::fabs(got - want) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

void ASpawnWalkIsKnownByItsFirstWord()
{
    Check("a spawn walk", IsSpawnWalkRow("walk-to-spawn gameobject:12"), true);
    Check("a malformed spawn walk still routes", IsSpawnWalkRow("walk-to-spawn now"), true);
    Check("a job mode is not a spawn walk", IsSpawnWalkRow("farm"), false);
    Check("a prefix is not a spawn walk", IsSpawnWalkRow("walk-to-spawns gameobject:1"), false);
    Check("a trainer walk is not a spawn walk", IsSpawnWalkRow("walk-to-trainer skill:182"),
          false);
    Check("a spawn walk is not a trainer walk", IsTrainerWalkRow("walk-to-spawn creature:5"),
          false);
    Check("a spawn walk is not a mailbox walk", IsMailWalkRow("walk-to-spawn creature:5"),
          false);
}

void TheSpawnGrammar()
{
    SpawnWalkRequest const node = ParseSpawnWalkRequest("walk-to-spawn gameobject:48213");
    CheckText("a node parses", node.error, "");
    Check("a gameobject spawn", node.gameObject, true);
    CheckNumber("the spawn id is read", node.spawn, 48213);
    CheckNear("the default cap is the errand cap", node.maxYards, ERRAND_WALK_MAX_YARDS);

    SpawnWalkRequest const mob = ParseSpawnWalkRequest("walk-to-spawn max:8000 creature:7");
    CheckText("keys in any order", mob.error, "");
    Check("a creature spawn", mob.gameObject, false);
    CheckNumber("the creature spawn id", mob.spawn, 7);
    CheckNear("a far cap is read", mob.maxYards, 8000.f);

    for (char const* row : {
             "walk-to-spawn",
             "walk-to-spawn max:500",
             "walk-to-spawn gameobject:0",
             "walk-to-spawn gameobject:-3",
             "walk-to-spawn gameobject:12 creature:13",
             "walk-to-spawn gameobject:12 gameobject:13",
             "walk-to-spawn gameobject:12 max:0",
             "walk-to-spawn gameobject:12 max:30000",
             "walk-to-spawn gameobject:12 max:500 max:600",
             "walk-to-spawn node:12",
             "walk-to-spawn gameobject:12 now",
             "walk-to-spawn gameobject:",
             "walk-to-trainer gameobject:12",
         })
    {
        SpawnWalkRequest const r = ParseSpawnWalkRequest(row);
        if (std::string(r.error) != S::MalformedSpawn)
        {
            std::printf("FAIL '%s' should be malformed, got '%s'\n", row, r.error);
            ++failures;
        }
        if (r.spawn != 0)
        {
            std::printf("FAIL '%s' refused but carried spawn %u\n", row, r.spawn);
            ++failures;
        }
    }
}

void AnyVendorThatBuys()
{
    VendorWalkRequest const any = ParseVendorWalkRequest("walk-to-vendor any");
    CheckText("any parses", any.error, "");
    Check("any is read", any.any, true);
    CheckNumber("any carries no item", any.item, 0);

    VendorWalkRequest const capped = ParseVendorWalkRequest("walk-to-vendor max:900 any");
    CheckText("any with a cap, in any order", capped.error, "");
    CheckNear("its cap", capped.maxYards, 900.f);

    VendorWalkRequest const item = ParseVendorWalkRequest("walk-to-vendor item:2901");
    Check("an item walk is not any", item.any, false);
    CheckNumber("an item walk keeps its item", item.item, 2901);

    for (char const* row : {"walk-to-vendor any item:2901", "walk-to-vendor item:2901 any",
                            "walk-to-vendor any any", "walk-to-vendor anything",
                            "walk-to-vendor any:1"})
    {
        VendorWalkRequest const r = ParseVendorWalkRequest(row);
        if (std::string(r.error) != E::MalformedVendor)
        {
            std::printf("FAIL '%s' should be malformed, got '%s'\n", row, r.error);
            ++failures;
        }
    }
}

void RefusalsNameTheSpawn()
{
    CheckText("goal word", WalkGoalWord(WalkGoal::Spawn), "spawn");
    CheckNear("a spawn walk's near cap", NearWalkCapYards(WalkGoal::Spawn),
              ERRAND_WALK_MAX_YARDS);
    Check("past the errand cap is far", IsFarWalk(WalkGoal::Spawn, 1500.f), true);

    CheckText("malformed", WalkRefusalFor(WalkGoal::Spawn, M::Malformed), S::MalformedSpawn);
    CheckText("nothing to walk to", WalkRefusalFor(WalkGoal::Spawn, M::NoMailboxOnMap),
              S::NoSuchSpawn);
    CheckText("too far", WalkRefusalFor(WalkGoal::Spawn, M::MailboxTooFar), S::SpawnTooFar);
    CheckText("across the line", WalkRefusalFor(WalkGoal::Spawn, M::OtherSidesGround),
              S::SpawnOtherSide);
    CheckText("the ground", WalkRefusalFor(WalkGoal::Spawn, M::GroundRefused), S::SpawnGround);
    CheckText("any walk under way", WalkRefusalFor(WalkGoal::Spawn, M::AlreadyWalking),
              E::AlreadyWalking);
    CheckText("the character's own wall passes through",
              WalkRefusalFor(WalkGoal::Spawn, M::OnRoster), M::OnRoster);

    CheckText("timeout", WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::TimedOut),
              S::SpawnTimedOut);
    CheckText("combat", WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::EnteredCombat),
              S::SpawnCombat);
    CheckText("died", WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::Died), S::SpawnDied);
    CheckText("stalled", WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::Stalled),
              S::SpawnStalled);
    CheckText("an arrival has no reason",
              WalkEndReasonFor(WalkGoal::Spawn, MailWalkState::Arrived), "");

    // The walks that were already there keep every literal.
    CheckText("the trainer keeps its sentence",
              WalkRefusalFor(WalkGoal::Trainer, M::MailboxTooFar), E::TrainerTooFar);
    CheckText("the vendor keeps its sentence",
              WalkRefusalFor(WalkGoal::Vendor, M::NoMailboxOnMap), E::NoVendorOnMap);
    CheckText("the vendor keeps its ending",
              WalkEndReasonFor(WalkGoal::Vendor, MailWalkState::Stalled), E::VendorStalled);
    CheckText("the mailbox keeps its ending",
              WalkEndReasonFor(WalkGoal::Mailbox, MailWalkState::Died), M::Died);
}

void WhichSpawnRefusalsMove()
{
    Check("too far moves: a random bot wanders", ErrandWalkRefusalRetryable(S::SpawnTooFar),
          true);
    Check("the ground moves", ErrandWalkRefusalRetryable(S::SpawnGround), true);
    Check("a fight on the way moves", ErrandWalkRefusalRetryable(S::SpawnCombat), true);
    Check("a death on the way moves", ErrandWalkRefusalRetryable(S::SpawnDied), true);
    Check("the clock moves", ErrandWalkRefusalRetryable(S::SpawnTimedOut), true);
    Check("malformed does not", ErrandWalkRefusalRetryable(S::MalformedSpawn), false);
    Check("no such spawn does not", ErrandWalkRefusalRetryable(S::NoSuchSpawn), false);
    Check("another map does not", ErrandWalkRefusalRetryable(S::SpawnOtherMap), false);
    Check("out of season does not", ErrandWalkRefusalRetryable(S::SpawnOutOfSeason), false);
    Check("the other side's ground does not", ErrandWalkRefusalRetryable(S::SpawnOtherSide),
          false);
}

void ThereIsNearTheSpawn()
{
    Check("on the spawn", SpawnWalkArrived(0.f), true);
    Check("at the edge", SpawnWalkArrived(SPAWN_WALK_ARRIVE_YARDS), true);
    Check("just past it", SpawnWalkArrived(SPAWN_WALK_ARRIVE_YARDS + 0.5f), false);
    Check("an unread distance never arrives", SpawnWalkArrived(-1.f), false);
}

std::string Source()
{
    std::ifstream file("src/mod_overseer.cpp");
    if (!file)
    {
        Check("the adapter source is readable", false, true);
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void TheAdapterWiresIt()
{
    std::string const text = Source();
    Check("kind='job' routes a spawn walk before the roster's job modes",
          text.find("kind == \"job\" && OverseerDecisions::IsSpawnWalkRow(command))")
              < text.find("else if (kind == \"job\")\n"),
          true);
    Check("the gameobject spawn is read from the core's spawn table",
          text.find("sObjectMgr->GetGameObjectData(ev.spawnId)") != std::string::npos, true);
    Check("the creature spawn is read from the core's spawn table",
          text.find("sObjectMgr->GetCreatureData(ev.spawnId)") != std::string::npos, true);
    Check("a spawn on another map is refused",
          text.find("return refuse(S::SpawnOtherMap);") != std::string::npos, true);
    std::size_t const arrival = text.find("state == D::MailWalkState::Arrived && ev.goal == "
                                          "D::WalkGoal::Spawn");
    Check("a spawn walk has its own arrival", arrival != std::string::npos, true);
    std::size_t const release =
        text.find("ReleaseHold(check.targetName, bot, \"the walk to its work is over\"", arrival);
    std::size_t const next = text.find("else if (state == D::MailWalkState::Arrived)", arrival);
    Check("the arrival lets the bot go before any other arrival branch",
          arrival != std::string::npos && release < next, true);
    Check("a sale walk rules out a vendor that refuses sales",
          text.find("return !(tmpl->flags_extra & CREATURE_FLAG_EXTRA_NO_SELL_VENDOR);")
              != std::string::npos,
          true);
}

}  // namespace

int main()
{
    ASpawnWalkIsKnownByItsFirstWord();
    TheSpawnGrammar();
    AnyVendorThatBuys();
    RefusalsNameTheSpawn();
    WhichSpawnRefusalsMove();
    ThereIsNearTheSpawn();
    TheAdapterWiresIt();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the spawn walk and the sale walk hold\n");
    return EXIT_SUCCESS;
}
