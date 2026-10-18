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
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::ClassSpellOffer;
using OverseerDecisions::CLASS_LEARN_MAX_PASSES;
using OverseerDecisions::ClassSpellsToBuy;
using OverseerDecisions::ClassSpellsToBuyThisPass;
using OverseerDecisions::ClassWalkReadsAgain;
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

// ONE VISIT, EVERY RANK (wow-overseer#792). A model of the core's trainer
// window: a line is offered once the member's level reaches it and the rank it
// needs (ReqAbility1) is known. The rows are a mage's Frostbolt chain off the
// acore_world trainer_spell table (116 at 4, 205 at 8, 837 at 14) and Arcane
// Intellect (1459 at 1), with their MoneyCost in copper.
struct TrainerLine
{
    uint32_t spell;
    uint32_t needs;
    uint32_t level;
    uint32_t cost;
};

std::vector<ClassSpellOffer> Offered(std::vector<TrainerLine> const& lines,
                                     std::vector<uint32_t> const& known, uint32_t level)
{
    std::vector<ClassSpellOffer> out;
    for (TrainerLine const& line : lines)
    {
        bool const have = std::find(known.begin(), known.end(), line.spell) != known.end();
        bool const ready = !line.needs ||
                           std::find(known.begin(), known.end(), line.needs) != known.end();
        if (!have && ready && line.level <= level)
            out.push_back({line.spell, line.level, line.cost});
    }
    return out;
}

// The arrival's loop as the adapter runs it, with `refuse` the one spell the
// core will not teach (a learn hook said no): what the visit learned.
std::vector<uint32_t> Visit(std::vector<TrainerLine> const& lines, uint32_t level, uint64_t money,
                            uint32_t refuse, uint32_t* passes)
{
    std::vector<uint32_t> known, tried;
    for (uint32_t pass = 0;; ++pass)
    {
        *passes = pass + 1;
        std::vector<uint32_t> const buy =
            ClassSpellsToBuyThisPass(Offered(lines, known, level), money, tried);
        uint32_t bought = 0;
        for (uint32_t spell : buy)
        {
            tried.push_back(spell);
            if (spell == refuse)
                continue;
            for (TrainerLine const& line : lines)
                if (line.spell == spell)
                    money -= line.cost;
            known.push_back(spell);
            ++bought;
        }
        if (!ClassWalkReadsAgain(pass, bought))
            break;
    }
    return known;
}

void OneVisitEveryRank()
{
    std::vector<TrainerLine> const mage = {
        {1459, 0, 1, 10}, {116, 0, 4, 100}, {205, 116, 8, 200}, {837, 205, 14, 600}};

    // The door list alone holds first ranks only: what one pass buys.
    std::vector<uint32_t> const door = ClassSpellsToBuy(Offered(mage, {}, 14), 100000);
    Check("the door list offers no second rank", door.size() == 2);

    uint32_t passes = 0;
    std::vector<uint32_t> const learned = Visit(mage, 14, 100000, 0, &passes);
    Check("a level 14 mage far behind learns all four in one visit", learned.size() == 4);
    Check("rank 3 of Frostbolt is among them",
          std::find(learned.begin(), learned.end(), 837u) != learned.end());
    Check("the visit stops on the pass that bought nothing", passes == 4);

    // The purse still decides: 310 copper buys Arcane Intellect, rank 1 and
    // rank 2 and stops short of rank 3's 600.
    std::vector<uint32_t> const short_ = Visit(mage, 14, 310, 0, &passes);
    Check("a short purse buys what it covers across passes", short_.size() == 3);
    Check("and not the rank it cannot pay for",
          std::find(short_.begin(), short_.end(), 837u) == short_.end());

    // A spell the core refused is not asked for again in the same visit, and
    // the chain behind it is not offered.
    std::vector<uint32_t> const refused = Visit(mage, 14, 100000, 116, &passes);
    Check("a refused rank is not retried and its chain waits", refused.size() == 1);
    Check("the refusal ends the visit once nothing else is bought", passes == 2);

    // The pass ceiling holds on a chain longer than it: a rank per pass, every
    // pass buying, and the visit still ends at CLASS_LEARN_MAX_PASSES.
    std::vector<TrainerLine> chain;
    for (uint32_t rank = 0; rank < CLASS_LEARN_MAX_PASSES + 5; ++rank)
        chain.push_back({9000 + rank, rank ? 9000 + rank - 1 : 0, 1, 1});
    std::vector<uint32_t> const ceiling = Visit(chain, 60, 100000, 0, &passes);
    Check("a chain longer than the ceiling stops at the ceiling",
          passes == CLASS_LEARN_MAX_PASSES && ceiling.size() == CLASS_LEARN_MAX_PASSES);

    Check("a pass that bought nothing ends the visit", !ClassWalkReadsAgain(0, 0));
    Check("a pass that bought reads the list again", ClassWalkReadsAgain(0, 1));
    Check("never past the pass ceiling", !ClassWalkReadsAgain(CLASS_LEARN_MAX_PASSES - 1, 5));
    std::vector<uint32_t> const tried = {116};
    std::vector<uint32_t> const again =
        ClassSpellsToBuyThisPass({{116, 4, 100}, {1459, 1, 10}}, 1000, tried);
    Check("an offer tried earlier in the visit is skipped",
          again.size() == 1 && again[0] == 1459u);
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
    has("the purse decides which, pass by pass",
        "D::ClassSpellsToBuyThisPass(offers, uint64(bot->GetMoney()), tried)");
    has("the list is read again after a pass that bought",
        "if (!D::ClassWalkReadsAgain(pass, boughtThisPass))");
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
    OneVisitEveryRank();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_class_training: all passed\n");
    return 0;
}
