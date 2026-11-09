/*
 * The loot council: the rules a family's group runs, who the heuristic gives a
 * drop to, how an open roll is voted once the council has decided, and the row
 * the site reads. Plus the seams the adapter needs, pinned in the source, since
 * LootRollAction and Group cannot be linked by this harness.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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

LootCandidate Candidate(std::string const& name, bool family, GearComparison comparison,
                        float gain, float score, int itemLevelGain = 5,
                        std::string const& role = "tank")
{
    LootCandidate c;
    c.name = name;
    c.family = family;
    c.wearable = true;
    c.comparison = comparison;
    c.gain = gain;
    c.score = score;
    c.itemLevelGain = itemLevelGain;
    c.role = role;
    return c;
}

void RulesForAPartyAndARaid()
{
    LootRules const party = LootRulesFor(false);
    Expect(party.method == LootMethodId::NeedBeforeGreed, "a party runs need before greed");
    Expect(party.threshold == LOOT_QUALITY_UNCOMMON, "a party rolls on uncommon and up");
    Expect(!party.leaderIsMasterLooter, "a party has no master looter");

    LootRules const raid = LootRulesFor(true);
    Expect(raid.method == LootMethodId::MasterLoot, "a raid runs master loot");
    Expect(raid.threshold == LOOT_QUALITY_UNCOMMON, "a raid's master loot starts at uncommon");
    Expect(raid.leaderIsMasterLooter, "the raid leader is the master looter");

    // A new group is group loot at uncommon (Group::Create): a party changes.
    Expect(LootRulesDiffer(party, LootMethodId::GroupLoot, LOOT_QUALITY_UNCOMMON, false),
           "a fresh party is moved to need before greed");
    Expect(!LootRulesDiffer(party, LootMethodId::NeedBeforeGreed, LOOT_QUALITY_UNCOMMON, false),
           "a party already on need before greed is left alone");
    Expect(LootRulesDiffer(raid, LootMethodId::MasterLoot, LOOT_QUALITY_UNCOMMON, false),
           "a raid whose master looter is not its leader is corrected");
    Expect(!LootRulesDiffer(raid, LootMethodId::MasterLoot, LOOT_QUALITY_UNCOMMON, true),
           "a raid already on master loot under its leader is left alone");
    Expect(LootRulesDiffer(raid, LootMethodId::MasterLoot, 4, true),
           "an epic threshold is brought back to uncommon");
}

void OnlyGearGoesToTheCouncil()
{
    Expect(LootCouncilJudges(2), "a weapon is judged");
    Expect(LootCouncilJudges(4), "armour is judged");
    Expect(!LootCouncilJudges(9), "a recipe keeps the upstream roll");
    Expect(!LootCouncilJudges(7), "trade goods keep the upstream roll");
}

void FamilyFirstThenTheBiggestUpgrade()
{
    // The raider gains far more, but the family comes first.
    std::vector<LootCandidate> c = {
        Candidate("Raider", false, GearComparison::Better, 80.f, 100.f, 20),
        Candidate("Og", true, GearComparison::Better, 10.f, 100.f, 3),
        Candidate("Grug", true, GearComparison::Better, 40.f, 100.f, 9),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(c);
    Expect(pick.recipient == "Grug", "the family member it upgrades most gets it");
    Expect(pick.ranked.size() == 3 && pick.ranked[1] == "Og" && pick.ranked[2] == "Raider",
           "the family is ranked before the guild's raiders");
    Expect(pick.why.find("40% of its worth is new") != std::string::npos,
           "the reason names the upgrade size");
    Expect(pick.why.find("the family as a tank") != std::string::npos,
           "the reason names the family and the role");
}

void TheUpgradeIsRelativeNotRaw()
{
    // A caster's scale is bigger than a tank's; the share of the worth that is
    // new is what compares across roles.
    std::vector<LootCandidate> c = {
        Candidate("Caster", true, GearComparison::Better, 30.f, 300.f, 4, "caster"),
        Candidate("Tank", true, GearComparison::Better, 20.f, 40.f, 4, "tank"),
    };
    Expect(LootCouncilHeuristic(c).recipient == "Tank",
           "half of a tank's piece being new beats a tenth of a caster's");

    LootCandidate empty = Candidate("Empty", true, GearComparison::Better, 50.f, 50.f);
    Expect(UpgradePercent(empty) == 100, "an empty slot is a whole new piece");
    LootCandidate none = Candidate("None", true, GearComparison::Better, 0.f, 50.f);
    Expect(UpgradePercent(none) == 0, "no gain is no upgrade");
}

void ACertainUpgradeBeatsAnUnsettledOne()
{
    std::vector<LootCandidate> c = {
        Candidate("Maybe", true, GearComparison::Undecided, 90.f, 100.f),
        Candidate("Sure", true, GearComparison::Better, 5.f, 100.f),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(c);
    Expect(pick.recipient == "Sure", "a certain upgrade outranks one the numbers cannot settle");
    Expect(pick.ranked.size() == 2, "the unsettled one is still ranked");
}

void NobodyItUpgrades()
{
    LootCandidate refused = Candidate("Ugga", true, GearComparison::Better, 50.f, 50.f);
    refused.wearable = false;
    std::vector<LootCandidate> c = {
        refused,
        Candidate("Bork", true, GearComparison::NotBetter, 0.f, 20.f),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(c);
    Expect(pick.recipient.empty(), "a drop that upgrades nobody names nobody");
    Expect(pick.ranked.empty(), "and ranks nobody");
    Expect(!pick.why.empty(), "and says why");
}

void TiesAreSettledByName()
{
    std::vector<LootCandidate> c = {
        Candidate("Zug", true, GearComparison::Better, 10.f, 100.f),
        Candidate("Bork", true, GearComparison::Better, 10.f, 100.f),
    };
    Expect(LootCouncilHeuristic(c).recipient == "Bork",
           "an exact tie never depends on the walk order");
}

GearItem Piece(int itemClass, int subClass, int armour, std::vector<GearStat> stats,
               float dps = 0.f)
{
    GearItem item;
    item.itemClass = itemClass;
    item.subClass = subClass;
    item.armour = armour;
    item.itemLevel = 50;
    item.stats = std::move(stats);
    item.dps = dps;
    return item;
}

void WhatMakesARolePiece()
{
    // ItemModType ids: 3 agility, 4 strength, 5 intellect, 6 spirit, 7 stamina,
    // 12 defense, 31 hit, 32 crit, 43 mana regeneration, 45 spell power.
    // Big Bad Pauldrons (9476), as the dev realm's item_template has it.
    GearItem const pauldrons = Piece(4, 4, 396, {{4, 12}, {7, 12}, {32, 8}});
    Expect(GearPieceForRole(pauldrons, GearRole::Tank),
           "plate that is mostly armour and stamina is a tank's piece");
    Expect(!GearPieceForRole(pauldrons, GearRole::Melee),
           "only a tank or a healer gets first call");

    GearItem const sword = Piece(2, 7, 0, {{4, 5}}, 30.f);
    Expect(!GearPieceForRole(sword, GearRole::Tank), "a weapon is damage, not a tank's piece");
    GearItem const ring = Piece(4, 0, 0, {{3, 8}, {31, 5}});
    Expect(!GearPieceForRole(ring, GearRole::Tank), "a ring of agility and hit is not");
    GearItem const defense = Piece(4, 0, 0, {{12, 10}, {3, 4}});
    Expect(GearPieceForRole(defense, GearRole::Tank), "a ring of defense is");

    // Necklace of Calisea (1714): intellect, spirit, stamina.
    GearItem const necklace = Piece(4, 0, 0, {{5, 7}, {6, 7}, {7, 8}});
    Expect(GearPieceForRole(necklace, GearRole::Healer),
           "intellect and spirit make a healer's piece");
    GearItem const casterHood = Piece(4, 1, 60, {{5, 10}, {45, 12}});
    Expect(!GearPieceForRole(casterHood, GearRole::Healer),
           "intellect and spell power alone are a caster's as much as a healer's");
    GearItem const mp5 = Piece(4, 1, 60, {{5, 6}, {43, 4}});
    Expect(GearPieceForRole(mp5, GearRole::Healer), "mana regeneration makes a healer's piece");
    Expect(!GearPieceForRole(Piece(4, 0, 0, {}), GearRole::Tank), "nothing on it is nothing");

    // The Blackrock Depths gloves (#1475): epic mail, 238 armour, spirit 20,
    // intellect 10, stamina 10. To a tank the spirit and intellect are worth
    // nothing, so they never dilute the share test - armour and stamina alone
    // made it a tank's piece, and the council gave a healer's gloves to the
    // plate-wearing tank. A piece whose every non-stamina stat is a caster
    // stat is not a tank's piece, however much armour it carries.
    GearItem const casterGloves = Piece(4, 3, 238, {{6, 20}, {5, 10}, {7, 10}});
    Expect(!GearPieceForRole(casterGloves, GearRole::Tank),
           "mail whose stats are spirit and intellect is not a tank's piece");
    Expect(GearPieceForRole(casterGloves, GearRole::Healer),
           "and is still a healer's piece");
    Expect(!GearPieceForRole(casterHood, GearRole::Tank),
           "a cloth hood of intellect and spell power is not a tank's piece either");
    Expect(!GearPieceForRole(necklace, GearRole::Tank),
           "a necklace of intellect and spirit is not a tank's piece");

    // The gate is about stat fit, not about spirit alone: a tank stat beside
    // the caster splash keeps the piece a tank's, and a piece of nothing but
    // stamina and armour is the plainest tank piece there is.
    GearItem const defenseSplash = Piece(4, 0, 0, {{12, 10}, {6, 5}});
    Expect(GearPieceForRole(defenseSplash, GearRole::Tank),
           "defense with a splash of spirit is still a tank's piece");
    GearItem const plainPlate = Piece(4, 4, 300, {{7, 15}});
    Expect(GearPieceForRole(plainPlate, GearRole::Tank),
           "armour and stamina alone are a tank's piece");
}

LootCandidate Seat(std::string const& name, std::string const& role, float gain, float score,
                   bool rolePiece, bool mainTank = false, bool family = true,
                   GearComparison comparison = GearComparison::Better)
{
    LootCandidate c = Candidate(name, family, comparison, gain, score, 8, role);
    c.tank = role == "tank";
    c.mainTank = mainTank;
    c.rolePiece = rolePiece;
    return c;
}

GearWearer Wearer(bool cloth, bool leather, bool mail, bool plate)
{
    GearWearer who;
    who.cloth = cloth;
    who.leather = leather;
    who.mail = mail;
    who.plate = plate;
    return who;
}

void ArmourClassAndStatFit()
{
    // The same mail gloves (#1475). Whose own armour mail is comes off the
    // character's skills: a shaman's heaviest is mail, a warrior's is the
    // plate over it, a druid's the leather under it.
    GearItem const gloves = Piece(4, 3, 238, {{6, 20}, {5, 10}, {7, 10}});
    Expect(GearOwnArmourClass(gloves, Wearer(true, true, true, false)),
           "mail is the shaman's own armour class");
    Expect(!GearOwnArmourClass(gloves, Wearer(true, true, true, true)),
           "mail sits under the warrior's plate");
    Expect(!GearOwnArmourClass(gloves, Wearer(true, true, false, false)),
           "mail sits over the druid's leather");
    // The shaman's wearer above is also a warrior who has not learned
    // plate: the skills are the same, so the answer is the same - the class
    // name is never consulted.

    // A shield, a ring and a sword are no class's own armour, and neither is
    // a cloak, which every class wears whatever it is filed under.
    GearWearer const warrior = Wearer(true, true, true, true);
    Expect(!GearOwnArmourClass(Piece(4, 6, 500, {{7, 10}}), warrior),
           "a shield is no class's own armour");
    Expect(!GearOwnArmourClass(Piece(4, 0, 0, {{7, 10}}), warrior),
           "a ring is no class's own armour");
    Expect(!GearOwnArmourClass(Piece(2, 7, 0, {{4, 5}}, 30.f), warrior),
           "a sword is no class's own armour");
    GearItem cloak = Piece(4, 1, 40, {{5, 8}});
    cloak.inventoryType = 16;  // INVTYPE_CLOAK
    Expect(!GearOwnArmourClass(cloak, Wearer(true, false, false, false)),
           "a cloak is no class's own armour");

    // The stats: spirit and intellect are what a healer and a caster build
    // on, and no physical role builds on either - which is why the family's
    // melee shaman is not these gloves' wearer either.
    Expect(GearStatsFitRole(gloves, GearRole::Healer), "spirit and intellect fit a healer");
    Expect(GearStatsFitRole(gloves, GearRole::Caster), "spirit and intellect fit a caster");
    Expect(!GearStatsFitRole(gloves, GearRole::Melee), "spirit and intellect fit no melee");
    Expect(!GearStatsFitRole(gloves, GearRole::Tank), "and fit no tank");
    Expect(!GearStatsFitRole(gloves, GearRole::Unknown), "an unread role is no fit either");

    // A role's consolation prices are not a fit: a tank prices crit at half
    // a point of armour per point, which settles ties and builds nothing.
    Expect(!GearStatsFitRole(Piece(4, 4, 300, {{32, 10}, {7, 10}}), GearRole::Tank),
           "crit alone does not fit a tank");
    Expect(GearStatsFitRole(Piece(4, 4, 300, {{4, 10}, {7, 10}}), GearRole::Tank),
           "strength fits a tank");
}

LootCandidate FitSeat(std::string const& name, std::string const& role, float gain, float score,
                      bool rolePiece, bool ownArmour, bool statsFit, bool mainTank = false,
                      bool family = true, GearComparison comparison = GearComparison::Better)
{
    LootCandidate c = Seat(name, role, gain, score, rolePiece, mainTank, family, comparison);
    c.ownArmour = ownArmour;
    c.statsFit = statsFit;
    return c;
}

void TheClassificationFeedsFirstCall()
{
    // Priority reads rolePiece and nothing else, so the classification fix
    // reaches it unchanged: on the mail gloves the main tank's first call
    // is gone and the healer's stands.
    GearItem const gloves = Piece(4, 3, 238, {{6, 20}, {5, 10}, {7, 10}});
    LootCandidate tank = Candidate("Zug", true, GearComparison::Better, 50.f, 100.f, 8, "tank");
    tank.tank = true;
    tank.mainTank = true;
    tank.rolePiece = GearPieceForRole(gloves, GearRole::Tank);
    Expect(LootCouncilPriority(tank) == 3, "a main tank has no first call on a caster's gloves");
    LootCandidate healer = Candidate("Zrog", true, GearComparison::Better, 30.f, 100.f, 8, "healer");
    healer.rolePiece = GearPieceForRole(gloves, GearRole::Healer);
    Expect(LootCouncilPriority(healer) == 2, "a healer's first call on them stands");
}

void TheHeuristicRespectsArmourFit()
{
    // The incident as the module saw it that night (#1475): the gloves were
    // classified a tank's piece, so the main tank held first call at 50% new,
    // ahead of the family's healing shaman at 30% - whose own armour mail is,
    // and whose role the stats fit. The tank is never named while she is
    // there to wear them, first call or not.
    std::vector<LootCandidate> c = {
        FitSeat("Zug", "tank", 50.f, 100.f, true, false, false, true),
        FitSeat("Zrog", "healer", 30.f, 100.f, true, true, true),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(c);
    Expect(pick.recipient == "Zrog",
           "the shaman whose own armour mail is gets the mail gloves, not the tank");
    Expect(pick.ranked.size() == 2 && pick.ranked[0] == "Zrog" && pick.ranked[1] == "Zug",
           "the fitted wearer heads the ranking");
    Expect(pick.why.find("armour class is not Zug's own") != std::string::npos,
           "the reason names the armour class and who was passed over");
    Expect(pick.why.find("fit Zrog as a healer") != std::string::npos,
           "and whose role the stats fit");

    // A fitted caster is promoted the same way: first call is no shelter.
    std::vector<LootCandidate> elemental = {
        FitSeat("Zug", "tank", 50.f, 100.f, true, false, false, true),
        FitSeat("Zrok", "caster", 30.f, 100.f, false, true, true),
    };
    Expect(LootCouncilHeuristic(elemental).recipient == "Zrok",
           "a caster whose own armour it is takes it over the tank's first call");

    // The fitted member need not be family: a guild raider the piece fits
    // is named over the family's tank it does not.
    std::vector<LootCandidate> raider = {
        FitSeat("Zug", "tank", 50.f, 100.f, true, false, false, true),
        FitSeat("Raidhealer", "healer", 30.f, 100.f, true, true, true, false, false),
    };
    Expect(LootCouncilHeuristic(raider).recipient == "Raidhealer",
           "a fitted raider is named over a family tank the piece does not fit");

    // With nobody in the group the piece fits - here the shaman's own
    // armour is mail but the stats fit no role she plays - the gate stays
    // silent and the council runs as it always has.
    c[1].statsFit = false;
    Expect(LootCouncilHeuristic(c).recipient == "Zug",
           "nobody fitting leaves the main tank's first call standing");

    // A fitted member the drop does not upgrade is not promoted into it,
    // and neither is one whose upgrade the numbers cannot settle, over a
    // certain one.
    std::vector<LootCandidate> notAnUpgrade = {
        FitSeat("Zug", "tank", 50.f, 100.f, true, false, false, true),
        FitSeat("Zrog", "healer", 30.f, 100.f, true, true, true, false, true,
                GearComparison::NotBetter),
    };
    Expect(LootCouncilHeuristic(notAnUpgrade).recipient == "Zug",
           "a fitted member it does not upgrade is not named either");
    std::vector<LootCandidate> unsettled = {
        FitSeat("Zug", "tank", 50.f, 100.f, true, false, false, true),
        FitSeat("Zrog", "healer", 30.f, 100.f, true, true, true, false, true,
                GearComparison::Undecided),
    };
    Expect(LootCouncilHeuristic(unsettled).recipient == "Zug",
           "an unsettled fitted member is not named over a certain one");

    // A piece that is the front's own armour class is untouched by the
    // gate, even when its stats fit him badly: that misfit is the
    // classification's and the bridge's business, not this gate's.
    std::vector<LootCandidate> plate = {
        FitSeat("Zug", "tank", 50.f, 100.f, false, true, false, true),
        FitSeat("Ugga", "healer", 30.f, 100.f, false, true, true),
    };
    LootCouncilPick const platePick = LootCouncilHeuristic(plate);
    Expect(platePick.recipient == "Zug",
           "the tank keeps a piece that is his own armour class");
    Expect(platePick.why.find("armour class is not") == std::string::npos,
           "and the reason does not claim otherwise");
}

void TheMainTankIsGearedFirst()
{
    // The dev realm, 2026-09-24: Big Bad Pauldrons from Zul'Farrak went to the
    // Retribution paladin (40% new) over the main tank (36% new).
    std::vector<LootCandidate> c = {
        Seat("Grug", "tank", 169.f, 467.f, true, true),
        Seat("Grog", "melee", 76.3f, 191.8f, false),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(c);
    Expect(pick.recipient == "Grug", "the main tank gets a tank's piece first");
    Expect(pick.why.find("the main tank in the family has first call on a tank's piece") !=
               std::string::npos,
           "the reason says it is the main tank's call");
    Expect(pick.why.find("ahead of Grog's 40%") != std::string::npos,
           "and whose bigger share it went ahead of");
    Expect(LootCouncilPriority(c[0]) == 0 && LootCouncilPriority(c[1]) == 3,
           "the main tank on a tank's piece has first call, a paladin in melee none");

    // The same drop, not a tank's piece: the biggest share wins as before.
    c[0].rolePiece = false;
    Expect(LootCouncilHeuristic(c).recipient == "Grog",
           "off a tank's piece the size of the upgrade decides");

    // First call is for a certain upgrade only.
    std::vector<LootCandidate> unsure = {
        Seat("Grug", "tank", -38.5f, 342.f, true, true, true, GearComparison::Undecided),
        Seat("Grog", "melee", -15.6f, 119.4f, false, false, true, GearComparison::Undecided),
    };
    Expect(LootCouncilPriority(unsure[0]) == 3, "an unsettled upgrade has no first call");
    Expect(LootCouncilHeuristic(unsure).recipient == "Grog",
           "an unsettled tank's piece is ranked by the numbers");
}

void TanksThenHealersThenTheRest()
{
    std::vector<LootCandidate> c = {
        Seat("Offtank", "tank", 20.f, 100.f, true),
        Seat("Grug", "tank", 10.f, 100.f, true, true),
    };
    Expect(LootCouncilHeuristic(c).recipient == "Grug", "the main tank before an off-tank");

    std::vector<LootCandidate> h = {
        Seat("Og", "caster", 60.f, 100.f, false),
        Seat("Ugga", "healer", 30.f, 100.f, true),
    };
    LootCouncilPick const pick = LootCouncilHeuristic(h);
    Expect(pick.recipient == "Ugga", "a healer gets a healer's piece first");
    Expect(pick.why.find("a healer in the family has first call on a healer's piece") !=
               std::string::npos,
           "and the reason says so");

    // The family still comes before a raider's first call.
    std::vector<LootCandidate> f = {
        Seat("Raidtank", "tank", 50.f, 100.f, true, false, false),
        Seat("Grog", "melee", 5.f, 100.f, false),
    };
    Expect(LootCouncilHeuristic(f).recipient == "Grog", "the family first, even over a tank");
}

void VotesOnAnOpenRoll()
{
    LootDisenchantFacts const none;
    Expect(LootCouncilVoteFor(false, "", "Og", 3, none) == LootCouncilVote::Hold,
           "an undecided roll holds the vote");
    Expect(LootCouncilVoteFor(false, "", "Og", LOOT_COUNCIL_ROLL_LAST_SECONDS, none) ==
               LootCouncilVote::Upstream,
           "an undecided roll near its deadline goes to the upstream vote");
    Expect(LootCouncilVoteFor(true, "Og", "Og", 5, none) == LootCouncilVote::Need,
           "the recipient needs");
    Expect(LootCouncilVoteFor(true, "Og", "Grug", 5, none) == LootCouncilVote::Pass,
           "everybody else passes");
    Expect(LootCouncilVoteFor(true, "", "Grug", 5, none) == LootCouncilVote::Greed,
           "nobody named: everybody greeds");
    Expect(LOOT_COUNCIL_ROLL_WAIT_SECONDS < LOOT_COUNCIL_ROLL_LAST_SECONDS &&
               LOOT_COUNCIL_ROLL_LAST_SECONDS < 60,
           "the heuristic decides before the upstream vote, and both before the roll ends");
}

// #160: a drop the council gives nobody is disenchanted by the family's
// Enchanter, and only when the realm allows it and an Enchanter is rolling.
void AnUnwantedDropGoesToTheEnchanter()
{
    LootDisenchantFacts open;
    open.realmAllows = true;
    open.enchanterRolling = true;

    LootDisenchantFacts enchanter = open;
    enchanter.voterCanDisenchant = true;
    Expect(LootCouncilVoteFor(true, "", "Og", 5, enchanter) == LootCouncilVote::Disenchant,
           "nobody named and an Enchanter can take it apart: the Enchanter disenchants");
    Expect(LootCouncilVoteFor(true, "", "Grug", 5, open) == LootCouncilVote::Pass,
           "and everybody else passes, so the materials reach the Enchanter");

    Expect(LootCouncilVoteFor(true, "Ugga", "Og", 5, enchanter) == LootCouncilVote::Pass,
           "a drop the council gave somebody is never disenchanted");
    Expect(LootCouncilVoteFor(true, "Og", "Og", 5, enchanter) == LootCouncilVote::Need,
           "the Enchanter needs a drop the council gave it");
    Expect(LootCouncilVoteFor(false, "", "Og", 3, enchanter) == LootCouncilVote::Hold,
           "an undecided roll still holds");

    LootDisenchantFacts realmOff = enchanter;
    realmOff.realmAllows = false;
    Expect(LootCouncilVoteFor(true, "", "Og", 5, realmOff) == LootCouncilVote::Greed,
           "with AiPlayerbot.LootRollDisenchant off, everybody greeds as before");

    LootDisenchantFacts noEnchanter = open;
    noEnchanter.enchanterRolling = false;
    Expect(LootCouncilVoteFor(true, "", "Grug", 5, noEnchanter) == LootCouncilVote::Greed,
           "with no Enchanter voting on the roll nobody passes it away");
}

void WhoCanDisenchantADrop()
{
    Expect(CanDisenchantDrop(41, 1, 1), "skill 1 takes apart a drop that asks for 1");
    Expect(CanDisenchantDrop(41, 25, 30), "more skill than asked is enough");
    Expect(!CanDisenchantDrop(41, 25, 24), "less skill than asked is not");
    Expect(!CanDisenchantDrop(0, 1, 300), "a drop with no disenchant loot cannot be taken apart");
    Expect(!CanDisenchantDrop(41, -1, 300), "a required skill of -1 means not disenchantable");
    Expect(!CanDisenchantDrop(41, 0, 0), "a character without Enchanting cannot");
}

void AMandateEndsOnlyInTheNamedHands()
{
    std::vector<std::string> const none;
    std::vector<std::string> const others = {"Grug", "Mog"};
    std::vector<std::string> const named = {"Grug", "Ugga"};
    Expect(MandateNextStep(false, "Ugga", none) == MandateStep::Repeat,
           "a dropless run repeats");
    Expect(MandateNextStep(false, "Ugga", others) == MandateStep::Repeat,
           "the item in other hands does not complete the mandate");
    Expect(MandateNextStep(false, "Ugga", named) == MandateStep::Done,
           "the named character holding the item completes it");
    Expect(MandateNextStep(false, "ugga", named) == MandateStep::Repeat,
           "the name is matched exactly");
    Expect(MandateNextStep(true, "Ugga", none) == MandateStep::Stopped,
           "a cancelled mandate stops repeating");
    Expect(MandateNextStep(true, "Ugga", others) == MandateStep::Stopped,
           "a cancelled mandate stays stopped over later cycles");
    Expect(MandateNextStep(false, "", others) == MandateStep::Stopped,
           "a mandate with no named character never repeats");
    Expect(MandateNextStep(true, "Ugga", named) == MandateStep::Stopped,
           "a cancelled mandate stops even when the named character holds the item");
    Expect(MandateNextStep(false, "Ugg", {"Ugga2"}) == MandateStep::Repeat,
           "a longer holder name that starts with the intended name does not match");
    Expect(MandateNextStep(false, "Ugga", {"Uggar"}) == MandateStep::Repeat,
           "a holder name that merely begins with the intended name does not match");
    Expect(MandateNextStep(false, "Ugga2", {"Ugga"}) == MandateStep::Repeat,
           "a holder name that is a prefix of the intended name does not match");
    Expect(MandateNextStep(false, "gga", {"Ugga"}) == MandateStep::Repeat,
           "a holder name that contains the intended name does not match");
    Expect(MandateNextStep(false, "Ugga", {"", "Grug"}) == MandateStep::Repeat,
           "an empty holder name is not a match");
    Expect(MandateNextStep(false, "Ugga", {"Ugga "}) == MandateStep::Repeat,
           "a holder name with trailing whitespace does not match");
    Expect(MandateNextStep(false, "Ugga", {" Ugga"}) == MandateStep::Repeat,
           "a holder name with leading whitespace does not match");
    Expect(MandateNextStep(false, "", none) == MandateStep::Stopped,
           "a nameless mandate stops even with nothing held");
}

void MandatedBindOnPickupRollsAreExclusive()
{
    Expect(MandatedLootVote(true, true, "Ugga", "Ugga") == LootCouncilVote::Need,
           "only the intended character needs a mandated bind-on-pickup item");
    Expect(MandatedLootVote(true, true, "Ugga", "Grug") == LootCouncilVote::Pass,
           "another character passes a mandated bind-on-pickup item");
    Expect(MandatedLootVote(true, true, "", "Grug") == LootCouncilVote::Pass,
           "an unresolved recipient cannot take a bind-on-pickup item");
    Expect(MandatedLootVote(false, true, "Ugga", "Ugga") == LootCouncilVote::Upstream,
           "a cancelled mandate no longer overrides the ordinary roll");
    Expect(MandatedLootVote(true, false, "Ugga", "Grug") == LootCouncilVote::Upstream,
           "a non-bind-on-pickup item keeps ordinary loot policy");
}

void SeatRoles()
{
    Expect(GearRoleForSeat(1, "tank") == GearRole::Tank, "a tank seat tanks");
    Expect(GearRoleForSeat(5, "healer") == GearRole::Healer, "a healer seat heals");
    Expect(GearRoleForSeat(4, "damage") == GearRole::Melee, "a rogue in a damage seat is melee");
    Expect(GearRoleForSeat(3, "damage") == GearRole::Ranged, "a hunter in a damage seat is ranged");
    Expect(GearRoleForSeat(8, "dps") == GearRole::Caster, "a mage in a dps seat is a caster");
    Expect(GearRoleForSeat(8, "") == GearRole::Unknown, "no seat is no opinion");
    // A duty read off the talent tree beats the class rule.
    Expect(GearRoleForSeat(5, "caster") == GearRole::Caster, "a Shadow priest's duty is caster");
    Expect(GearRoleForSeat(11, "melee") == GearRole::Melee, "a Feral druid's duty is melee");
    Expect(GearRoleForSeat(3, "ranged") == GearRole::Ranged, "a hunter's duty is ranged");
    Expect(GearRoleForSeat(2, "main tank") == GearRole::Tank &&
               GearRoleForSeat(2, "off tank") == GearRole::Tank,
           "the main tank and an off tank tank");
    Expect(GearRoleForSeat(5, "damage") == GearRole::Caster,
           "a tree the site could not read is the class rule");

    // Who the loot council gears first.
    Expect(LootMainTank(true, true, true, "", false), "no named main tank: the tanking head");
    Expect(!LootMainTank(true, false, true, "", false), "a head who does not tank is not");
    Expect(LootMainTank(false, true, false, "main tank", true),
           "a raid that names its main tank gears that raider first, family or not");
    Expect(!LootMainTank(true, true, true, "off tank", true),
           "and then the head in an off tank seat is an off tank");
    Expect(std::string(GearRoleName(GearRole::Tank)) == "tank", "role names are words");
    Expect(ClassWord(1) == "Warrior" && ClassWord(11) == "Druid", "class words");
    Expect(ClassWord(10).empty(), "no class 10");
    Expect(SpecTreeName(1, 2) == "Protection", "warrior tab 2 is protection");
    Expect(SpecTreeName(2, 2) == "Retribution", "paladin tab 2 is retribution");
    Expect(SpecTreeName(11, 1) == "Feral Combat", "druid tab 1 is feral");
    Expect(SpecTreeName(1, 255).empty(), "no tree chosen names no tree");
    Expect(ItemLootDetail(ItemVia::Council, "Lucifron") ==
               "awarded by the loot council from Lucifron",
           "a council award reads as one in the loot record");
}

void KeysAndJson()
{
    Expect(LootCouncilRollKey(4242) == "roll:4242", "a roll's key");
    Expect(LootCouncilMasterKey(99, 2) == "ml:99:2", "a master-loot drop's key");

    LootCandidate c = Candidate("Og", true, GearComparison::Better, 12.5f, 50.f, 7);
    c.className = "Warrior";
    c.spec = "Protection";
    c.tank = true;
    c.why = "plate, 400 \"armour\"";
    std::string const json = LootCandidatesJson({c});
    Expect(json.front() == '[' && json.back() == ']', "the candidates are an array");
    Expect(json.find("\"name\":\"Og\"") != std::string::npos, "the name is written");
    Expect(json.find("\"family\":true") != std::string::npos, "the family flag is written");
    Expect(json.find("\"comparison\":\"better\"") != std::string::npos, "the comparison is written");
    Expect(json.find("\"gain\":12.5") != std::string::npos, "the gain is written");
    Expect(json.find("\"upgrade_percent\":25") != std::string::npos, "the share is written");
    Expect(json.find("\"item_level_gain\":7") != std::string::npos, "item levels are written");
    Expect(json.find("\"spec\":\"Protection\"") != std::string::npos, "the spec is written");
    Expect(json.find("\"tank\":true") != std::string::npos, "the tank flag is written");
    Expect(json.find("\"main_tank\":false") != std::string::npos, "the main tank flag is written");
    Expect(json.find("\"role_piece\":false") != std::string::npos, "the role piece is written");
    Expect(json.find("\"priority\":3") != std::string::npos, "the first call is written");
    c.mainTank = true;
    c.rolePiece = true;
    std::string const first = LootCandidatesJson({c});
    Expect(first.find("\"main_tank\":true") != std::string::npos &&
               first.find("\"role_piece\":true") != std::string::npos &&
               first.find("\"priority\":0") != std::string::npos,
           "a main tank's first call is written");
    Expect(json.find("400 \\\"armour\\\"") != std::string::npos, "quotes are escaped");
    Expect(LootCandidatesJson({}) == "[]", "no candidates is an empty array");
}

void TheAdapterSeams()
{
    std::string const module = Read("src/mod_overseer.cpp");
    Expect(module.find("OverseerDecisions::LootRulesFor(") != std::string::npos,
           "the adapter sets each family group's loot rules");
    Expect(module.find("SetMasterLooterGuid(") != std::string::npos,
           "a raid gets a master looter");
    Expect(module.find("SetLootRollSteer(") != std::string::npos,
           "the adapter steers the roster's roll votes");
    Expect(module.find("OverseerDecisions::LootCouncilHeuristic(") != std::string::npos,
           "the heuristic is the council's fallback");
    Expect(module.find("overseer_loot_council") != std::string::npos,
           "the council's questions and answers go through its table");
    Expect(module.find("StoreNewItem(dest, drop.itemid, true, drop.randomPropertyId") !=
               std::string::npos,
           "the master looter hands a drop over the way the core's own give does");
    Expect(module.find("void AnswerOpenRolls(std::vector<GearMember> const& members)") !=
               std::string::npos,
           "the open-roll reaction is kept");
    Expect(module.find("case OverseerDecisions::LootCouncilVote::Disenchant: vote = DISENCHANT;") !=
               std::string::npos,
           "the steer casts the council's Disenchant as the core's DISENCHANT vote");
    Expect(module.find("OverseerCompat::LootRollDisenchant(sPlayerbotAIConfig)") !=
               std::string::npos,
           "the realm's AiPlayerbot.LootRollDisenchant gates the Disenchant vote");
    Expect(module.find("SKILL_ENCHANTING") != std::string::npos,
           "the voters' live Enchanting decides who can take a drop apart");
    Expect(module.find("OverseerDecisions::CanDisenchantDrop(") != std::string::npos,
           "who can take a drop apart is the decision's arithmetic");
    std::string const compat = Read("src/overseer_core_compat.h");
    Expect(compat.find("requires { config.lootRollDisenchant; }") != std::string::npos,
           "the compat header detects the setting's spelling");
    Expect(module.find("c.rolePiece = OverseerDecisions::GearPieceForRole(") !=
               std::string::npos,
           "each candidate is told whether the drop is a piece for its role");
    Expect(module.find("c.ownArmour = OverseerDecisions::GearOwnArmourClass(") !=
                   std::string::npos &&
               module.find("c.statsFit = OverseerDecisions::GearStatsFitRole(") !=
                   std::string::npos,
           "each candidate is told whether the drop's armour class is its own and "
           "whether the drop's stats fit its role");
    Expect(module.find("candidates.back().tank && p->GetName() == family") != std::string::npos &&
               module.find("candidates.back().mainTank = OverseerDecisions::LootMainTank(") !=
                   std::string::npos,
           "the family's head is its main tank on a roll, and the raid's on master loot");
    Expect(module.find("SELECT name, role, duty FROM overseer_raid_seat") != std::string::npos &&
               module.find("SchemaHasColumns(\"overseer_raid_seat\", \"'duty'\", 1)") !=
                   std::string::npos,
           "a seat's duty is read where the realm has the column, and its role where not");

    std::string const duty =
        Read("data/sql/characters/base/2026_09_24_03_overseer_raid_seat_duty.sql");
    Expect(duty.find("ADD COLUMN `duty` VARCHAR(16) NOT NULL DEFAULT ''") != std::string::npos &&
               duty.find("INFORMATION_SCHEMA.COLUMNS") != std::string::npos,
           "the duty column is added once, and a second apply is a no-op");

    std::string const patch =
        Read("patches/mod-playerbots/0015-a-module-can-steer-a-loot-roll-vote.patch");
    Expect(patch.find("SetLootRollSteer") != std::string::npos,
           "the playerbots patch exposes the steer");
    Expect(patch.find("NOT_EMITED_YET") != std::string::npos,
           "the steer can hold a vote");

    std::string const sql =
        Read("data/sql/characters/base/2026_09_24_01_overseer_loot_council.sql");
    Expect(sql.find("CREATE TABLE IF NOT EXISTS `overseer_loot_council`") != std::string::npos,
           "the council table is created");
    Expect(sql.find("`council_key`") != std::string::npos, "rows are keyed by drop");
}

}  // namespace

int main()
{
    RulesForAPartyAndARaid();
    OnlyGearGoesToTheCouncil();
    FamilyFirstThenTheBiggestUpgrade();
    TheUpgradeIsRelativeNotRaw();
    ACertainUpgradeBeatsAnUnsettledOne();
    NobodyItUpgrades();
    TiesAreSettledByName();
    WhatMakesARolePiece();
    ArmourClassAndStatFit();
    TheClassificationFeedsFirstCall();
    TheHeuristicRespectsArmourFit();
    TheMainTankIsGearedFirst();
    TanksThenHealersThenTheRest();
    VotesOnAnOpenRoll();
    AnUnwantedDropGoesToTheEnchanter();
    WhoCanDisenchantADrop();
    MandatedBindOnPickupRollsAreExclusive();
    AMandateEndsOnlyInTheNamedHands();
    SeatRoles();
    KeysAndJson();
    TheAdapterSeams();
    if (failures)
    {
        std::fprintf(stderr, "%d loot council check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_loot_council: ok\n");
    return 0;
}
