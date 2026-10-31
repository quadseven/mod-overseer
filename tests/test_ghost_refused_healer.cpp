/*
 * A ghost whose spirit healer is refused is not revived beside its killer
 * (#1275). A level 34 guild member with no errand died 313 times to level 45
 * Primitive Owlbeasts at the Shadra'Alor graveyard: released beside its corpse,
 * it reclaimed there, 26 yards from an owlbeast spawn. Every graveyard its zone
 * links was refused, two of them for high ground alone with no spawn near.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::GhostRecovery;
using OverseerDecisions::GraveyardStanding;
using OverseerDecisions::JudgeGraveyard;
using OverseerDecisions::PickSafestGraveyard;
using OverseerDecisions::PlaceDangerReason;
using OverseerDecisions::RefusedHealerFacts;
using OverseerDecisions::RefusedHealerFallback;

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

// The owlbeast spot: two deaths here, one killer at the corpse, just released.
RefusedHealerFacts Owlbeast()
{
    RefusedHealerFacts f;
    f.deathsHere = 2;
    f.level = 34;
    f.strongestNearCorpse = 45;
    f.liveThreatsNearCorpse = 1;
    f.holdSeconds = 1200;
    f.healerKnown = true;
    return f;
}

void TheGhostIsKeptOffTheCorpse()
{
    RefusedHealerFacts f = Owlbeast();
    Check("two deaths and a killer at the corpse wait, not reclaim",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
    f.deathsHere = 313;
    f.strongestNearCorpse = 33;
    Check("repeat deaths alone hold it, whatever the level",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
    f = Owlbeast();
    f.deathsHere = 1;
    Check("one death to something far above it holds it too",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
    f = Owlbeast();
    f.heldSeconds = 1199;
    Check("still waiting one second short of the hold",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
}

void AnEmptyCorpseIsReclaimed()
{
    RefusedHealerFacts f = Owlbeast();
    f.liveThreatsNearCorpse = 0;
    Check("the killer gone, the corpse run stands",
          RefusedHealerFallback(f) == GhostRecovery::CorpseRun);
    f = Owlbeast();
    f.deathsHere = 1;
    f.strongestNearCorpse = 34;
    Check("a first death with no gap is the corpse run it always was",
          RefusedHealerFallback(f) == GhostRecovery::CorpseRun);
}

void AHoldEndsAtAHealerNeverTheCorpse()
{
    RefusedHealerFacts f = Owlbeast();
    f.heldSeconds = 1200;
    Check("a killer that never moves ends at the nearest healer",
          RefusedHealerFallback(f) == GhostRecovery::SpiritHealer);
    f.healerWalkFailed = true;
    Check("a failed walk with a live threat retries the natural healer walk",
          RefusedHealerFallback(f) == GhostRecovery::SpiritHealer);
    f.healerKnown = false;
    Check("a failed walk is not retried without a healer on this map",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
    f = Owlbeast();
    f.heldSeconds = 1200;
    f.healerKnown = false;
    Check("no healer on the map leaves it waiting",
          RefusedHealerFallback(f) == GhostRecovery::Wait);
}

void TheSafestGraveyardIsPicked()
{
    Check("danger outranks high ground",
          JudgeGraveyard(true, PlaceDangerReason::Hostiles) == GraveyardStanding::Dangerous);
    Check("repeat deaths are danger",
          JudgeGraveyard(false, PlaceDangerReason::RepeatDeaths) == GraveyardStanding::Dangerous);
    Check("high ground with nothing near is high ground only",
          JudgeGraveyard(true, PlaceDangerReason::Safe) == GraveyardStanding::HighGroundOnly);
    Check("neither passes", JudgeGraveyard(false, PlaceDangerReason::Safe) == GraveyardStanding::Passes);

    using S = GraveyardStanding;
    Check("the owlbeast spot: past the dangerous nearest to bare high ground",
          PickSafestGraveyard({S::Dangerous, S::HighGroundOnly, S::Dangerous, S::HighGroundOnly}) == 1);
    Check("one that passes beats nearer high ground",
          PickSafestGraveyard({S::HighGroundOnly, S::Dangerous, S::Passes}) == 2);
    Check("all dangerous picks none",
          PickSafestGraveyard({S::Dangerous, S::Dangerous}) == 2);
    Check("nothing to pick from picks none", PickSafestGraveyard({}) == 0);
}

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void TheAdapterIsWired()
{
    std::string const source = Source();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("a caller without the ladder asks the fallback",
          source.find("OverseerDecisions::RefusedHealerFallback(facts, GHOST_RECOVERY_LIMITS)") !=
              std::string::npos);
    Check("the choice goes through it only where the ladder is absent",
          source.find("if (choice == OverseerDecisions::GhostRecovery::Ladder && !ladder)\n"
                      "            choice = RefusedHealerStep(") != std::string::npos);
    Check("an unsafe nearest healer is rerouted even with the ladder",
          source.find("if (st.healerGrave && !st.healerRefused.empty())") != std::string::npos);
    Check("a guild walk that ran out is no healer",
          source.find("(ladder || !st.healerWalkSpent)") != std::string::npos);
    Check("a spent walk does not hand the corpse run back",
          source.find("goes back to the dead engine") == std::string::npos);
    Check("the ghost's reroute and the ladder take the safest",
          source.find("refused, /*acceptHighGround*/ true);") != std::string::npos &&
              source.find("evidence, refused,\n                              /*acceptHighGround*/ true);") !=
                  std::string::npos);
    Check("a living member's safe ground stays strict",
          source.find("KillerEntryAt(name, mapId, x, y)),\n                                 refused);") !=
              std::string::npos);
    Check("no reachable graveyard is logged",
          source.find("has no reachable graveyard it can stand at") != std::string::npos);
    Check("a long healer walk hold is logged on a timer",
          source.find("now - st.refusedStatusSince >= 300") != std::string::npos &&
              source.find("has waited {}s with {} hostile(s) at its corpse") !=
                  std::string::npos);
}

}  // namespace

int main()
{
    TheGhostIsKeptOffTheCorpse();
    AnEmptyCorpseIsReclaimed();
    AHoldEndsAtAHealerNeverTheCorpse();
    TheSafestGraveyardIsPicked();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ghost refused healer: ok\n");
    return 0;
}
