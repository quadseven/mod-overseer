/*
 * A family mage's way to a capital (#1161).
 *
 * Both families carry one mage, and the levels this roster has earned include
 * Teleport and Portal for its capital. A character sent there was walking the
 * whole distance even though one cast would do it. These are the decisions
 * under the drive: which spell moves whom, when a portal needs a party leader
 * at the front, when a missing reagent is a refusal rather than a silent walk,
 * and what the line says so a live check can read the same sentence.
 *
 * The ids below are the ones the dev realm's mages know (2026-10-26): Og the
 * Alliance mage has Teleport: Stormwind 3561 and Portal: Stormwind 10059; Oz
 * the Horde mage has Teleport: Orgrimmar 3567 and Portal: Orgrimmar 11417.
 * They are pinned as fixtures so a decision that starts reading a different
 * spell fails here rather than in the world.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::MageTravelFacts;
using OverseerDecisions::MageTravelLine;
using OverseerDecisions::MageTravelPlan;
using OverseerDecisions::MageTravelSpell;
using OverseerDecisions::MageTravelSpellKind;
using OverseerDecisions::MageTravelStep;
using OverseerDecisions::MageTravelStepWord;
using OverseerDecisions::PlanMageTravel;

namespace Refusal = OverseerDecisions::MageTravelRefusal;

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

void Check(char const* what, bool ok)
{
    Check(what, ok, true);
}

void CheckNum(char const* what, std::uint32_t got, std::uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u, wanted %u\n", what, unsigned(got), unsigned(want));
    ++failures;
}

void CheckStep(char const* what, MageTravelStep got, MageTravelStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, MageTravelStepWord(got),
                MageTravelStepWord(want));
    ++failures;
}

void CheckReason(char const* what, char const* got, char const* want)
{
    if (std::strcmp(got, want) == 0)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got, want);
    ++failures;
}

MageTravelSpell Teleport(std::uint32_t id)
{
    MageTravelSpell s;
    s.spellId = id;
    s.kind = MageTravelSpellKind::Teleport;
    s.knownToMage = true;
    return s;
}

MageTravelSpell Portal(std::uint32_t id, std::uint32_t carried = 2)
{
    MageTravelSpell s;
    s.spellId = id;
    s.kind = MageTravelSpellKind::Portal;
    s.knownToMage = true;
    s.reagentEntry = 17032;  // Rune of Portals
    s.reagentRequired = 1;
    s.reagentCarried = carried;
    return s;
}

MageTravelSpell Unknown(std::uint32_t id, MageTravelSpellKind kind)
{
    MageTravelSpell s;
    s.spellId = id;
    s.kind = kind;
    s.knownToMage = true;
    s.resolvesInSpellData = false;
    return s;
}

// A mage on map 1, being sent to map 0 (the capital), with a party.
MageTravelFacts ToStormwind()
{
    MageTravelFacts f;
    f.mage = "Og";
    f.mageReadable = true;
    f.mageInWorld = true;
    f.mageAlive = true;
    f.travellerIsMage = true;
    f.travellerLeadsGroup = true;
    f.travellerGrouped = true;
    f.travellerWithMage = true;
    f.destinationIsCapital = true;
    f.destinationMap = 0;
    f.mageMap = 1;
    f.walkYards = -1.f;
    f.partyWithMage = 4;
    f.spells = {Teleport(3561), Portal(10059)};
    return f;
}

void TheTeleportForTheMage()
{
    MageTravelFacts f = ToStormwind();
    MageTravelPlan const plan = PlanMageTravel(f);
    CheckStep("a mage sent to the other continent casts the teleport", plan.step,
              MageTravelStep::Teleport);
    CheckNum("the teleport is the spell's own id", plan.spellId, 3561);

    // The party does not change what moves the mage's own body.
    f.travellerLeadsGroup = false;
    CheckStep("a mage that does not lead still teleports itself", PlanMageTravel(f).step,
              MageTravelStep::Teleport);

    // Horde side, same shape and a different id.
    MageTravelFacts horde = f;
    horde.mage = "Oz";
    horde.spells = {Teleport(3567), Portal(11417)};
    MageTravelPlan const hordePlan = PlanMageTravel(horde);
    CheckStep("the Horde mage teleports to Orgrimmar", hordePlan.step,
              MageTravelStep::Teleport);
    CheckNum("the Horde teleport is its own id", hordePlan.spellId, 3567);

    // A long walk on one map is still worth the cast.
    MageTravelFacts sameMap = f;
    sameMap.destinationMap = 1;
    sameMap.walkYards = 2400.f;
    CheckStep("a 2400 yard walk on one map is a teleport", PlanMageTravel(sameMap).step,
              MageTravelStep::Teleport);

    MageTravelFacts near = sameMap;
    near.walkYards = 300.f;
    CheckStep("a 300 yard walk is walked", PlanMageTravel(near).step, MageTravelStep::Walk);

    MageTravelFacts edge = sameMap;
    edge.walkYards = OverseerDecisions::MAGE_TRAVEL_LONG_WALK_YARDS;
    CheckStep("exactly the long-walk threshold is a cast", PlanMageTravel(edge).step,
              MageTravelStep::Teleport);
    edge.walkYards = OverseerDecisions::MAGE_TRAVEL_LONG_WALK_YARDS - 1.f;
    CheckStep("one yard short of the threshold is walked", PlanMageTravel(edge).step,
              MageTravelStep::Walk);
}

void ThePortalForSomebodyElse()
{
    MageTravelFacts f = ToStormwind();
    f.travellerIsMage = false;
    f.travellerLeadsGroup = true;
    f.spells = {Portal(10059)};
    MageTravelPlan const plan = PlanMageTravel(f);
    CheckStep("a leader sent to the capital takes the mage's portal", plan.step,
              MageTravelStep::Portal);
    CheckNum("the portal is the spell's own id", plan.spellId, 10059);

    // The leader has to be standing with the mage to click it.
    f.travellerWithMage = false;
    CheckStep("a leader away from the mage walks", PlanMageTravel(f).step,
              MageTravelStep::Walk);

    // A follower may not cross before its leader, so the portal is not a way
    // for that follower to leave the party behind.
    f = ToStormwind();
    f.travellerIsMage = false;
    f.travellerLeadsGroup = false;
    f.travellerWithMage = true;
    f.spells = {Portal(10059)};
    CheckStep("a follower sent away does not take the party's portal", PlanMageTravel(f).step,
              MageTravelStep::Walk);

    // A teleport cannot move somebody other than the caster.
    f.travellerLeadsGroup = true;
    f.spells = {Teleport(3561)};
    CheckStep("a leader with only a teleport walks", PlanMageTravel(f).step,
              MageTravelStep::Walk);
}

void TheMageThatCannotClickFirst()
{
    MageTravelFacts f = ToStormwind();
    f.spells = {Portal(10059)};  // no teleport learned
    f.travellerLeadsGroup = false;
    f.partyWithMage = 3;
    CheckStep("a mage that does not lead does not cast a portal for a party that cannot "
              "cross it",
              PlanMageTravel(f).step, MageTravelStep::Walk);

    f.travellerLeadsGroup = true;
    CheckStep("a mage that leads may cast the portal and cross first", PlanMageTravel(f).step,
              MageTravelStep::Portal);

    MageTravelFacts groupedAlone = f;
    groupedAlone.travellerLeadsGroup = false;
    groupedAlone.travellerGrouped = true;
    groupedAlone.partyWithMage = 0;
    CheckStep("a grouped follower alone beside its portal still does not cross before its "
              "leader",
              PlanMageTravel(groupedAlone).step, MageTravelStep::Walk);

    MageTravelFacts alone = f;
    alone.travellerLeadsGroup = false;
    alone.travellerGrouped = false;
    alone.partyWithMage = 0;
    CheckStep("a mage in no group and with nobody to leave behind casts the portal and clicks "
              "it itself",
              PlanMageTravel(alone).step, MageTravelStep::Portal);
}

void TheReagent()
{
    MageTravelFacts f = ToStormwind();
    f.travellerIsMage = false;
    f.travellerLeadsGroup = true;
    f.spells = {Portal(10059, 1)};
    CheckStep("one rune is exactly enough", PlanMageTravel(f).step, MageTravelStep::Portal);

    f.spells = {Portal(10059, 0)};
    MageTravelPlan const plan = PlanMageTravel(f);
    CheckStep("no rune is a refusal, not a walk", plan.step, MageTravelStep::Refuse);
    CheckReason("the refusal names the missing reagent", plan.reason, Refusal::MissingReagent);
    CheckNum("the refusal names the portal", plan.spellId, 10059);

    MageTravelFacts alone = f;
    alone.travellerIsMage = true;
    alone.spells = {Portal(10059, 0)};
    MageTravelPlan const alonePlan = PlanMageTravel(alone);
    CheckStep("a mage with no teleport and no rune refuses", alonePlan.step,
              MageTravelStep::Refuse);
    CheckReason("the mage's refusal names the reagent too", alonePlan.reason,
                Refusal::MissingReagent);

    // A teleport is not a portal and does not take its reagent.
    alone.spells = {Teleport(3561), Portal(10059, 0)};
    CheckStep("a known teleport is used without a rune", PlanMageTravel(alone).step,
              MageTravelStep::Teleport);
}

void TheUnknownSpell()
{
    MageTravelFacts f = ToStormwind();
    f.spells = {Unknown(3561, MageTravelSpellKind::Teleport)};
    MageTravelPlan const plan = PlanMageTravel(f);
    CheckStep("a spell this world cannot explain is refused", plan.step,
              MageTravelStep::Refuse);
    CheckReason("the refusal names the spell data", plan.reason, Refusal::UnknownSpell);
    CheckNum("the refusal names the spellbook id", plan.spellId, 3561);

    // A usable spell beside an unreadable one still works.
    f.spells = {Unknown(3562, MageTravelSpellKind::Teleport), Teleport(3561)};
    CheckStep("an unreadable rank does not block a readable one", PlanMageTravel(f).step,
              MageTravelStep::Teleport);

    f.spells = {};
    CheckStep("no travel spell is a walk", PlanMageTravel(f).step, MageTravelStep::Walk);
}

void TheMageThatCannotCast()
{
    for (int which = 0; which < 5; ++which)
    {
        MageTravelFacts f = ToStormwind();
        char const* what = "";
        switch (which)
        {
            case 0: f.mageReadable = false; what = "unreadable"; break;
            case 1: f.mageInWorld = false; what = "not in the world"; break;
            case 2: f.mageAlive = false; what = "dead"; break;
            case 3: f.mageInCombat = true; what = "in combat"; break;
            default: f.mageCasting = true; what = "already casting"; break;
        }
        std::string const label = std::string("a mage that is ") + what + " does not cast";
        CheckStep(label.c_str(), PlanMageTravel(f).step, MageTravelStep::Walk);
    }
}

void NotACapital()
{
    MageTravelFacts f = ToStormwind();
    f.destinationIsCapital = false;
    CheckStep("an aim that is not a capital is walked", PlanMageTravel(f).step,
              MageTravelStep::Walk);

    MageTravelFacts far = ToStormwind();
    far.walkYards = 300.f;
    far.destinationMap = 1;
    CheckStep("a capital close by is walked", PlanMageTravel(far).step,
              MageTravelStep::Walk);
}

void TheLine()
{
    MageTravelFacts f = ToStormwind();
    MageTravelPlan const cast = PlanMageTravel(f);
    std::string const castLine = MageTravelLine(f, cast);
    Check("the cast line names the mage", castLine.find("'Og'") != std::string::npos);
    Check("the cast line names the spell", castLine.find("3561") != std::string::npos);
    Check("the cast line says teleport", castLine.find("teleport") != std::string::npos);
    Check("the cast line says where the walk was", castLine.find("another map") !=
                                                       std::string::npos);

    MageTravelFacts portal = ToStormwind();
    portal.traveller = "Ugga";
    portal.travellerIsMage = false;
    portal.spells = {Portal(10059)};
    MageTravelPlan const portalPlan = PlanMageTravel(portal);
    std::string const portalLine = MageTravelLine(portal, portalPlan);
    Check("the portal line names the portal", portalLine.find("10059") != std::string::npos);
    Check("the portal line names the traveller", portalLine.find("for 'Ugga'") !=
                                                     std::string::npos);
    Check("the portal line counts the party", portalLine.find("4 family members") !=
                                                  std::string::npos);

    portal.spells = {Portal(10059, 0)};
    MageTravelPlan const refused = PlanMageTravel(portal);
    std::string const refusedLine = MageTravelLine(portal, refused);
    Check("the refusal line names the mage", refusedLine.find("'Og'") != std::string::npos);
    Check("the refusal line names the spell", refusedLine.find("10059") != std::string::npos);
    Check("the refusal line says why", refusedLine.find(Refusal::MissingReagent) !=
                                           std::string::npos);

    f.walkYards = 300.f;
    f.destinationMap = 1;
    MageTravelPlan const walk = PlanMageTravel(f);
    std::string const walkLine = MageTravelLine(f, walk);
    Check("the walk line says why", walkLine.find("close enough to walk") !=
                                       std::string::npos);

    Check("the words a log and a test pin", std::strcmp(MageTravelStepWord(MageTravelStep::Walk),
                                                       "walk") == 0);
    Check("teleport word", std::strcmp(MageTravelStepWord(MageTravelStep::Teleport),
                                      "teleport") == 0);
    Check("portal word", std::strcmp(MageTravelStepWord(MageTravelStep::Portal), "portal") == 0);
    Check("refuse word", std::strcmp(MageTravelStepWord(MageTravelStep::Refuse), "refuse") == 0);
}

}  // namespace

int main()
{
    TheTeleportForTheMage();
    ThePortalForSomebodyElse();
    TheMageThatCannotClickFirst();
    TheReagent();
    TheUnknownSpell();
    TheMageThatCannotCast();
    NotACapital();
    TheLine();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a family mage teleports itself or portals the party to its capital, and "
                "says which and why\n");
    return 0;
}
