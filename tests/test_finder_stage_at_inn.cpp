/*
 * The family is hearthed to its inn before the dungeon finder queues it (#1510).
 *
 * MEASURED ON THE DEV REALM, 2026-11-12: the core puts every living member back
 * where it stood at the queue (Player::SetEntryPoint on the way in,
 * Player::TeleportToEntryPoint on the way out), and the finder road queued the
 * family from wherever it stood. The 07:08 queue had the head at Light's Hope
 * Chapel in Eastern Plaguelands and four members at or near the inn in the
 * Badlands; after the clear the four stood 8,000 to 10,000 yards from the head
 * and the bag-room errand took 86 minutes.
 *
 * Compiles against the pure decision file and nothing from AzerothCore, and
 * reads src/mod_overseer.cpp to pin the wiring (run from the repo root).
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

void Check(char const* what, bool got, bool want = true)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

FinderStageMember Member(char const* name, float yards, ExitHearthStep hearth = ExitHearthStep::Cast)
{
    FinderStageMember m;
    m.name = name;
    m.inHearthSet = true;
    m.yardsFromInn = yards;
    m.alive = true;
    m.carriesStone = true;
    m.hearth = hearth;
    return m;
}

FinderStageFacts Family()
{
    FinderStageFacts f;
    f.planReady = true;
    // The 07:08 measured queue: the head in Eastern Plaguelands (another
    // continent position, 9,000 yards out), four near the inn.
    f.family.push_back(Member("Head", 9001.f));
    f.family.push_back(Member("A", 40.f, ExitHearthStep::NotInside));
    f.family.push_back(Member("B", 120.f, ExitHearthStep::NotInside));
    f.family.push_back(Member("C", 300.f, ExitHearthStep::NotInside));
    f.family.push_back(Member("D", 800.f, ExitHearthStep::NotInside));
    return f;
}

void TheMeasuredSpreadHearthsFirst()
{
    FinderStageVerdict const v = FinderStageNext(Family());
    Check("the measured spread holds the queue and hearths", v.step == FinderStageStep::Hearth);
    Check("only the far head casts", v.cast.size() == 1 && v.cast[0] == "Head");
}

void ACastInFlightHoldsTheQueue()
{
    FinderStageFacts f = Family();
    f.family[0].hearth = ExitHearthStep::Waiting;
    FinderStageVerdict const v = FinderStageNext(f);
    Check("a cast in flight waits", v.step == FinderStageStep::Wait);
    Check("and asks for no second cast", v.cast.empty());
}

void ALandedFamilyQueuesNow()
{
    FinderStageFacts f = Family();
    f.family[0].yardsFromInn = 30.f;
    f.family[0].hearth = ExitHearthStep::NotInside;
    FinderStageVerdict const v = FinderStageNext(f);
    Check("a family within the spread queues", v.step == FinderStageStep::Queue);
    Check("and that is not a failure", !v.failed);
}

void AnotherMapIsFar()
{
    FinderStageFacts f = Family();
    f.family[0].yardsFromInn = -1.f;
    Check("a member on another map hearths",
          FinderStageNext(f).step == FinderStageStep::Hearth);
}

void TheBudgetBoundsTheStage()
{
    FinderStageFacts f = Family();
    f.waitedSeconds = FINDER_STAGE_BUDGET_SECONDS;
    FinderStageVerdict const v = FinderStageNext(f);
    Check("past the budget the queue goes ahead", v.step == FinderStageStep::Queue);
    Check("and it is marked failed", v.failed);
    Check("with no cast asked", v.cast.empty());
    f.waitedSeconds = FINDER_STAGE_BUDGET_SECONDS - 1;
    Check("one second before the budget it still hearths",
          FinderStageNext(f).step == FinderStageStep::Hearth);
}

void NeverTwiceInARowAfterAFailure()
{
    FinderStageFacts f = Family();
    f.failedLastTime = true;
    FinderStageVerdict const v = FinderStageNext(f);
    Check("a failed stage is skipped the next time", v.step == FinderStageStep::Queue);
    Check("and the skip clears the flag", !v.failed);
    f.failedLastTime = false;
    Check("then the stage is tried again",
          FinderStageNext(f).step == FinderStageStep::Hearth);
}

void StonesThatCannotBeUsedQueueNowAndSayWhy()
{
    FinderStageFacts f = Family();
    f.family[0].onCooldown = true;
    f.family[0].hearth = ExitHearthStep::Impossible;
    FinderStageVerdict v = FinderStageNext(f);
    Check("a stone on cooldown queues now", v.step == FinderStageStep::Queue);
    Check("without calling it a failure", !v.failed);
    Check("and says why in one line",
          v.skipped.size() == 1 && v.skipped[0] == "Head: hearthstone on cooldown");

    f = Family();
    f.family[0].inHearthSet = false;
    f.family[0].hearth = ExitHearthStep::NotInside;
    v = FinderStageNext(f);
    Check("a member bound at another inn queues now", v.step == FinderStageStep::Queue);
    Check("and says it is bound elsewhere",
          v.skipped.size() == 1 && v.skipped[0] == "Head: bound at another inn");

    f = Family();
    f.family[0].carriesStone = false;
    f.family[0].hearth = ExitHearthStep::Impossible;
    Check("no stone says so", FinderStageNext(f).skipped[0] == "Head: no hearthstone");

    f = Family();
    f.family[0].alive = false;
    f.family[0].hearth = ExitHearthStep::Impossible;
    Check("a dead member says so", FinderStageNext(f).skipped[0] == "Head: dead");
}

void ACastThatNeverLandedIsAFailure()
{
    FinderStageFacts f = Family();
    f.family[0].attempts = EXIT_HEARTH_ATTEMPTS;
    f.family[0].hearth = ExitHearthStep::Impossible;
    FinderStageVerdict const v = FinderStageNext(f);
    Check("spent casts queue now", v.step == FinderStageStep::Queue);
    Check("and the failure is carried to the next attempt", v.failed);
}

void NoPlanNoStageAndSwitchOff()
{
    FinderStageFacts f = Family();
    f.planReady = false;
    f.planWhyNot = "no inn has more than half the family bound at it";
    FinderStageVerdict v = FinderStageNext(f);
    Check("no plan queues now", v.step == FinderStageStep::Queue);
    Check("and quotes the plan's reason",
          v.why.find("no inn has more than half") != std::string::npos);
    f = Family();
    f.enabled = false;
    Check("the switch off queues now", FinderStageNext(f).step == FinderStageStep::Queue);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    Check("source opens", static_cast<bool>(in));
    std::stringstream ss;
    ss << in.rdbuf();
    std::string const source = ss.str();

    std::size_t const rung = source.find("    FinderRungOutcome DriveFinderRung(");
    std::size_t const end = source.find("            case OverseerDecisions::FinderStep::Accept:", rung);
    Check("the finder rung is found", rung != std::string::npos && end != std::string::npos);
    if (rung == std::string::npos || end == std::string::npos)
        return;
    std::string const join = source.substr(rung, end - rung);
    std::size_t const stage = join.find("FinderStageHoldsTheQueue(coord");
    std::size_t const joinLfg = join.find("sLFGMgr->JoinLfg(");
    Check("the stage is asked in the join step", stage != std::string::npos);
    Check("and before the family is queued",
          stage != std::string::npos && joinLfg != std::string::npos && stage < joinLfg);

    std::size_t const fnBegin = source.find("    bool FinderStageHoldsTheQueue(");
    std::size_t const fnEnd = source.find("    FinderRungOutcome DriveFinderRung(", fnBegin);
    Check("the stage function is found", fnBegin != std::string::npos && fnEnd != std::string::npos);
    if (fnBegin != std::string::npos && fnEnd != std::string::npos)
    {
        std::string const fn = source.substr(fnBegin, fnEnd - fnBegin);
        Check("the stage never teleports", fn.find("TeleportTo") == std::string::npos);
        Check("the stage casts through DoHearth", fn.find("DoHearth(") != std::string::npos);
        Check("the stage is decided by FinderStageNext",
              fn.find("OverseerDecisions::FinderStageNext(") != std::string::npos);
    }
    Check("the rung's ceiling carries the stage budget",
          source.find("FINDER_CEILING_SECONDS * 2 +\n                                      "
                      "OverseerDecisions::FINDER_STAGE_BUDGET_SECONDS") != std::string::npos);
    Check("the failure flag is carried across attempts",
          source.find("next.finderStageFailed = coord.finderStageFailed;") != std::string::npos);
    Check("the switch is read", source.find("Overseer.DungeonFinder.StageAtInn") != std::string::npos);
}

}  // namespace

int main()
{
    TheMeasuredSpreadHearthsFirst();
    ACastInFlightHoldsTheQueue();
    ALandedFamilyQueuesNow();
    AnotherMapIsFar();
    TheBudgetBoundsTheStage();
    NeverTwiceInARowAfterAFailure();
    StonesThatCannotBeUsedQueueNowAndSayWhy();
    ACastThatNeverLandedIsAFailure();
    NoPlanNoStageAndSwitchOff();
    TheAdapterIsWired();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    else
        std::printf("finder stage at inn: all checks passed\n");
    return failures ? 1 : 0;
}
