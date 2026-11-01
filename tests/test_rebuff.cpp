/*
 * The party buff is kept, not only allowed (mod-overseer#823).
 *
 * KeepBuffStrategies puts `buff` on a priest once a minute and nothing more;
 * a family then went more than twenty minutes of a town trip with Fortitude
 * on none of its five members. DriveRebuff reads the party back and, when a
 * member the caster can reach lacks the buff and the caster can pay for it
 * above a reserve, asks the caster's own party buff action to run.
 *
 * What is pinned here is the decision (DecideRebuff), the class table, and -
 * by reading src/mod_overseer.cpp from the repo root - that the drive is on a
 * clock and asks the AI's action rather than casting a spell itself.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::DecideRebuff;
using OverseerDecisions::RebuffDecision;
using OverseerDecisions::RebuffFacts;
using OverseerDecisions::RebuffMember;
using OverseerDecisions::RebuffSpell;
using OverseerDecisions::RebuffSpellFor;
using OverseerDecisions::RebuffStep;
using OverseerDecisions::RebuffStepWord;

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

RebuffMember Member(bool hasBuff, float yards = 10.0f)
{
    RebuffMember m;
    m.hasBuff = hasBuff;
    m.yards = yards;
    return m;
}

// A priest out of combat, on foot, full of mana, with a party of five where
// nobody carries Fortitude and everybody is close: the town trip of #823.
RebuffFacts TownTrip()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.members = {Member(false, 0.0f), Member(false), Member(false), Member(false),
                     Member(false)};
    return facts;
}

void TheTownTripIsACast()
{
    RebuffDecision const d = DecideRebuff(TownTrip());
    Check("five members without the buff, all close, full mana: Cast", d.step == RebuffStep::Cast);
    Check("counts all five missing", d.missing == 5);
    Check("and all five in reach", d.inReach == 5);
}

void NothingMissingIsNotACast()
{
    RebuffFacts facts = TownTrip();
    for (RebuffMember& m : facts.members)
        m.hasBuff = true;
    RebuffDecision const d = DecideRebuff(facts);
    Check("everybody buffed: NothingMissing", d.step == RebuffStep::NothingMissing);
    Check("and counts none missing", d.missing == 0);
}

void TheDeadAndTheElsewhereAreNotMissing()
{
    RebuffFacts facts = TownTrip();
    for (RebuffMember& m : facts.members)
        m.hasBuff = true;
    facts.members[1].hasBuff = false;
    facts.members[1].alive = false;
    facts.members[2].hasBuff = false;
    facts.members[2].sameMap = false;
    RebuffDecision const d = DecideRebuff(facts);
    Check("a dead member and one on another map are not missing it", d.missing == 0);
    Check("so there is nothing to do", d.step == RebuffStep::NothingMissing);
}

void OutOfReachIsSaid()
{
    RebuffFacts facts = TownTrip();
    facts.members = {Member(true, 0.0f), Member(false, 40.0f), Member(false, 80.0f)};
    RebuffDecision d = DecideRebuff(facts);
    Check("missing members beyond reach: OutOfReach", d.step == RebuffStep::OutOfReach);
    Check("still counted missing", d.missing == 2);
    Check("none in reach", d.inReach == 0);

    facts.members = {Member(true, 0.0f), Member(false, 10.0f)};
    facts.members[1].inSight = false;
    d = DecideRebuff(facts);
    Check("close but behind a wall is out of reach", d.step == RebuffStep::OutOfReach);

    facts.members = {Member(true, 0.0f), Member(false, OverseerDecisions::REBUFF_REACH_YARDS)};
    d = DecideRebuff(facts);
    Check("exactly at the reach is in reach", d.step == RebuffStep::Cast);
}

void ManaIsCheckedAgainstTheReserve()
{
    RebuffFacts facts = TownTrip();
    facts.mana = 200;
    Check("less mana than the cost: LowMana", DecideRebuff(facts).step == RebuffStep::LowMana);

    // 30% of 2000 is 600; a 300 cost needs 900 to leave 600.
    facts.mana = 899;
    Check("one short of cost plus reserve: LowMana",
          DecideRebuff(facts).step == RebuffStep::LowMana);
    facts.mana = 900;
    Check("exactly cost plus reserve: Cast", DecideRebuff(facts).step == RebuffStep::Cast);

    RebuffDecision const d = DecideRebuff(TownTrip());
    Check("a low-mana refusal still counts the missing", d.missing == 5);
}

void TheCastersOwnStateComesFirst()
{
    RebuffFacts facts = TownTrip();
    facts.knowsSpell = false;
    Check("no rank learned: NoSpell", DecideRebuff(facts).step == RebuffStep::NoSpell);

    facts = TownTrip();
    facts.alive = false;
    Check("a dead caster: Dead", DecideRebuff(facts).step == RebuffStep::Dead);

    facts = TownTrip();
    facts.inCombat = true;
    Check("in combat: the engine's priorities stand", DecideRebuff(facts).step == RebuffStep::InCombat);

    facts = TownTrip();
    facts.casting = true;
    Check("already casting: Casting", DecideRebuff(facts).step == RebuffStep::Casting);

    facts = TownTrip();
    facts.mounted = true;
    Check("mounted: a rebuff would dismount it", DecideRebuff(facts).step == RebuffStep::Mounted);

    facts = TownTrip();
    facts.inCombat = true;
    facts.mana = 0;
    Check("combat is said before mana", DecideRebuff(facts).step == RebuffStep::InCombat);
}

void TheClassTable()
{
    RebuffSpell const priest = RebuffSpellFor(5);
    Check("a priest keeps Fortitude",
          priest.aura && std::strcmp(priest.aura, "power word: fortitude") == 0);
    Check("Prayer of Fortitude counts as the buff",
          priest.groupAura && std::strcmp(priest.groupAura, "prayer of fortitude") == 0);
    Check("by its own party action",
          priest.action && std::strcmp(priest.action, "power word: fortitude on party") == 0);

    RebuffSpell const mage = RebuffSpellFor(8);
    Check("a mage keeps Arcane Intellect",
          mage.action && std::strcmp(mage.action, "arcane intellect on party") == 0);
    RebuffSpell const druid = RebuffSpellFor(11);
    Check("a druid keeps Mark of the Wild",
          druid.action && std::strcmp(druid.action, "mark of the wild on party") == 0);

    for (unsigned cls : {1u, 2u, 3u, 4u, 6u, 7u, 9u})
        Check("every other class keeps none here", RebuffSpellFor(cls).action == nullptr);
}

void EveryStepHasAWord()
{
    for (RebuffStep step : {RebuffStep::NoSpell, RebuffStep::Dead, RebuffStep::InCombat,
                            RebuffStep::Casting, RebuffStep::Mounted, RebuffStep::NothingMissing,
                            RebuffStep::OutOfReach, RebuffStep::LowMana, RebuffStep::Cast})
        Check("each step says itself", std::strcmp(RebuffStepWord(step), "unknown") != 0);
}

std::string Slurp(char const* path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The wiring: the drive runs on a clock, and it asks the AI's own action
// rather than casting the spell itself.
void TheDriveIsWired()
{
    std::string const src = Slurp("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable from the repo root", !src.empty());
    std::size_t const def = src.find("void DriveRebuff()");
    Check("DriveRebuff is defined", def != std::string::npos);
    Check("and called", src.find("DriveRebuff();") != std::string::npos);
    if (def == std::string::npos)
        return;
    std::size_t const end = src.find("\n    // THIS REALM SAYS WHAT IT IS AND WHAT IT IS RUNNING", def);
    std::string const body = src.substr(def, end == std::string::npos ? std::string::npos : end - def);
    Check("it decides with DecideRebuff", body.find("OverseerDecisions::DecideRebuff(") != std::string::npos);
    Check("it asks the AI's own action", body.find("DoSpecificAction(spell.action") != std::string::npos);
    Check("raid casts are queued as named cast rows",
          body.find("'cast', '', '{}'") != std::string::npos &&
          body.find(" on:\" +") != std::string::npos &&
          body.find("decision.crossPartyTarget->name") != std::string::npos);
    Check("it never adds an aura itself", body.find("AddAura(") == std::string::npos);
}

// RAID GROUPING: only the caster's own subgroup for the party step (#1159).
void CasterOwnSubgroupOnlyWhenInRaid()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    facts.casterSubgroup = 0;
    
    // One group: 2 subgroups with 5 each
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = false;
        facts.members.push_back(m);
    }
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 0.0f;
        m.subgroup = 1;
        m.hasBuffer = false;
        facts.members.push_back(m);
    }
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("in a raid, only caster's subgroup is counted as missing",
          d.missing == 5 && d.inReach == 5);
}

void CrossPartyTargetChosenWhenNoBufferInSubgroup()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup has no living caster able to provide this buff.
    for (int i = 0; i < 4; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 15.0f;
        m.subgroup = 1;
        m.hasBuffer = false;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target chosen only if subgroup has no buffer",
          d.crossPartyTarget.has_value() && d.crossPartyTarget->subgroup == 1);
}

void OutsideRaidKeepsWholePartyCount()
{
    RebuffFacts facts = TownTrip();
    RebuffMember other = Member(false, 12.0f);
    other.subgroup = 1;
    facts.members.push_back(other);
    RebuffDecision const d = DecideRebuff(facts);
    Check("outside a raid all party members still count", d.missing == 6 && d.inReach == 6);
    Check("outside a raid there is no cross-party target", !d.crossPartyTarget.has_value());
}

void CrossPartyTargetRejectedIfBufferInSubgroup()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: all have buffer
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 15.0f;
        m.subgroup = 1;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("no cross-party target when subgroup has buffer",
          !d.crossPartyTarget.has_value());
}

void CrossPartyTargetRejectedIfOutofReach()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: one member at exactly REBUFF_REACH_YARDS
    RebuffMember m;
    m.hasBuff = false;
    m.yards = OverseerDecisions::REBUFF_REACH_YARDS + 0.1f;
    m.subgroup = 1;
    m.hasBuffer = false;
    m.alive = true;
    m.sameMap = true;
    m.inSight = true;
    facts.members.push_back(m);
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target rejected if out of reach",
          !d.crossPartyTarget.has_value());
}

void CrossPartyTargetRejectedIfOutOfSight()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: one member out of sight
    RebuffMember m;
    m.hasBuff = false;
    m.yards = 10.0f;
    m.subgroup = 1;
    m.hasBuffer = false;
    m.alive = true;
    m.sameMap = true;
    m.inSight = false;
    facts.members.push_back(m);
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target rejected if out of sight",
          !d.crossPartyTarget.has_value());
}

void CrossPartyTargetRejectedIfDead()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: one dead member
    RebuffMember m;
    m.hasBuff = false;
    m.yards = 10.0f;
    m.subgroup = 1;
    m.hasBuffer = false;
    m.alive = false;
    m.sameMap = true;
    facts.members.push_back(m);
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target rejected if dead",
          !d.crossPartyTarget.has_value());
}

void CrossPartyTargetRejectedIfNotSameMap()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: one on another map
    RebuffMember m;
    m.hasBuff = false;
    m.yards = 10.0f;
    m.subgroup = 1;
    m.hasBuffer = false;
    m.alive = true;
    m.sameMap = false;
    facts.members.push_back(m);
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target rejected if not same map",
          !d.crossPartyTarget.has_value());
}

void CrossPartyTargetIsNearest()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: two members at different distances
    for (int i = 0; i < 2; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 10.0f + i * 5.0f;
        m.subgroup = 1;
        m.hasBuffer = false;
        m.alive = true;
        m.sameMap = true;
        m.inSight = true;
        facts.members.push_back(m);
    }
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("cross-party target is the nearest one",
          d.crossPartyTarget.has_value() && d.crossPartyTarget->yards == 10.0f);
}

void CrossPartyTargetAtMostOnePerPoll()
{
    RebuffFacts facts;
    facts.knowsSpell = true;
    facts.mana = 2000;
    facts.maxMana = 2000;
    facts.cost = 300;
    facts.inRaid = true;
    
    // Caster's subgroup: all have buff
    for (int i = 0; i < 5; ++i)
    {
        RebuffMember m;
        m.hasBuff = true;
        m.yards = 0.0f;
        m.subgroup = 0;
        m.hasBuffer = true;
        m.alive = true;
        m.sameMap = true;
        facts.members.push_back(m);
    }
    
    // Other subgroup: two members
    for (int i = 0; i < 2; ++i)
    {
        RebuffMember m;
        m.hasBuff = false;
        m.yards = 15.0f;
        m.subgroup = 1;
        m.hasBuffer = false;
        m.alive = true;
        m.sameMap = true;
        m.inSight = true;
        facts.members.push_back(m);
    }
    
    RebuffDecision const d = DecideRebuff(facts);
    Check("at most one cross-party target per poll",
          d.crossPartyTarget.has_value());
}

}  // namespace

int main()
{
    TheTownTripIsACast();
    NothingMissingIsNotACast();
    TheDeadAndTheElsewhereAreNotMissing();
    OutOfReachIsSaid();
    ManaIsCheckedAgainstTheReserve();
    TheCastersOwnStateComesFirst();
    TheClassTable();
    EveryStepHasAWord();
    TheDriveIsWired();
    CasterOwnSubgroupOnlyWhenInRaid();
    CrossPartyTargetChosenWhenNoBufferInSubgroup();
    CrossPartyTargetRejectedIfBufferInSubgroup();
    CrossPartyTargetRejectedIfOutofReach();
    CrossPartyTargetRejectedIfOutOfSight();
    CrossPartyTargetRejectedIfDead();
    CrossPartyTargetRejectedIfNotSameMap();
    CrossPartyTargetIsNearest();
    CrossPartyTargetAtMostOnePerPoll();
    OutsideRaidKeepsWholePartyCount();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("rebuff: all checks passed\n");
    return 0;
}
