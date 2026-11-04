/*
 * A ritual summoner outside the family, and nobody at the stone who can click.
 *
 * PlanSummonRung lets the selected ritual summoner be a guild warlock who is
 * not in the family vector at all; a family member standing at the stone is
 * then the one who summons. When that warlock is named and the only member at
 * the stone is the leader, dead or in combat, the list of members who can
 * click is empty, and the plan took front() of it. That is undefined
 * behaviour on an empty vector: a pointer to nothing, read as a member a few
 * lines later. The two-clicker guard above it does not run when a ritual
 * summoner is named, and the existing outside-warlock test keeps two live
 * clickers at the stone, so this path had no test.
 *
 * The plan now waits for clickers, naming why. Against main this binary
 * reads through the dangling pointer; it is expected to crash there, and
 * its checks fail if it does not.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace OverseerDecisions;

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

SummonRungMember Member(char const* name, bool leader, bool atStone)
{
    SummonRungMember m;
    m.name = name;
    m.leader = leader;
    m.inWorld = true;
    m.alive = true;
    m.inCombat = false;
    m.atStone = atStone;
    m.summonable = true;
    m.tries = 0;
    return m;
}
}

int main()
{
    // The leader stands at the stone but is dead: nobody there can click.
    {
        SummonRungMember leader = Member("Leader", true, true);
        leader.alive = false;
        SummonRungMember straggler = Member("Straggler", false, false);
        SummonRungPlan const plan =
            PlanSummonRung({leader, straggler}, 2, "OutsideWarlock");
        Check("a dead leader alone at the stone waits for clickers",
              plan.step == SummonRungStep::WaitForClickers);
        Check("the reason names the outside summoner and the empty stone",
              plan.why.find("not in the family") != std::string::npos &&
                  plan.why.find("nobody at the stone can click") != std::string::npos);
        Check("no summoner is named", plan.summoner.empty());
        Check("no helper is named", plan.helper.empty());
    }

    // The same with the leader in combat.
    {
        SummonRungMember leader = Member("Leader", true, true);
        leader.inCombat = true;
        SummonRungMember straggler = Member("Straggler", false, false);
        SummonRungPlan const plan =
            PlanSummonRung({leader, straggler}, 2, "OutsideWarlock");
        Check("a leader in combat alone at the stone waits for clickers",
              plan.step == SummonRungStep::WaitForClickers);
    }

    // One live clicker at the stone and an outside summoner: the clicker
    // summons, and the plan still wants a second member to help - the
    // behaviour that was already there, kept.
    {
        SummonRungMember leader = Member("Leader", true, true);
        SummonRungMember straggler = Member("Straggler", false, false);
        SummonRungPlan const plan =
            PlanSummonRung({leader, straggler}, 2, "OutsideWarlock");
        Check("one live clicker with an outside summoner still waits for a helper",
              plan.step == SummonRungStep::WaitForClickers);
        Check("that reason is the helper one",
              plan.why.find("one family member at the stone to click") != std::string::npos);
    }

    // Two live clickers and an outside summoner: the summon goes ahead, as
    // tests/test_ritual_summoner.cpp already pins for a warlock in the vector.
    {
        SummonRungMember leader = Member("Leader", true, true);
        SummonRungMember helper = Member("Helper", false, true);
        SummonRungMember straggler = Member("Straggler", false, false);
        SummonRungPlan const plan =
            PlanSummonRung({leader, helper, straggler}, 2, "OutsideWarlock");
        Check("two live clickers summon", plan.step == SummonRungStep::Summon);
        Check("the leader at the stone summons", plan.summoner == "Leader");
        Check("the other clicker helps", plan.helper == "Helper");
        Check("the straggler is the target", plan.target == "Straggler");
    }

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("summon rung: an outside summoner with no clickers waits");
    return EXIT_SUCCESS;
}
