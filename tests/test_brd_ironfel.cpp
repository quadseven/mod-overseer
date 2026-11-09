/*
 * The Dark Iron Legacy carrier loots Ironfel from Fineous Darkvire's corpse
 * (mod-overseer#1454).
 *
 * Ironfel is a quest-required, 100 percent drop that the core offers only to a
 * member holding Dark Iron Legacy (3802) and only when that member opens the
 * corpse. The first Blackrock Depths run to kill Fineous with such a member in
 * the party was pulled into the next packs, the dungeon brain's loot yield timed
 * out, and no Ironfel reached any bag, so the Shrine step (#1391) never began.
 * The rules in src/overseer_brd_ironfel.h pick the member, judge the walk and
 * end it; the adapter's wiring is pinned in the source because it needs a
 * worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_ironfel.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

BrdIronfelHolder Member(char const* name, bool legacy, unsigned ironfel, bool alive = true)
{
    BrdIronfelHolder h;
    h.name = name;
    h.aliveInside = alive;
    h.holdsLegacy = legacy;
    h.ironfel = ironfel;
    return h;
}

void TheWanter()
{
    std::vector<BrdIronfelHolder> party = {Member("Zug", false, 0), Member("Oz", true, 0),
                                           Member("Uzza", false, 0)};
    Expect(BrdIronfelWanter(BRD_MAP_ID, party) == "Oz",
           "a blackrock-depths run with a 3802 holder who has no Ironfel gets the pickup");
    Expect(BrdIronfelWanter(36, party).empty(), "no other dungeon gets it");

    std::vector<BrdIronfelHolder> none = {Member("Zug", false, 0), Member("Oz", false, 0)};
    Expect(BrdIronfelWanter(BRD_MAP_ID, none).empty(), "no 3802 holder: nobody wants Ironfel");
    std::vector<BrdIronfelHolder> has = {Member("Zug", false, 0), Member("Oz", true, 1)};
    Expect(BrdIronfelWanter(BRD_MAP_ID, has).empty(),
           "a 3802 holder who already carries Ironfel wants none (the Shrine step is its turn)");
    std::vector<BrdIronfelHolder> dead = {Member("Oz", true, 0, false)};
    Expect(BrdIronfelWanter(BRD_MAP_ID, dead).empty(), "a dead holder is walked nowhere");
    std::vector<BrdIronfelHolder> two = {Member("Zug", true, 0), Member("Oz", true, 0)};
    Expect(BrdIronfelWanter(BRD_MAP_ID, two) == "Zug", "the first wanter in the party's order");
}

void TheReach()
{
    Expect(BrdIronfelAtCorpse(0.f) && BrdIronfelAtCorpse(BRD_IRONFEL_LOOT_YARDS),
           "within the loot reach of the corpse is at it");
    Expect(!BrdIronfelAtCorpse(BRD_IRONFEL_LOOT_YARDS + 0.1f) && !BrdIronfelAtCorpse(300.f),
           "beyond it is not");
    Expect(!BrdIronfelAtCorpse(-1.f), "an unread distance never counts");
    Expect(BRD_IRONFEL_LOOT_YARDS < 5.5f,
           "the reach is inside the core's 5.5 yard interaction distance, so a Loot step opens");
    Expect(BrdIronfelClose(BRD_IRONFEL_CLOSE_YARDS) && !BrdIronfelClose(BRD_IRONFEL_CLOSE_YARDS + 1.f),
           "the last stretch is the member's own inside 25 yards");
}

BrdIronfelFacts Facts(char const* wanter, bool fineousDead, bool offers, float yards,
                      std::time_t now, bool fighting = false)
{
    BrdIronfelFacts f;
    f.wanter = wanter;
    f.fineousDead = fineousDead;
    f.corpseOffers = offers;
    f.yards = yards;
    f.now = now;
    f.wanterInCombat = fighting;
    return f;
}

void TheWalk()
{
    BrdIronfelClock clock;
    BrdIronfelVerdict v = BrdIronfelNext(Facts("", true, true, 30.f, 1000), clock);
    Expect(v.step == BrdIronfelStep::Nothing && !v.clock.started, "nobody wants it: nothing");

    v = BrdIronfelNext(Facts("Oz", false, false, -1.f, 1000), clock);
    Expect(v.step == BrdIronfelStep::Nothing && !v.clock.started,
           "Fineous Darkvire alive: the run does not walk yet");

    v = BrdIronfelNext(Facts("Oz", true, false, 40.f, 1000), clock);
    Expect(v.step == BrdIronfelStep::Nothing && !v.clock.started,
           "Fineous dead but his corpse does not offer Ironfel to the member: nothing");

    v = BrdIronfelNext(Facts("Oz", true, true, 40.f, 1000, true), clock);
    Expect(v.step == BrdIronfelStep::Nothing && !v.clock.started,
           "the member is fighting: the pickup is not begun mid-fight");

    v = BrdIronfelNext(Facts("Oz", true, true, 40.f, 1000), clock);
    Expect(v.step == BrdIronfelStep::Walk && v.clock.started && v.clock.wanter == "Oz",
           "Fineous dead, the corpse offers Ironfel: the party walks to it");
    clock = v.clock;

    v = BrdIronfelNext(Facts("Oz", true, true, 30.f, 1100, true), clock);
    Expect(v.step == BrdIronfelStep::Walk && v.clock.best == 30.f,
           "a walk under way is carried on through a fight, and nearer is progress");
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, true, -1.f, 1200), clock);
    Expect(v.step == BrdIronfelStep::Walk && v.clock.best == 30.f,
           "an unread poll carries the walk as it is");

    v = BrdIronfelNext(Facts("Oz", true, true, 6.f, 1300), clock);
    Expect(v.step == BrdIronfelStep::Walk, "6 yards is the member's own last stretch, not yet a loot");
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, true, 3.f, 1310), clock);
    Expect(v.step == BrdIronfelStep::Loot && v.clock.lootTries == 1,
           "at the corpse the member loots it");
    clock = v.clock;

    v = BrdIronfelNext(Facts("", true, true, 3.f, 1320), clock);
    Expect(v.step == BrdIronfelStep::Done && v.clock.over,
           "the member holds Ironfel now: done, and the clear (the Shrine step) goes on");
    clock = v.clock;
    v = BrdIronfelNext(Facts("Zug", true, true, 40.f, 1400), clock);
    Expect(v.step == BrdIronfelStep::Nothing, "the pickup is asked once per run");
}

void TheBounds()
{
    BrdIronfelClock clock;
    BrdIronfelVerdict v = BrdIronfelNext(Facts("Oz", true, true, 450.f, 0), clock);
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, true, 445.f, BRD_IRONFEL_WALK_SECONDS - 1), clock);
    Expect(v.step == BrdIronfelStep::Walk, "five yards nearer is not progress, but inside the bound");
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, true, 445.f, BRD_IRONFEL_WALK_SECONDS), clock);
    Expect(v.step == BrdIronfelStep::GiveUp && v.clock.over,
           "a walk no 10 yards nearer for the bound gives up");

    clock = BrdIronfelClock{};
    for (unsigned attempt = 1; attempt <= BRD_IRONFEL_LOOT_TRIES; ++attempt)
    {
        v = BrdIronfelNext(Facts("Oz", true, true, 2.f, 100 + attempt), clock);
        Expect(v.step == BrdIronfelStep::Loot && v.clock.lootTries == attempt,
               "each opening of the corpse is a loot step");
        clock = v.clock;
    }
    v = BrdIronfelNext(Facts("Oz", true, true, 2.f, 200), clock);
    Expect(v.step == BrdIronfelStep::GiveUp && v.clock.over,
           "three openings with Ironfel still wanted give up");
    v = BrdIronfelNext(Facts("Oz", true, true, 2.f, 300), v.clock);
    Expect(v.step == BrdIronfelStep::Nothing, "and the run is not asked again");

    clock = BrdIronfelClock{};
    v = BrdIronfelNext(Facts("Oz", true, true, 40.f, 0), clock);
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, false, 40.f, 30), clock);
    Expect(v.step == BrdIronfelStep::GiveUp && v.clock.over,
           "a corpse that stops offering Ironfel while it is wanted gives up");

    clock = BrdIronfelClock{};
    v = BrdIronfelNext(Facts("Oz", true, true, 40.f, 0), clock);
    clock = v.clock;
    v = BrdIronfelNext(Facts("Oz", true, true, 40.f, 20), clock);
    Expect(v.step == BrdIronfelStep::Walk, "a walk is carried on while the corpse offers it");
    clock = v.clock;
    v = BrdIronfelNext(Facts("Uzza", true, true, 40.f, 40), clock);
    Expect(v.step == BrdIronfelStep::Walk && v.clock.wanter == "Uzza" && v.clock.lootTries == 0,
           "a new wanter restarts the clock and the attempts");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(src.find("#include \"overseer_brd_ironfel.h\"") != std::string::npos,
           "the adapter includes the rules");
    std::size_t const step = src.find("bool StepBrdIronfel(DungeonRunCoordinatorState& coord,");
    Expect(step != std::string::npos, "the Ironfel step exists");
    Expect(src.find("D::BrdIronfelWanter(D::BRD_MAP_ID, party)", step) != std::string::npos &&
               src.find("D::BrdIronfelNext(facts, before)", step) != std::string::npos,
           "it asks the wanter and the walk from the rules");
    Expect(src.find("p->GetQuestStatus(D::BRD_DARK_IRON_LEGACY)", step) != std::string::npos &&
               src.find("p->GetItemCount(D::BRD_IRONFEL)", step) != std::string::npos,
           "it reads 3802 and Ironfel off each member");
    Expect(src.find("GetCreatureListWithEntryInGrid(found, D::BRD_FINEOUS_ENTRY", step) !=
               std::string::npos,
           "Fineous Darkvire is looked for across the instance");
    Expect(src.find("fineous->loot.quest_items", step) != std::string::npos &&
               src.find("item.AllowedForPlayer(wanter, fineous->GetGUID())", step) !=
                   std::string::npos,
           "the corpse offers Ironfel when its quest slot is unlooted and the member may take it");
    Expect(src.find("HuntLootCorpse(wanter, corpse)", step) != std::string::npos,
           "the member opens the corpse with the hunt's corpse looter");
    Expect(src.find("EscortToward(member->GetName(), aim.str(), \"FINEOUS DARKVIRE'S CORPSE\"",
                    step) != std::string::npos,
           "the party is escorted to the corpse");
    std::size_t const end = src.find("    void DriveDungeonRunFor(", step);
    Expect(src.find("AddQuestAndCheckCompletion", step) > end && src.find("RewardQuest", step) > end &&
               src.find("StoreNewItem", step) > end,
           "the module turns nothing in and grants nothing: the item is the core's drop");

    std::size_t const hook = src.find("StepBrdIronfel(coord, members, leader, clearingNow)");
    std::size_t const held =
        src.find("HoldClearingClock(coord, OverseerDecisions::ClearingClock::HeldIronfelPickup,", hook);
    std::size_t const shrine = src.find("StepBrdShrine(coord, members, leader, clearingNow)", hook);
    Expect(hook != std::string::npos && held != std::string::npos && shrine != std::string::npos &&
               held < shrine,
           "CLEARING on map 230 asks Ironfel before the Shrine and holds the clearing clock");
    Expect(std::string(ClearingClockHoldReason(ClearingClock::HeldIronfelPickup)).find("Ironfel") !=
               std::string::npos,
           "the held clock says why");
}

}  // namespace

int main()
{
    TheWanter();
    TheReach();
    TheWalk();
    TheBounds();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Ironfel pickup check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_ironfel: ok\n");
    return 0;
}
