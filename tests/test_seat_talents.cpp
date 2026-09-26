/*
 * overseer_raid_spec: the talent tree a guild raider's seat needs, and the
 * points it spends there as it levels.
 *
 * THE CASE IS THE DEV REALM'S. The two natural guilds' bots are reset to level
 * 1 and level again by themselves. The site plans each guild's raid as eight
 * groups of one tank, one healer and three damage dealers, and writes each
 * raider's target tree. Without this rule the playerbots level-up action picks
 * a tree at random at level 10 and keeps it, so a warrior planned as a tank
 * could come out Fury. The family is not touched: TrainRoster spends its points
 * in the roster's tree.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::PlanTalentSpend;
using OverseerDecisions::RaidSpecTarget;
using OverseerDecisions::RaidSpecTargetFor;
using OverseerDecisions::SeatTalentVerdict;
using OverseerDecisions::SeatTalentVerdictFor;
using OverseerDecisions::TalentLearnStep;
using OverseerDecisions::TalentSlot;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

unsigned const WARRIOR = 1;
unsigned const PRIEST = 5;
unsigned const DEATH_KNIGHT = 6;

RaidSpecTarget Target(char const* name, unsigned classId, unsigned tab)
{
    RaidSpecTarget t;
    t.name = name;
    t.classId = classId;
    t.tab = tab;
    return t;
}

void TestSpend()
{
    RaidSpecTarget const tank = Target("Tankbot", WARRIOR, 2);

    SeatTalentVerdict v = SeatTalentVerdictFor(false, &tank, WARRIOR, 1);
    Check("a guild raider with a free point spends it in its seat's tree", v.spend && v.tab == 2);
    Check("...and says so", std::strlen(v.said) > 0);

    v = SeatTalentVerdictFor(false, &tank, WARRIOR, 0);
    Check("no free point, nothing to spend", !v.spend);

    v = SeatTalentVerdictFor(true, &tank, WARRIOR, 5);
    Check("a family character is TrainRoster's, never this rule's", !v.spend);

    v = SeatTalentVerdictFor(false, nullptr, WARRIOR, 5);
    Check("no seat target leaves the bot to playerbots", !v.spend);

    v = SeatTalentVerdictFor(false, &tank, PRIEST, 5);
    Check("a target planned for another class is stale and not followed", !v.spend);

    RaidSpecTarget const bad = Target("Oddbot", WARRIOR, 3);
    v = SeatTalentVerdictFor(false, &bad, WARRIOR, 5);
    Check("a tab outside 0-2 is refused", !v.spend);

    RaidSpecTarget const dk = Target("Deathbot", DEATH_KNIGHT, 0);
    v = SeatTalentVerdictFor(false, &dk, DEATH_KNIGHT, 1);
    Check("a death knight's Ebon Hold quest points go to its seat's tree too", v.spend && v.tab == 0);

    for (SeatTalentVerdict const& each :
         {SeatTalentVerdictFor(false, &tank, WARRIOR, 1), SeatTalentVerdictFor(true, &tank, WARRIOR, 1),
          SeatTalentVerdictFor(false, nullptr, WARRIOR, 1), SeatTalentVerdictFor(false, &tank, PRIEST, 1),
          SeatTalentVerdictFor(false, &bad, WARRIOR, 1), SeatTalentVerdictFor(false, &tank, WARRIOR, 0)})
        Check("every verdict says why, with no quote character",
              std::strlen(each.said) > 0 && !std::strchr(each.said, '\''));
}

void TestLookup()
{
    std::vector<RaidSpecTarget> const book = {Target("Tankbot", WARRIOR, 2), Target("Healbot", PRIEST, 1)};
    RaidSpecTarget const* found = RaidSpecTargetFor(book, "tankbot");
    Check("a name is found whatever its case, as the core's names compare", found && found->tab == 2);
    Check("a name not in the book has no target", RaidSpecTargetFor(book, "Nobody") == nullptr);
    Check("an empty name has no target", RaidSpecTargetFor(book, "") == nullptr);
    Check("an empty book has no target", RaidSpecTargetFor({}, "Tankbot") == nullptr);
}

// A tree shaped like a warrior's Protection: two talents on row 0 (2 and 5
// ranks), two on row 1 (3 and 5), and one on row 2 that needs row 1's first
// talent at full rank.
std::vector<TalentSlot> Tree()
{
    std::vector<TalentSlot> t(5);
    t[0].talentId = 10; t[0].row = 0; t[0].col = 0; t[0].ranks = 2;
    t[1].talentId = 11; t[1].row = 0; t[1].col = 1; t[1].ranks = 5;
    t[2].talentId = 20; t[2].row = 1; t[2].col = 0; t[2].ranks = 3;
    t[3].talentId = 21; t[3].row = 1; t[3].col = 1; t[3].ranks = 5;
    t[4].talentId = 30; t[4].row = 2; t[4].col = 1; t[4].ranks = 1;
    t[4].dependsOn = 20; t[4].dependsOnRank = 2;
    return t;
}

void Apply(std::vector<TalentSlot>& tree, std::vector<TalentLearnStep> const& steps)
{
    for (TalentLearnStep const& s : steps)
        for (TalentSlot& slot : tree)
            if (slot.talentId == s.talentId && slot.known == s.rankIndex)
                ++slot.known;
}

unsigned Spent(std::vector<TalentSlot> const& tree)
{
    unsigned n = 0;
    for (TalentSlot const& s : tree)
        n += s.known;
    return n;
}

void TestPlan()
{
    std::vector<TalentSlot> tree = Tree();
    std::vector<TalentLearnStep> steps = PlanTalentSpend(tree, 1);
    Check("the first point goes to row 0's first talent, rank 1",
          steps.size() == 1 && steps[0].talentId == 10 && steps[0].rankIndex == 0);

    // ONE POINT A LEVEL, as a bot levelling from 10 gets them. The old walk
    // only ever bought rank 1 with a single point, so it stalled once row 0's
    // first ranks were taken and left the points to playerbots.
    tree = Tree();
    for (int level = 10; level <= 25; ++level)
        Apply(tree, PlanTalentSpend(tree, 1));
    Check("one point a level from 10 to 25 spends all 16 the tree holds", Spent(tree) == 16);

    tree = Tree();
    tree[0].known = 2;
    tree[1].known = 2;
    steps = PlanTalentSpend(tree, 3);
    Check("row 0 takes points up to 5, the tier requirement", steps.size() == 3 && steps[0].talentId == 11);
    Apply(tree, steps);
    steps = PlanTalentSpend(tree, 1);
    Check("with 5 in row 0 the next point opens row 1", steps.size() == 1 && steps[0].talentId == 20);

    tree = Tree();
    tree[0].known = 2;
    tree[1].known = 5;
    tree[2].known = 2;
    tree[3].known = 5;
    steps = PlanTalentSpend(tree, 1);
    Check("row 2's talent waits for its prerequisite at full rank", steps.size() == 1 && steps[0].talentId == 20);
    Apply(tree, steps);
    steps = PlanTalentSpend(tree, 1);
    Check("...and follows it", steps.size() == 1 && steps[0].talentId == 30);

    // Row 0 holds only 2 ranks here, so row 1 never opens: the point is left
    // rather than spent against the tier rule, which LearnTalent would refuse.
    std::vector<TalentSlot> shallow(2);
    shallow[0].talentId = 1; shallow[0].row = 0; shallow[0].ranks = 2; shallow[0].known = 2;
    shallow[1].talentId = 2; shallow[1].row = 1; shallow[1].ranks = 3;
    Check("a tier stays shut until the tree holds row x 5 points", PlanTalentSpend(shallow, 1).empty());

    tree = Tree();
    for (TalentSlot& s : tree)
        s.known = s.ranks;
    Check("a full tree plans nothing", PlanTalentSpend(tree, 3).empty());
    Check("no free points plans nothing", PlanTalentSpend(Tree(), 0).empty());

    tree = Tree();
    steps = PlanTalentSpend(tree, 16);
    bool rising = true;
    std::vector<TalentSlot> check = Tree();
    for (TalentLearnStep const& s : steps)
    {
        for (TalentSlot& slot : check)
            if (slot.talentId == s.talentId)
            {
                if (slot.known != s.rankIndex)
                    rising = false;
                ++slot.known;
            }
    }
    Check("each step is the next rank of its talent, so LearnTalent pays exactly one point", rising);
    Check("sixteen points are all planned", steps.size() == 16);
}

}  // namespace

int main()
{
    TestSpend();
    TestLookup();
    TestPlan();
    if (failures)
    {
        std::printf("test_seat_talents: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_seat_talents: all passed\n");
    return 0;
}
