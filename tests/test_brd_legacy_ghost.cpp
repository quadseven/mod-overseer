/*
 * A ghost at Blackrock Mountain walks to Franclorn Forgewright first
 * (wow-overseer#2021).
 *
 * The Shadowforge Key's one source, Dark Iron Legacy, starts only with a ghost
 * within 20 yards of Franclorn Forgewright, about 400 yards from the graveyard
 * a Blackrock Depths ghost lands at and 120 yards above it. No corpse run
 * passed him, so a family with no lockpicker never took the chain. The rules
 * in src/overseer_brd_legacy_ghost.h send such a ghost there once; the
 * adapter's wiring is pinned in the source because it needs a worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_legacy_ghost.h"

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

// A ghost at the Blackrock Mountain graveyard (about 405 yards flat from
// Franclorn, 120 below him) whose corpse lies inside Blackrock Depths, of a
// keyless, lockpicker-less family with a Blackrock Depths order.
LegacyGhostFacts AtTheGraveyard()
{
    LegacyGhostFacts f;
    f.ghost = true;
    f.level = 54;
    f.ghostMap = 0;
    f.ghostFlatYards = 405.f;
    f.ghostYards = 422.f;
    f.corpseMap = 230;
    f.familyBrdOrder = true;
    return f;
}

void TheKeylessFamilyIsSent()
{
    LegacyGhostFacts f = AtTheGraveyard();
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Walk,
           "a ghost of a keyless, lockpicker-less family with a BRD order walks to Franclorn");

    f.corpseMap = 0;
    f.corpseFlatYards = 300.f;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Walk,
           "a death on the mountain itself (corpse on map 0 nearby) walks there too");

    f = AtTheGraveyard();
    f.level = LEGACY_MIN_LEVEL;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Walk, "level 48 is old enough for the chain");
}

void WhoIsNotSent()
{
    LegacyGhostFacts f = AtTheGraveyard();
    f.familyHasKey = true;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a family with the key, or with 3802 taken, is not sent");

    f = AtTheGraveyard();
    f.familyHasLockpicker = true;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a family with a lockpicker at 250 is not sent");

    f = AtTheGraveyard();
    f.level = LEGACY_MIN_LEVEL - 1;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None, "a ghost under level 48 is not sent");

    f = AtTheGraveyard();
    f.familyBrdOrder = false;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a family with no Blackrock Depths order is not sent");

    f = AtTheGraveyard();
    f.ghost = false;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None, "an unreleased corpse is not sent");

    f = AtTheGraveyard();
    f.ghostFlatYards = 2000.f;
    Expect(!LegacyGhostAtMountain(f) && LegacyGhostNext(f) == LegacyGhostStep::None,
           "a ghost far from Blackrock Mountain is not sent");

    f = AtTheGraveyard();
    f.ghostMap = 1;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None, "a ghost on another continent is not sent");

    f = AtTheGraveyard();
    f.corpseMap = 0;
    f.corpseFlatYards = 3000.f;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a ghost passing the mountain with its corpse far away is not sent");

    f = AtTheGraveyard();
    f.corpseMap = 229;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a ghost whose corpse is in another instance is not sent");

    f = AtTheGraveyard();
    f.spent = true;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::None,
           "a member whose detour already ended is not sent again");
}

void TheWalkAndTheWait()
{
    LegacyGhostFacts f = AtTheGraveyard();
    f.begun = true;
    f.walkSeconds = 200;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Walk, "a walk under its allowance goes on");

    f.walkSeconds = LEGACY_WALK_SECONDS;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Done,
           "a walk that never reached him ends, so the corpse run is not lost for good");

    f = AtTheGraveyard();
    f.begun = true;
    f.walkSeconds = 300;
    f.ghostYards = 8.f;
    f.ghostFlatYards = 6.f;
    f.atFranclornSeconds = 30;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Wait,
           "in reach, it stands at Franclorn while the bridge's rows take the chain");

    f.walkSeconds = LEGACY_WALK_SECONDS + 100;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Wait,
           "the walk's allowance does not cut short the wait at Franclorn");

    f.atFranclornSeconds = LEGACY_WAIT_SECONDS;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Done,
           "a wait that saw no quest taken ends after its allowance");

    f = AtTheGraveyard();
    f.begun = true;
    f.ghostYards = 8.f;
    f.atFranclornSeconds = 40;
    f.familyHasKey = true;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Done,
           "once 3802 is taken the detour is over and the corpse run resumes");

    f = AtTheGraveyard();
    f.begun = true;
    f.spent = true;
    f.walkSeconds = 10;
    Expect(LegacyGhostNext(f) == LegacyGhostStep::Walk,
           "a detour in progress is not ended by the spent mark of an earlier one");

    Expect(LEGACY_ARRIVE_YARDS < 20.f,
           "the walk ends inside the bridge's 20-yard reach for the chain rows");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(src.find("#include \"overseer_brd_legacy_ghost.h\"") != std::string::npos,
           "the adapter includes the rules");

    std::size_t const drive = src.find("bool DriveLegacyGhostDetour(Player* bot");
    std::size_t const end = src.find("\n    }\n", drive);
    auto within = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, drive);
        return drive != std::string::npos && at != std::string::npos && at < end;
    };
    Expect(within("D::LegacyGhostNext(facts)"), "it asks the rules");
    Expect(within("bot->GetCorpseLocation()"),
           "it reads the corpse across maps, not the map-scoped GetCorpse");
    Expect(within("ReadLegacyFamily(name, facts)"), "it reads the family's order, key and lockpicks");
    Expect(within("motion->MovePoint(LEGACY_GHOST_POINT_ID") &&
               within("/*generatePath*/ true, /*forceDestination*/ false"),
           "the ghost walks on a path, never a forced straight line");
    Expect(within("LeaseCorpseRun(bot, botAI, st.lease, true)") &&
               within("LegacyDetourEnd(botAI, name)"),
           "the dead engine is leased for the walk and handed back after");

    std::size_t const family = src.find("void ReadLegacyFamily(");
    std::size_t const familyEnd = src.find("\n    }\n", family);
    auto inFamily = [&](char const* needle)
    {
        std::size_t const at = src.find(needle, family);
        return family != std::string::npos && at != std::string::npos && at < familyEnd;
    };
    Expect(inFamily("keyword = 'blackrock-depths'") && inFamily("overseer_dungeon_run"),
           "a queued or active order, or an active run on map 230, is the BRD order");
    Expect(inFamily("GetItemCount(D::LEGACY_SHADOWFORGE_KEY, true)") &&
               inFamily("GetQuestStatus(D::LEGACY_KEY_QUEST)") &&
               inFamily("GetQuestRewardStatus(D::LEGACY_KEY_QUEST)") &&
               inFamily("GetSkillValue(SKILL_LOCKPICKING) >= D::LEGACY_LOCKPICKING"),
           "the key, 3802 held or rewarded, and lockpicking 250 are read off every member");

    // The stuck-revival drive asks the detour before any other recovery of a
    // ghost, and ends it when the member is alive again.
    std::size_t const revival = src.find("void DriveStuckRevival()");
    std::size_t const detour = src.find("if (DriveLegacyGhostDetour(bot, botAI, name))", revival);
    std::size_t const recovery = src.find("if (DriveGhostRecovery(bot, botAI, name, corpse))", revival);
    std::size_t const alive = src.find("LegacyDetourEnd(botAI, name);", revival);
    Expect(revival != std::string::npos && detour != std::string::npos &&
               recovery != std::string::npos && detour < recovery,
           "DriveStuckRevival asks the detour before the corpse-run decision and the ladder");
    Expect(alive != std::string::npos && alive < detour,
           "a member alive again ends its detour");
}

}  // namespace

int main()
{
    TheKeylessFamilyIsSent();
    WhoIsNotSent();
    TheWalkAndTheWait();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Franclorn detour check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_legacy_ghost: ok\n");
    return 0;
}
