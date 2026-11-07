/*
 * A Blackrock Depths family run walks the Ironfel carrier to the Shrine of
 * Thaurissan (mod-overseer#1391).
 *
 * The Shadowforge Key's only source is Dark Iron Legacy (3802): Ironfel laid at
 * the Shrine of Thaurissan inside the dungeon. The bridge writes the 3802
 * turn-in once the carrier stands within 20 yards of the Shrine, and nothing
 * walked the carrier there, so no member could earn the key. The rules in
 * src/overseer_brd_shrine.h pick the carrier, judge the walk and end it; the
 * adapter's wiring is pinned in the source because it needs a worldserver.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_brd_shrine.h"

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

BrdShrineHolder Member(char const* name, bool legacy, unsigned ironfel, bool alive = true)
{
    BrdShrineHolder h;
    h.name = name;
    h.aliveInside = alive;
    h.holdsLegacy = legacy;
    h.ironfel = ironfel;
    return h;
}

void TheCarrier()
{
    std::vector<BrdShrineHolder> party = {Member("Zug", false, 0), Member("Oz", true, 1),
                                          Member("Uzza", false, 0)};
    Expect(BrdShrineCarrier(BRD_MAP_ID, party) == "Oz",
           "a blackrock-depths run with a member holding 3802 and Ironfel gets the Shrine");
    Expect(BrdShrineCarrier(36, party).empty(), "no other dungeon gets it");

    std::vector<BrdShrineHolder> none = {Member("Zug", false, 0), Member("Oz", false, 1)};
    Expect(BrdShrineCarrier(BRD_MAP_ID, none).empty(),
           "no 3802 holder: Ironfel alone is no objective");
    std::vector<BrdShrineHolder> noItem = {Member("Zug", true, 0), Member("Oz", false, 0)};
    Expect(BrdShrineCarrier(BRD_MAP_ID, noItem).empty(),
           "a 3802 holder without Ironfel gets no objective");
    std::vector<BrdShrineHolder> dead = {Member("Oz", true, 1, false)};
    Expect(BrdShrineCarrier(BRD_MAP_ID, dead).empty(), "a dead holder is walked nowhere");
    std::vector<BrdShrineHolder> two = {Member("Zug", true, 1), Member("Oz", true, 1)};
    Expect(BrdShrineCarrier(BRD_MAP_ID, two) == "Zug", "the first carrier in the party's order");
}

void TheReach()
{
    Expect(BRD_SHRINE_REACH_YARDS == 20.f, "the reach is the turn-in's 20 yards");
    Expect(BrdShrineReached(0.f) && BrdShrineReached(19.9f) && BrdShrineReached(20.f),
           "within 20 yards of the Shrine is reached");
    Expect(!BrdShrineReached(20.1f) && !BrdShrineReached(400.f), "beyond 20 yards is not");
    Expect(!BrdShrineReached(-1.f), "an unread distance never counts");
}

BrdShrineFacts Facts(char const* carrier, bool fineousDead, float yards, std::time_t now)
{
    BrdShrineFacts f;
    f.carrier = carrier;
    f.fineousDead = fineousDead;
    f.yards = yards;
    f.now = now;
    return f;
}

void TheWalk()
{
    BrdShrineClock clock;
    BrdShrineVerdict v = BrdShrineNext(Facts("", true, -1.f, 1000), clock);
    Expect(v.step == BrdShrineStep::Nothing && !v.clock.started, "no carrier: nothing");

    v = BrdShrineNext(Facts("Oz", false, 450.f, 1000), clock);
    Expect(v.step == BrdShrineStep::Nothing && !v.clock.started,
           "Fineous Darkvire alive: the run does not walk yet");

    v = BrdShrineNext(Facts("Oz", true, 450.f, 1000), clock);
    Expect(v.step == BrdShrineStep::Walk && v.clock.started && v.clock.carrier == "Oz",
           "Fineous dead and a carrier: the party walks to the Shrine");
    clock = v.clock;

    v = BrdShrineNext(Facts("Oz", true, 300.f, 1200), clock);
    Expect(v.step == BrdShrineStep::Walk && v.clock.best == 300.f, "nearer is progress");
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, -1.f, 1300), clock);
    Expect(v.step == BrdShrineStep::Walk && v.clock.best == 300.f,
           "an unread poll carries the walk as it is");

    v = BrdShrineNext(Facts("Oz", true, 25.f, 1400), clock);
    Expect(v.step == BrdShrineStep::Walk, "25 yards is not at the Shrine yet");
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, 12.f, 1420), clock);
    Expect(v.step == BrdShrineStep::Stand && v.clock.standSince == 1420,
           "within 20 yards the carrier stands for the turn-in");
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, 12.f, 1480), clock);
    Expect(v.step == BrdShrineStep::Stand, "and keeps standing for the bridge's poll");
    clock = v.clock;

    v = BrdShrineNext(Facts("", true, -1.f, 1500), clock);
    Expect(v.step == BrdShrineStep::Done && v.clock.over,
           "the turn-in took 3802 and Ironfel: done, and the clear resumes");
    clock = v.clock;
    v = BrdShrineNext(Facts("Zug", true, 450.f, 1600), clock);
    Expect(v.step == BrdShrineStep::Nothing, "the Shrine is asked once per run");
}

void TheBounds()
{
    BrdShrineClock clock;
    BrdShrineVerdict v = BrdShrineNext(Facts("Oz", true, 450.f, 0), clock);
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, 445.f, BRD_SHRINE_WALK_SECONDS - 1), clock);
    Expect(v.step == BrdShrineStep::Walk, "five yards nearer is not progress, but inside the bound");
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, 445.f, BRD_SHRINE_WALK_SECONDS), clock);
    Expect(v.step == BrdShrineStep::GiveUp && v.clock.over,
           "a walk no 10 yards nearer for the bound gives up");

    clock = BrdShrineClock{};
    v = BrdShrineNext(Facts("Oz", true, 5.f, 0), clock);
    Expect(v.step == BrdShrineStep::Stand, "a carrier already at the Shrine stands");
    clock = v.clock;
    v = BrdShrineNext(Facts("Oz", true, 5.f, BRD_SHRINE_STAND_SECONDS), clock);
    Expect(v.step == BrdShrineStep::GiveUp && v.clock.over,
           "standing past the bound with 3802 still held gives up");
    Expect(BRD_SHRINE_STAND_SECONDS >= 60, "the stand outlasts a bridge poll");
}

void TheAdapterWiresIt()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Expect(src.find("#include \"overseer_brd_shrine.h\"") != std::string::npos,
           "the adapter includes the rules");
    std::size_t const step = src.find("bool StepBrdShrine(DungeonRunCoordinatorState& coord,");
    Expect(step != std::string::npos, "the Shrine step exists");
    Expect(src.find("D::BrdShrineCarrier(D::BRD_MAP_ID, party)", step) != std::string::npos &&
               src.find("D::BrdShrineNext(facts, before)", step) != std::string::npos,
           "it asks the carrier and the walk from the rules");
    Expect(src.find("p->GetQuestStatus(D::BRD_DARK_IRON_LEGACY)", step) != std::string::npos &&
               src.find("p->GetItemCount(D::BRD_IRONFEL)", step) != std::string::npos,
           "it reads 3802 and Ironfel off each member");
    Expect(src.find("GetCreatureListWithEntryInGrid(found, D::BRD_FINEOUS_ENTRY", step) !=
               std::string::npos,
           "Fineous Darkvire is looked for across the instance");
    Expect(src.find("EscortToward(member->GetName(), aim.str(), \"SHRINE OF THAURISSAN\"", step) !=
               std::string::npos,
           "the party is escorted to the Shrine");
    std::size_t const end = src.find("    void DriveDungeonRunFor(", step);
    Expect(src.find("AddQuestAndCheckCompletion", step) > end &&
               src.find("RewardQuest", step) > end,
           "the module turns nothing in: the bridge writes the turn-in");

    std::size_t const hook = src.find("StepBrdShrine(coord, members, leader, clearingNow)");
    std::size_t const held =
        src.find("HoldClearingClock(coord, OverseerDecisions::ClearingClock::HeldShrineWalk,", hook);
    std::size_t const moved = src.find("// HAS THE RUN ACTUALLY GONE ANYWHERE (#171)?", hook);
    Expect(hook != std::string::npos && held != std::string::npos && moved != std::string::npos &&
               held < moved,
           "CLEARING on map 230 asks the Shrine and holds the clearing clock while it walks");
    Expect(std::string(ClearingClockHoldReason(ClearingClock::HeldShrineWalk)).find("Shrine") !=
               std::string::npos,
           "the held clock says why");
}

}  // namespace

int main()
{
    TheCarrier();
    TheReach();
    TheWalk();
    TheBounds();
    TheAdapterWiresIt();
    if (failures)
    {
        std::fprintf(stderr, "%d Shrine of Thaurissan check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_brd_shrine: ok\n");
    return 0;
}
