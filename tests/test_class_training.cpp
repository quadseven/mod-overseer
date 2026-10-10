/*
 * A guild bot's class spells, bought at its class trainer (wow-overseer
 * member-gold fix).
 *
 * On the dev realm on 2026-10-10, 134 of the 142 members of the two family
 * guilds had class spells waiting at a trainer: 1,569 in all, a median of ten
 * each. The random guild bots run the new rpg strategy, which never visits a
 * trainer, and `walk-to-trainer` covered trades and talent resets only. This
 * file pins the third form, `walk-to-trainer class`, the order a purse buys
 * spells in, and the adapter's use of the core's own Trainer::TeachSpell.
 *
 * The last part reads src/mod_overseer.cpp (run from the repo root).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ClassSpellOffer;
using OverseerDecisions::ClassSpellsToBuy;
using OverseerDecisions::ErrandWalkRefusalRetryable;
using OverseerDecisions::JudgeTrainerVisit;
using OverseerDecisions::ParseTrainerWalkRequest;
using OverseerDecisions::TrainerVisitFacts;
using OverseerDecisions::TrainerVisitOutcome;
using OverseerDecisions::TrainerWalkRequest;

namespace E = OverseerDecisions::ErrandWalkRefusal;

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

bool Same(char const* a, char const* b)
{
    return std::string(a) == b;
}

void TheGrammar()
{
    TrainerWalkRequest const plain = ParseTrainerWalkRequest("walk-to-trainer class");
    Check("class parses", Same(plain.error, ""));
    Check("as a class walk", plain.classSpells);
    Check("with no trade, tree or list",
          plain.skill == 0 && plain.talentTab == -1 && plain.learn.empty());
    Check("at the errand cap", plain.maxYards == OverseerDecisions::ERRAND_WALK_MAX_YARDS);

    TrainerWalkRequest const far = ParseTrainerWalkRequest("walk-to-trainer class max:20000");
    Check("class with a cap parses", Same(far.error, "") && far.classSpells);
    Check("and keeps the cap", far.maxYards == 20000.f);
    TrainerWalkRequest const capFirst = ParseTrainerWalkRequest("walk-to-trainer max:600 class");
    Check("in any order", Same(capFirst.error, "") && capFirst.classSpells);

    Check("a trade walk is not a class walk",
          !ParseTrainerWalkRequest("walk-to-trainer skill:197").classSpells);
    Check("nor a talents walk",
          !ParseTrainerWalkRequest("walk-to-trainer talents:1").classSpells);

    char const* const bad[] = {
        "walk-to-trainer class class",
        "walk-to-trainer class skill:197",
        "walk-to-trainer class learn:3908",
        "walk-to-trainer class talents:1",
        "walk-to-trainer class:1",
        "walk-to-trainer classes",
        "walk-to-trainer",
        "walk-to-trainer max:600",
    };
    for (char const* row : bad)
    {
        TrainerWalkRequest const request = ParseTrainerWalkRequest(row);
        if (!Same(request.error, E::MalformedTrainer))
        {
            std::printf("FAIL malformed: %s\n", row);
            ++failures;
        }
        Check("a malformed row is no class walk", !request.classSpells);
    }
}

bool Is(std::vector<uint32_t> const& got, std::vector<uint32_t> const& want)
{
    return got == want;
}

void ThePurse()
{
    // A level 25 mage's list as the trainer offers it, out of order.
    std::vector<ClassSpellOffer> const mage = {
        {10, 20, 2000},   // Blizzard
        {5144, 16, 1500}, // Arcane Missiles rank 2
        {475, 18, 1800},  // Remove Lesser Curse
        {3140, 18, 1800}, // Fireball rank 4
        {543, 20, 2000},  // Fire Ward
    };
    Check("lowest level first, then cheapest, then by id, while the purse lasts",
          Is(ClassSpellsToBuy(mage, 100000), {5144, 475, 3140, 10, 543}));
    Check("a purse of exactly the first spell buys it",
          Is(ClassSpellsToBuy(mage, 1500), {5144}));
    Check("an empty purse buys nothing", ClassSpellsToBuy(mage, 0).empty());
    Check("a purse short of every spell buys nothing", ClassSpellsToBuy(mage, 1499).empty());
    Check("five gold buys the first three of 8,100 copper of spells",
          Is(ClassSpellsToBuy(mage, 5100), {5144, 475, 3140}));

    // A dear spell that does not fit is skipped, and a cheaper one after it is
    // still bought: Summon Felsteed's 10,000 must not stand in front of the
    // 2,000-copper spells at the same level.
    std::vector<ClassSpellOffer> const warlock = {
        {1710, 20, 10000},
        {698, 20, 2000},
        {706, 20, 2000},
        {126, 22, 2500},
    };
    Check("the dear spell is skipped and the cheaper ones still bought",
          Is(ClassSpellsToBuy(warlock, 7000), {698, 706, 126}));
    Check("the cheapest at a level is bought before a dearer one",
          Is(ClassSpellsToBuy(warlock, 100000), {698, 706, 1710, 126}));

    Check("a free spell is bought from an empty purse",
          Is(ClassSpellsToBuy({{99, 4, 0}, {100, 4, 100}}, 0), {99}));
    Check("an offer of spell 0 is never bought", ClassSpellsToBuy({{0, 1, 0}}, 1000).empty());
    Check("nothing offered, nothing bought", ClassSpellsToBuy({}, 100000).empty());
}

void TheVerdict()
{
    // A class visit is judged by the trade visit's own rule: the offered
    // spells are the asked ones, the bought ones are the learned ones.
    TrainerVisitFacts none;
    Check("nothing offered: nothing to learn",
          JudgeTrainerVisit(none) == TrainerVisitOutcome::NothingToLearn);
    TrainerVisitFacts poor;
    poor.asked = 4;
    Check("offered and none bought: taught nothing",
          JudgeTrainerVisit(poor) == TrainerVisitOutcome::TaughtNothing);
    TrainerVisitFacts some = poor;
    some.learned = 2;
    Check("one bought: learned", JudgeTrainerVisit(some) == TrainerVisitOutcome::Learned);
    Check("a purse short at the trainer is worth asking again", ErrandWalkRefusalRetryable(E::TaughtNothing));
    Check("a map with no class trainer to teach it is not",
          !ErrandWalkRefusalRetryable(E::NoClassSpellTrainerOnMap));
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("the row's class word reaches the evidence", "ev.classSpells = req.classSpells;");
    has("only a class trainer with a spell to teach serves a class walk",
        "if (ev.classSpells)\n            return TrainerServesClassOf(entry, who) && ClassTrainerTeaches(trainer, who);");
    has("the arrival buys class spells", "return ClassSpellsAtTheTrainer(bot, ev, reason);");
    has("the purse decides which", "D::ClassSpellsToBuy(offers, uint64(bot->GetMoney()))");
    has("each through the core's own trainer window", "trainer->TeachSpell(npc, bot, spellId);");
    has("a learn spell the core casts is witnessed by the purse",
        "if (bot->HasSpell(spellId) || bot->GetMoney() < purseBefore)");
    has("the price is the walker's, with its reputation discount",
        "bot->GetReputationPriceDiscount(npc)");
    has("the log line names what was bought",
        "\"overseer: class trainer walk {} - '{}' reached '{}' after {}ms and {} \"\n"
        "                             \"leg(s): {} - {} class spell(s) learned of {} offered, {} too \"\n"
        "                             \"dear, money {} -> {}\"");
    has("the result says it was a class walk", "\",\\\"class\\\":true,\\\"offered\\\":\"");
    has("a map with no class trainer says so for a class walk",
        "reason = D::ErrandWalkRefusal::NoClassSpellTrainerOnMap;");
}

}  // namespace

int main()
{
    TheGrammar();
    ThePurse();
    TheVerdict();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_class_training: all passed\n");
    return 0;
}
