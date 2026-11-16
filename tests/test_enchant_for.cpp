/*
 * The trade-window enchant: an enchanter puts an Enchanting recipe it knows on
 * a guild member's worn item, through the core's own trade window, and the
 * item never changes owner.
 *
 * Until this verb the module's enchant craft (EnchantTargetFor) only ever cast
 * on the crafter's OWN items, so a guild's enchanters could not spend their
 * disenchanted materials on anyone else's gear. The core already has the
 * mechanism: an item placed in the "will not be traded" slot is the target of
 * a spell the other party casts with TARGET_FLAG_TRADE_ITEM, the spell is
 * stored on the trade and runs when both accept, and the caster pays the
 * reagents. This test pins the decisions around that:
 *
 *   * the row grammar and what a malformed row is,
 *   * every refusal, in the order it is said, with its retry class,
 *   * the rule that an enchant never replaces a better one or one Enchanting
 *     did not make,
 *   * the resolver's next step,
 *   * the proof line,
 *   * the capability fact the bridge gates on,
 *   * and, from the source, that the executor is routed, casts the way the
 *     rule file allows, and grants nothing.
 *
 * Compiled against src/overseer_decisions.cpp; reads src/mod_overseer.cpp as
 * text (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
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

void Check(std::string const& what, bool ok)
{
    Check(what.c_str(), ok);
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string Slurp(char const* path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Everything is fine: a bracer with no enchant, a recipe known and paid for.
EnchantForFacts Good()
{
    EnchantForFacts f;
    f.enchanterAlive = f.targetAlive = true;
    f.targetOnline = true;
    f.spellEnchantsItem = true;
    f.knowsSpell = true;
    f.hasEnchantingSkill = true;
    f.reagentsInBags = true;
    f.sameGuild = true;
    f.withinTradeRange = true;
    f.itemFound = true;
    f.itemFits = true;
    f.levelOk = true;
    f.ownEnchant = 927;
    f.ownRank = 250;
    return f;
}

void TheGrammar()
{
    Check("an enchant-for row is recognised",
          IsEnchantForRow("enchant-for target:42 slot:8 spell:13939"));
    Check("a give row is not", !IsEnchantForRow("guid:5"));
    Check("an empty row is not", !IsEnchantForRow(""));
    Check("a similar word is not", !IsEnchantForRow("enchant-forever target:1"));

    EnchantForSpec const s = ParseEnchantForSpec("enchant-for target:42 slot:8 spell:13939");
    Check("a good row parses", s.valid);
    Check("the target is the character guid", s.target == 42);
    Check("the slot is the equipment slot", s.slot == 8);
    Check("the spell is the recipe", s.spell == 13939);

    Check("keys may come in any order",
          ParseEnchantForSpec("enchant-for spell:7 slot:0 target:9").valid);
    Check("a slot of zero is the head and is valid",
          ParseEnchantForSpec("enchant-for target:9 slot:0 spell:7").slot == 0);
    Check("a missing target is malformed",
          !ParseEnchantForSpec("enchant-for slot:8 spell:7").valid);
    Check("a missing slot is malformed",
          !ParseEnchantForSpec("enchant-for target:9 spell:7").valid);
    Check("a missing spell is malformed",
          !ParseEnchantForSpec("enchant-for target:9 slot:8").valid);
    Check("a zero target is malformed",
          !ParseEnchantForSpec("enchant-for target:0 slot:8 spell:7").valid);
    Check("a zero spell is malformed",
          !ParseEnchantForSpec("enchant-for target:9 slot:8 spell:0").valid);
    Check("a bag slot is not an equipment slot",
          !ParseEnchantForSpec("enchant-for target:9 slot:19 spell:7").valid);
    Check("a repeated key is malformed",
          !ParseEnchantForSpec("enchant-for target:9 target:3 slot:8 spell:7").valid);
    Check("an unknown key is malformed",
          !ParseEnchantForSpec("enchant-for target:9 slot:8 spell:7 gold:5").valid);
    Check("a non-number is malformed",
          !ParseEnchantForSpec("enchant-for target:x slot:8 spell:7").valid);
    Check("a negative number is malformed",
          !ParseEnchantForSpec("enchant-for target:9 slot:-1 spell:7").valid);
    Check("an overflowing number is malformed",
          !ParseEnchantForSpec("enchant-for target:99999999999 slot:8 spell:7").valid);
    Check("the item is named by slot, never by an item entry",
          !ParseEnchantForSpec("enchant-for target:9 slot:8 spell:7 item:5").valid);
}

void TheGoodCaseGoes()
{
    EnchantForVerdict const v = JudgeEnchantFor(Good());
    Check("everything present goes", v.go);
    Check("a go carries no reason", std::string(v.reason).empty());
}

struct Case
{
    char const* what;
    void (*spoil)(EnchantForFacts&);
    char const* retry;
};

void EveryRefusalIsNamed()
{
    Case const cases[] = {
        {"the same character", [](EnchantForFacts& f) { f.sameCharacter = true; }, "never"},
        {"not an enchant spell", [](EnchantForFacts& f) { f.spellEnchantsItem = false; }, "never"},
        {"recipe not known", [](EnchantForFacts& f) { f.knowsSpell = false; }, "never"},
        {"no Enchanting skill", [](EnchantForFacts& f) { f.hasEnchantingSkill = false; }, "never"},
        {"member offline", [](EnchantForFacts& f) { f.targetOnline = false; }, "later"},
        {"not the same guild", [](EnchantForFacts& f) { f.sameGuild = false; }, "never"},
        {"enchanter dead", [](EnchantForFacts& f) { f.enchanterAlive = false; }, "later"},
        {"member dead", [](EnchantForFacts& f) { f.targetAlive = false; }, "later"},
        {"in flight", [](EnchantForFacts& f) { f.anyInFlight = true; }, "later"},
        {"stunned", [](EnchantForFacts& f) { f.anyStunned = true; }, "later"},
        {"logging out", [](EnchantForFacts& f) { f.anyLoggingOut = true; }, "later"},
        {"already trading", [](EnchantForFacts& f) { f.anyAlreadyTrading = true; }, "later"},
        {"in combat", [](EnchantForFacts& f) { f.anyInCombat = true; }, "later"},
        {"enchanter moving", [](EnchantForFacts& f) { f.enchanterMoving = true; }, "later"},
        {"too far apart", [](EnchantForFacts& f) { f.withinTradeRange = false; }, "later"},
        {"reagents short", [](EnchantForFacts& f) { f.reagentsInBags = false; }, "later"},
        {"nothing in the slot", [](EnchantForFacts& f) { f.itemFound = false; }, "later"},
        {"item does not fit", [](EnchantForFacts& f) { f.itemFits = false; }, "never"},
        {"item level too low", [](EnchantForFacts& f) { f.levelOk = false; }, "never"},
    };
    for (Case const& c : cases)
    {
        EnchantForFacts f = Good();
        c.spoil(f);
        EnchantForVerdict const v = JudgeEnchantFor(f);
        std::string const what = c.what;
        Check(what + " is refused", !v.go);
        Check(what + " names a reason", v.reason && *v.reason);
        Check(what + " is classed " + c.retry, std::string(v.retry) == c.retry);
    }
}

void TheFirstWallIsTheOneSaid()
{
    EnchantForFacts f = Good();
    f.sameCharacter = true;
    f.knowsSpell = false;
    f.targetOnline = false;
    Check("the permanent wall is said before a passing one",
          std::string(JudgeEnchantFor(f).retry) == "never");
    f = Good();
    f.targetOnline = false;
    f.itemFound = false;
    Check("an offline member is said before a missing item",
          Has(JudgeEnchantFor(f).reason, "online"));
}

void NeverAWorseEnchant()
{
    EnchantForFacts f = Good();
    f.currentEnchant = 0;
    Check("an empty slot takes it", JudgeEnchantFor(f).go);

    f = Good();
    f.currentEnchant = 927;
    Check("the same enchant is refused", !JudgeEnchantFor(f).go);
    Check("the same enchant is permanent", std::string(JudgeEnchantFor(f).retry) == "never");
    Check("and says it is already there", Has(JudgeEnchantFor(f).reason, "already"));

    f = Good();
    f.currentEnchant = 1883;
    f.currentRankKnown = true;
    f.currentRank = 270;
    Check("a better Enchanting enchant is never written over", !JudgeEnchantFor(f).go);
    Check("and says so", Has(JudgeEnchantFor(f).reason, "better"));

    f = Good();
    f.currentEnchant = 66;
    f.currentRankKnown = true;
    f.currentRank = 250;
    Check("an equal rung may be written over", JudgeEnchantFor(f).go);

    f = Good();
    f.currentEnchant = 66;
    f.currentRankKnown = true;
    f.currentRank = 100;
    Check("a lower rung may be written over", JudgeEnchantFor(f).go);

    f = Good();
    f.currentEnchant = 1843;
    f.currentRankKnown = false;
    Check("an enchant Enchanting did not make is never written over", !JudgeEnchantFor(f).go);
    Check("and says so", Has(JudgeEnchantFor(f).reason, "not made by Enchanting"));

    f = Good();
    f.ownRank = 0;
    f.currentEnchant = 66;
    f.currentRankKnown = true;
    f.currentRank = 100;
    Check("a recipe of unknown rank replaces nothing", !JudgeEnchantFor(f).go);
}

void TheResolverSteps()
{
    EnchantForProgress p;
    p.enchanterHere = p.targetHere = true;
    p.tradeOpen = true;
    p.pollsSpent = 1;
    p.pollLimit = ENCHANT_FOR_POLLS;

    Check("a cast still preparing waits", NextEnchantForStep(p).step == EnchantForStep::Wait);

    p.spellStored = true;
    Check("a stored spell is accepted", NextEnchantForStep(p).step == EnchantForStep::Accept);

    p.acceptSent = true;
    Check("an accepted trade that is still open waits for the core",
          NextEnchantForStep(p).step == EnchantForStep::Wait);

    p.enchantApplied = true;
    p.tradeOpen = false;
    Check("the enchant read back is the only success",
          NextEnchantForStep(p).step == EnchantForStep::Applied);

    p = EnchantForProgress();
    p.enchanterHere = p.targetHere = true;
    p.tradeOpen = false;
    p.acceptSent = true;
    Check("a trade that closed with no enchant is a failure",
          NextEnchantForStep(p).step == EnchantForStep::Failed);

    p = EnchantForProgress();
    p.enchanterHere = true;
    p.targetHere = false;
    p.tradeOpen = true;
    Check("a member who left is a failure", NextEnchantForStep(p).step == EnchantForStep::Failed);

    p = EnchantForProgress();
    p.enchanterHere = p.targetHere = true;
    p.tradeOpen = true;
    p.pollsSpent = ENCHANT_FOR_POLLS;
    p.pollLimit = ENCHANT_FOR_POLLS;
    Check("a cast that never lands runs out of polls",
          NextEnchantForStep(p).step == EnchantForStep::Failed);
    Check("and says what it waited for", Has(NextEnchantForStep(p).reason, "cast"));

    p = EnchantForProgress();
    p.enchanterHere = p.targetHere = true;
    p.tradeOpen = false;
    p.pollsSpent = 0;
    p.pollLimit = ENCHANT_FOR_POLLS;
    Check("a trade that closed before any accept is a failure",
          NextEnchantForStep(p).step == EnchantForStep::Failed);

    p = EnchantForProgress();
    p.enchanterHere = p.targetHere = true;
    p.tradeOpen = true;
    p.enchantApplied = true;
    Check("a spell that landed with the window still open is read as applied",
          NextEnchantForStep(p).step == EnchantForStep::Applied);
}

void TheProofLine()
{
    EnchantForProof proof;
    proof.enchanter = "Aeda";
    proof.target = "Brom";
    proof.itemEntry = 12345;
    proof.slot = 8;
    proof.enchantId = 927;
    proof.spell = 13939;
    proof.reagents = {{11176, 2}, {11175, 1}};
    std::string const line = EnchantForProofLine(proof);
    Check("the proof names the enchanter", Has(line, "enchanter='Aeda'"));
    Check("the proof names the member", Has(line, "target='Brom'"));
    Check("the proof names the item entry", Has(line, "item=12345"));
    Check("the proof names the slot", Has(line, "slot=8"));
    Check("the proof names the enchant id", Has(line, "enchant=927"));
    Check("the proof names the recipe", Has(line, "spell=13939"));
    Check("the proof lists each reagent and the count paid",
          Has(line, "reagents=11176x2,11175x1"));
    Check("the proof starts with a stable prefix",
          line.rfind("overseer: enchant-for proof:", 0) == 0);

    proof.reagents.clear();
    Check("a recipe with no reagents says none", Has(EnchantForProofLine(proof), "reagents=none"));
}

void TheCapability()
{
    bool found = false;
    for (BuildFact const& fact : BuildReport("", {}))
        if (fact.name == "enchant_for")
        {
            found = true;
            Check("the capability fact is compiled", fact.source == SOURCE_COMPILED);
            Check("the capability fact says trade", fact.value == "trade");
        }
    Check("the build report carries enchant_for", found);
}

void TheSource()
{
    std::string const src = Slurp("src/mod_overseer.cpp");
    Check("the source is readable", !src.empty());
    Check("the row is routed on the first word of a trade row",
          Has(src, "kind == \"trade\" && OverseerDecisions::IsEnchantForRow(command)"));
    Check("the executor exists", Has(src, "DoEnchantFor("));
    Check("the resolver runs from the poll", Has(src, "ResolveEnchantForChecks(sincePollMs);"));
    Check("the cast targets the trade slot", Has(src, "SetTradeItemTarget(enchanter)"));
    Check("the member places the item in the will-not-be-traded slot",
          Has(src, "SetItem(TRADE_SLOT_NONTRADED"));
    Check("the cast carries the natural-only marker",
          Has(src, "natural-only: a recipe the enchanter knows"));
    Check("the cast is untriggered so the core takes the reagents",
          Has(src, "enchanter->CastSpell(targets, info, nullptr, TRIGGERED_NONE)"));
    Check("the proof line is logged", Has(src, "EnchantForProofLine("));

    // Grants nothing: no item is created, no spell or skill is set, no money
    // moves, in the executor's body.
    std::size_t const a = src.find("static char const* DoEnchantFor(");
    std::size_t const b = src.find("void ResolveEnchantForChecks(uint32 elapsedMs)");
    Check("the executor and resolver are found", a != std::string::npos && b != std::string::npos);
    if (a != std::string::npos && b != std::string::npos && b > a)
    {
        std::string const body = src.substr(a, b - a);
        Check("no item is created", !Has(body, "StoreNewItem") && !Has(body, "AddItem"));
        Check("no spell is learned", !Has(body, "learnSpell") && !Has(body, "LearnSpell"));
        Check("no skill is set", !Has(body, "SetSkill"));
        Check("no money moves", !Has(body, "ModifyMoney"));
        Check("no enchant is written directly",
              !Has(body, "ApplyEnchantment") && !Has(body, "SetEnchantment"));
    }

    std::string const header = Slurp("src/overseer_decisions.h");
    Check("the build fact is named in the header", Has(header, "ENCHANT_FOR_CAPABILITY"));
}

}  // namespace

int main()
{
    TheGrammar();
    TheGoodCaseGoes();
    EveryRefusalIsNamed();
    TheFirstWallIsTheOneSaid();
    NeverAWorseEnchant();
    TheResolverSteps();
    TheProofLine();
    TheCapability();
    TheSource();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
