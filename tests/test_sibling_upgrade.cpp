/*
 * The transfer gate for mod-overseer#14, kept separate from the world adapter.
 * The test supplies the receiver's role-aware GearVerdict and slot incumbent,
 * so it can prove ownership safety without AzerothCore or a live database.
 * This is deliberately only the decision seam: no test here claims that a
 * trade transaction, proximity, or inventory mutation exists yet.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::GearIncumbent;
using OverseerDecisions::GearItem;
using OverseerDecisions::GearRole;
using OverseerDecisions::GearScore;
using OverseerDecisions::GearStat;
using OverseerDecisions::GearVerdict;
using OverseerDecisions::GearWearer;
using OverseerDecisions::SiblingUpgradeRequest;
using OverseerDecisions::SiblingUpgradeDecision;

namespace
{

int failures = 0;

void Expect(char const* what, bool got, bool expected)
{
    if (got != expected)
    {
        ++failures;
        std::printf("FAIL %s: expected %s, got %s\n", what,
                    expected ? "true" : "false", got ? "true" : "false");
    }
}

GearWearer Tank()
{
    GearWearer who;
    who.role = GearRole::Tank;
    who.level = 27;
    who.mail = true;
    who.shield = true;
    who.weaponProficient = true;
    return who;
}

GearItem MailChest(char const* name, int armour)
{
    GearItem item;
    item.name = name;
    item.itemClass = 4;
    item.subClass = 3;
    item.inventoryType = 5;
    item.itemLevel = 25;
    item.requiredLevel = 20;
    item.armour = armour;
    return item;
}

GearVerdict Upgrade()
{
    return GearScore(MailChest("a real upgrade", 300), Tank());
}

void ARealSiblingUpgradeIsAccepted()
{
    SiblingUpgradeRequest request;
    request.candidate = Upgrade();
    request.incumbent = 100.f;
    Expect("a genuine role-appropriate upgrade can be handed over",
           SiblingUpgradeDecision(request), true);
}

void ProtectedItemsAreNeverHandedOver()
{
    SiblingUpgradeRequest request;
    request.candidate = Upgrade();
    request.incumbent = 100.f;

    request.questItem = true;
    Expect("quest items stay with the looter", SiblingUpgradeDecision(request), false);
    request.questItem = false;
    request.equipped = true;
    Expect("equipped items stay with the wearer", SiblingUpgradeDecision(request), false);
    request.equipped = false;
    request.soulbound = true;
    Expect("soulbound items stay with the owner", SiblingUpgradeDecision(request), false);
}

void SeveringAxeDoesNotDisarmAShieldTank()
{
    GearWearer const tank = Tank();
    GearItem axe;
    axe.name = "Severing Axe";
    axe.itemClass = 2;
    axe.inventoryType = 17;
    axe.itemLevel = 25;
    axe.requiredLevel = 20;
    axe.dps = 30.f;
    axe.stats = {GearStat{4, 6}, GearStat{7, 4}};

    GearItem mainHand;
    mainHand.name = "Ironpatch Blade";
    mainHand.itemClass = 2;
    mainHand.inventoryType = 13;
    mainHand.itemLevel = 20;
    mainHand.requiredLevel = 15;
    mainHand.dps = 14.f;

    GearItem shield;
    shield.name = "Dervish Buckler";
    shield.itemClass = 4;
    shield.subClass = 6;
    shield.inventoryType = 14;
    shield.itemLevel = 28;
    shield.requiredLevel = 23;
    shield.armour = 545;

    SiblingUpgradeRequest request;
    request.candidate = GearScore(axe, tank);
    request.incumbent = GearIncumbent(GearScore(mainHand, tank).score,
                                      GearScore(shield, tank).score, true);
    Expect("the axe is not a shield tank upgrade", SiblingUpgradeDecision(request), false);
}

}  // namespace

int main()
{
    ARealSiblingUpgradeIsAccepted();
    ProtectedItemsAreNeverHandedOver();
    SeveringAxeDoesNotDisarmAShieldTank();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the sibling upgrade decisions hold\n");
    return EXIT_SUCCESS;
}
