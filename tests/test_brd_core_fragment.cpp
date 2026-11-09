/*
 * Each attunement holder loots the Core Fragment chest (mod-overseer#1456).
 *
 * Attunement to the Core (7848 Alliance, 7487 Horde) ends in one Core Fragment
 * (18412) from the chest (gameobject 179553) beside the Molten Bridge. The
 * rules in src/overseer_brd_core_fragment.h pick the members, judge each walk
 * and end it; the adapter's wiring is pinned in the source because it needs a
 * worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_core_fragment.h"

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

BrdCoreFragmentMember Member(char const* name, bool quest, unsigned fragments, bool alive = true)
{
    BrdCoreFragmentMember m;
    m.name = name;
    m.aliveInside = alive;
    m.holdsQuest = quest;
    m.fragments = fragments;
    return m;
}

void TheWanters()
{
    Expect(BRD_CORE_FRAGMENT_CHEST == 179553 && BRD_CORE_FRAGMENT == 18412 &&
               BRD_ATTUNE_CORE_ALLIANCE == 7848 && BRD_ATTUNE_CORE_HORDE == 7487,
           "the chest, the item and both quests are the ones the ticket names");

    std::vector<BrdCoreFragmentMember> party = {Member("Zug", false, 0), Member("Oz", true, 0),
                                                Member("Uzza", true, 0)};
    std::vector<std::string> const wanters = BrdCoreFragmentWanters(BRD_MAP_ID, party);
    Expect(wanters.size() == 2 && wanters[0] == "Oz" && wanters[1] == "Uzza",
           "every member with the quest and no fragment wants it, in the party's order");
    Expect(BrdCoreFragmentWanters(36, party).empty(), "no other dungeon gets a detour");

    std::vector<BrdCoreFragmentMember> none = {Member("Zug", false, 0), Member("Oz", false, 0)};
    Expect(BrdCoreFragmentWanters(BRD_MAP_ID, none).empty(),
           "a run with no quest holder adds no detour");
    std::vector<BrdCoreFragmentMember> has = {Member("Oz", true, 1)};
    Expect(BrdCoreFragmentWanters(BRD_MAP_ID, has).empty(),
           "a member already holding the fragment is skipped");
    std::vector<BrdCoreFragmentMember> dead = {Member("Oz", true, 0, false)};
    Expect(BrdCoreFragmentWanters(BRD_MAP_ID, dead).empty(), "a dead member is walked nowhere");
}

void TheSkipReasonsAndLines()
{
    Expect(BrdCoreFragmentSkipFor(Member("A", false, 0)) == BrdCoreFragmentSkip::NoQuest,
           "no quest is skipped for the quest");
    Expect(BrdCoreFragmentSkipFor(Member("A", true, 2)) == BrdCoreFragmentSkip::HoldsFragment,
           "a holder of the fragment is skipped for it");
    Expect(BrdCoreFragmentSkipFor(Member("A", true, 0, false)) == BrdCoreFragmentSkip::NotInside,
           "a member not alive inside is skipped for that");
    Expect(BrdCoreFragmentSkipFor(Member("A", true, 0)) == BrdCoreFragmentSkip::None,
           "a quest holder with none wants it");

    Expect(BrdCoreFragmentLine("Oz", "") == "core fragment: Oz looted", "the looted line");
    Expect(BrdCoreFragmentLine("Zug", BrdCoreFragmentSkipWord(BrdCoreFragmentSkip::NoQuest)) ==
               "core fragment: Zug skipped (no attunement quest)",
           "the skipped line names the reason");
}

void TheReach()
{
    Expect(BrdCoreFragmentAtChest(0.f) && BrdCoreFragmentAtChest(BRD_CORE_FRAGMENT_LOOT_YARDS),
           "within the loot reach is at the chest");
    Expect(!BrdCoreFragmentAtChest(BRD_CORE_FRAGMENT_LOOT_YARDS + 0.1f), "beyond it is not");
    Expect(!BrdCoreFragmentAtChest(-1.f) && !BrdCoreFragmentClose(-1.f),
           "an unread distance never counts");
    Expect(BRD_CORE_FRAGMENT_LOOT_YARDS < 5.0f,
           "the reach is inside a chest's usual interaction distance");
    Expect(BrdCoreFragmentClose(BRD_CORE_FRAGMENT_CLOSE_YARDS) &&
               !BrdCoreFragmentClose(BRD_CORE_FRAGMENT_CLOSE_YARDS + 1.f),
           "the last stretch is the member's own inside 25 yards");
}

BrdCoreFragmentFacts Facts(std::vector<std::string> wanters, float yards, std::time_t now,
                           bool chest = true, bool fighting = false)
{
    BrdCoreFragmentFacts f;
    f.wanters = std::move(wanters);
    f.chestFound = chest;
    f.yards = yards;
    f.now = now;
    f.inCombat = fighting;
    return f;
}

void TheVisit()
{
    BrdCoreFragmentClock clock;
    BrdCoreFragmentVerdict v = BrdCoreFragmentNext(Facts({}, 40.f, 1000), clock);
    Expect(v.step == BrdCoreFragmentStep::Nothing && !v.clock.started,
           "nobody wants it: no detour");

    v = BrdCoreFragmentNext(Facts({"Oz"}, 40.f, 1000, false), clock);
    Expect(v.step == BrdCoreFragmentStep::Nothing && !v.clock.started,
           "the world holds no chest: the run never walks");

    v = BrdCoreFragmentNext(Facts({"Oz"}, 40.f, 1000, true, true), clock);
    Expect(v.step == BrdCoreFragmentStep::Nothing && !v.clock.started,
           "the member is fighting: the visit is not begun mid-fight");

    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 300.f, 1000), clock);
    Expect(v.step == BrdCoreFragmentStep::Walk && v.clock.started && v.clock.current == "Oz",
           "a wanter: the party walks to the chest, the first member's turn");
    clock = v.clock;

    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 200.f, 1100, true, true), clock);
    Expect(v.step == BrdCoreFragmentStep::Walk && v.clock.best == 200.f,
           "a walk under way is carried on through a fight, and nearer is progress");
    clock = v.clock;
    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, -1.f, 1200), clock);
    Expect(v.step == BrdCoreFragmentStep::Walk && v.clock.best == 200.f,
           "an unread poll carries the walk as it is");

    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 6.f, 1300), clock);
    Expect(v.step == BrdCoreFragmentStep::Walk, "6 yards is the last stretch, not yet a loot");
    clock = v.clock;
    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 3.f, 1310), clock);
    Expect(v.step == BrdCoreFragmentStep::Loot && v.clock.lootTries == 1,
           "at the chest the member opens it");
    clock = v.clock;

    // Oz now holds the fragment: his turn ends, Uzza's begins at the chest.
    v = BrdCoreFragmentNext(Facts({"Uzza"}, 3.f, 1320), clock);
    Expect(v.ended == "Oz" && v.step == BrdCoreFragmentStep::Loot && v.clock.current == "Uzza" &&
               v.clock.lootTries == 1,
           "the first member's turn ends and the next member opens the chest");
    clock = v.clock;

    v = BrdCoreFragmentNext(Facts({}, 3.f, 1330), clock);
    Expect(v.ended == "Uzza" && v.step == BrdCoreFragmentStep::Done && v.clock.over,
           "nobody is left: done, and the clear goes on");
    v = BrdCoreFragmentNext(Facts({"Zug"}, 40.f, 1400), v.clock);
    Expect(v.step == BrdCoreFragmentStep::Nothing, "the chest is visited once per run");
}

void TheBounds()
{
    BrdCoreFragmentClock clock;
    BrdCoreFragmentVerdict v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 450.f, 0), clock);
    clock = v.clock;
    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 445.f, BRD_CORE_FRAGMENT_WALK_SECONDS - 1), clock);
    Expect(v.step == BrdCoreFragmentStep::Walk,
           "five yards nearer is not progress, but inside the bound");
    clock = v.clock;
    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 445.f, BRD_CORE_FRAGMENT_WALK_SECONDS), clock);
    Expect(v.step == BrdCoreFragmentStep::Skip && v.gaveUp == "Oz" && !v.reason.empty() &&
               !v.clock.over,
           "a walk no 10 yards nearer for the bound gives that member up, not the run");
    clock = v.clock;
    v = BrdCoreFragmentNext(Facts({"Oz", "Uzza"}, 445.f, BRD_CORE_FRAGMENT_WALK_SECONDS + 5), clock);
    Expect(v.clock.current == "Uzza" && v.step == BrdCoreFragmentStep::Walk,
           "the given-up member is not asked again; the next member's turn begins");

    clock = BrdCoreFragmentClock{};
    for (unsigned attempt = 1; attempt <= BRD_CORE_FRAGMENT_LOOT_TRIES; ++attempt)
    {
        v = BrdCoreFragmentNext(Facts({"Oz"}, 2.f, 100 + attempt), clock);
        Expect(v.step == BrdCoreFragmentStep::Loot && v.clock.lootTries == attempt,
               "each opening of the chest is a loot step");
        clock = v.clock;
    }
    v = BrdCoreFragmentNext(Facts({"Oz"}, 2.f, 200), clock);
    Expect(v.step == BrdCoreFragmentStep::Skip && v.gaveUp == "Oz",
           "three openings with the fragment still missing give the member up");
    v = BrdCoreFragmentNext(Facts({"Oz"}, 2.f, 210), v.clock);
    Expect(v.step == BrdCoreFragmentStep::Done && v.clock.over,
           "with every member settled the visit is done");

    clock = BrdCoreFragmentClock{};
    v = BrdCoreFragmentNext(Facts({"Oz"}, 40.f, 0), clock);
    v = BrdCoreFragmentNext(Facts({"Oz"}, 40.f, 30, false), v.clock);
    Expect(v.step == BrdCoreFragmentStep::GiveUp && v.clock.over,
           "a chest that leaves the world mid-visit gives the visit up");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(src.find("#include \"overseer_brd_core_fragment.h\"") != std::string::npos,
           "the adapter includes the rules");
    std::size_t const step = src.find("bool StepBrdCoreFragment(DungeonRunCoordinatorState& coord,");
    Expect(step != std::string::npos, "the Core Fragment step exists");
    Expect(src.find("D::BrdCoreFragmentWanters(D::BRD_MAP_ID, party)", step) != std::string::npos &&
               src.find("D::BrdCoreFragmentNext(facts, before)", step) != std::string::npos,
           "it asks the wanters and the walk from the rules");
    Expect(src.find("p->GetQuestStatus(questId)", step) != std::string::npos &&
               src.find("{D::BRD_ATTUNE_CORE_ALLIANCE, D::BRD_ATTUNE_CORE_HORDE}", step) !=
                   std::string::npos &&
               src.find("p->GetItemCount(D::BRD_CORE_FRAGMENT)", step) != std::string::npos,
           "it reads both attunement quests and the fragment off each member");
    std::size_t const spawn = src.find("BrdShrineSpot const* BrdCoreFragmentSpawn()");
    Expect(spawn != std::string::npos &&
               src.find("sObjectMgr->GetAllGOData()", spawn) != std::string::npos &&
               src.find("data.id != OverseerDecisions::BRD_CORE_FRAGMENT_CHEST", spawn) !=
                   std::string::npos,
           "the chest position is the world database's spawn of the chest");
    Expect(src.find("NoteChestOpen(turn, chest->GetGUID())", step) != std::string::npos &&
               src.find("DriveChestOpen(session, opener, chest->GetGUID())", step) !=
                   std::string::npos &&
               src.find("DriveGameObjectUse(session, chest->GetGUID())", step) !=
                   std::string::npos,
           "the member opens the chest as a client does and the opener takes the loot");
    Expect(src.find("EscortToward(member->GetName(), aim.str(), \"THE CORE FRAGMENT CHEST\"",
                    step) != std::string::npos,
           "the party is escorted to the chest");
    Expect(src.find("D::BrdCoreFragmentLine(", step) != std::string::npos,
           "each member's line is the rules' line");
    std::size_t const end = src.find("    // BLACKROCK DEPTHS' UPPER CITY (overseer_brd_wing.h", step);
    Expect(end != std::string::npos && src.find("AddQuestAndCheckCompletion", step) > end &&
               src.find("RewardQuest", step) > end && src.find("StoreNewItem", step) > end,
           "the module turns nothing in and grants nothing: the item is the chest's loot");

    std::size_t const hook = src.find("StepBrdCoreFragment(coord, members, leader, clearingNow)");
    std::size_t const held = src.find(
        "HoldClearingClock(coord, OverseerDecisions::ClearingClock::HeldCoreFragmentChest,", hook);
    Expect(hook != std::string::npos && held != std::string::npos,
           "CLEARING on map 230 asks the chest step and holds the clearing clock");
    Expect(std::string(ClearingClockHoldReason(ClearingClock::HeldCoreFragmentChest))
                   .find("Core Fragment") != std::string::npos,
           "the held clock says why");
}

}  // namespace

int main()
{
    TheWanters();
    TheSkipReasonsAndLines();
    TheReach();
    TheVisit();
    TheBounds();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Core Fragment check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_core_fragment: ok\n");
    return 0;
}
