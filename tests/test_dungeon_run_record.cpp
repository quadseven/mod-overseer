/*
 * What a dungeon run leaves behind for its post-run summary (mod-overseer#971).
 *
 * A run already has a row (overseer_dungeon_run: leader, members, map, started,
 * ended, outcome). What it did not have was anything to put in a summary:
 * item_loot covers rare and wearable items only, a boss dying looked like any
 * creature dying, achievements were never written, and there was no damage
 * figure at all. This file pins the pure half of the four additions:
 *
 *   - loot_won: every item a roster character wins inside a dungeon, of any
 *     quality, by loot or roll, and found from the store note even when
 *     another script has already destroyed the item (a grey);
 *   - boss_kill: a kill the core's own encounter list credits, or a creature
 *     flagged as a dungeon boss, never an ordinary creature;
 *   - the per-run damage book, one line per character per instance copy,
 *     bounded, and the upsert it flushes as;
 *   - ONE vocabulary of highlighted kinds, which the summary view in
 *     data/sql/characters/base/2026_10_19_00_overseer_dungeon_run_summary.sql
 *     must list exactly, so the post-run screen and the live recording
 *     timeline cannot highlight different events.
 *
 * Compiled against src/overseer_decisions.cpp; reads the migration as text
 * (run from the repo root, as CI does).
 */

#include "overseer_decisions.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::BossKillBasis;
using OverseerDecisions::LootStoreNote;
using OverseerDecisions::LootedItem;
using OverseerDecisions::MeterKind;
using OverseerDecisions::RunDamageBook;

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

void CheckEq(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got  [%s]\n  want [%s]\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckU(char const* what, std::uint64_t got, std::uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, want %llu\n", what, static_cast<unsigned long long>(got),
                static_cast<unsigned long long>(want));
    ++failures;
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

constexpr std::uint64_t UGGA = 0x11ULL;
constexpr std::uint64_t GROG = 0x12ULL;

// ------------------------------------------------------------- loot_won --

void AGreyWonIsStillNamedByTheNote()
{
    // The grey another script destroys leaves no story guid (#572), but the
    // run's loot table still has to list it: the note keeps its entry.
    LootStoreNote note;
    OverseerDecisions::NoteStoredItem(note, UGGA, 9001, 1, false, 2589);
    LootedItem const got = OverseerDecisions::TakeLootedItemNote(note, UGGA, 1);
    CheckU("a grey has no story guid", got.storyGuid, 0);
    CheckU("a grey still names its entry", got.entry, 2589);
    CheckU("a grey still names its guid", got.guid, 9001);
}

void ANotableWinKeepsItsStoryGuid()
{
    LootStoreNote note;
    OverseerDecisions::NoteStoredItem(note, UGGA, 4242, 1, true, 5191);
    LootedItem const got = OverseerDecisions::TakeLootedItemNote(note, UGGA, 1);
    CheckU("a notable win keeps its story guid", got.storyGuid, 4242);
    CheckU("a notable win names its entry", got.entry, 5191);
}

void SomebodyElsesStoreNamesNothing()
{
    LootStoreNote note;
    OverseerDecisions::NoteStoredItem(note, GROG, 9001, 1, false, 2589);
    LootedItem const got = OverseerDecisions::TakeLootedItemNote(note, UGGA, 1);
    CheckU("another looter's store names no entry", got.entry, 0);
    LootStoreNote again;
    OverseerDecisions::NoteStoredItem(again, UGGA, 9001, 2, false, 2589);
    CheckU("a different count names no entry",
           OverseerDecisions::TakeLootedItemNote(again, UGGA, 1).entry, 0);
}

void TheOldGuidAnswerIsUnchanged()
{
    // TakeLootedItemGuid is what item_loot and the guild tallies ask; it still
    // answers only for a story candidate.
    LootStoreNote note;
    OverseerDecisions::NoteStoredItem(note, UGGA, 9001, 1, false, 2589);
    CheckU("the story guid answer is still 0 for a grey",
           OverseerDecisions::TakeLootedItemGuid(note, UGGA, 1), 0);
}

void LootWonIsTheRostersInsideADungeon()
{
    Check("a roster win in a dungeon is recorded",
          OverseerDecisions::ShouldRecordLootWon(true, true, 2589));
    Check("a win outside a dungeon is not a run's",
          !OverseerDecisions::ShouldRecordLootWon(true, false, 2589));
    Check("a stranger's win is not recorded",
          !OverseerDecisions::ShouldRecordLootWon(false, true, 2589));
    Check("a win with no entry names nothing",
          !OverseerDecisions::ShouldRecordLootWon(true, true, 0));
}

void LootWonDetailSaysHowAndFromWhat()
{
    CheckEq("a loot from a boss",
            OverseerDecisions::LootWonDetail(OverseerDecisions::ItemVia::Loot, 1, "Edwin VanCleef"),
            "loot x1 from Edwin VanCleef");
    CheckEq("a need roll with no source",
            OverseerDecisions::LootWonDetail(OverseerDecisions::ItemVia::Need, 3, ""),
            "need x3");
    std::string const longSource(400, 'a');
    Check("the detail fits its column",
          OverseerDecisions::LootWonDetail(OverseerDecisions::ItemVia::Greed, 1, longSource)
                  .size() <= 255);
}

// ------------------------------------------------------------ boss_kill --

void ABossIsWhatTheCoreCreditsOrFlags()
{
    Check("an encounter credit is a boss kill",
          OverseerDecisions::ClassifyBossKill(true, true, false) == BossKillBasis::Encounter);
    Check("a dungeon boss flag is a boss kill",
          OverseerDecisions::ClassifyBossKill(true, false, true) == BossKillBasis::BossFlag);
    Check("the encounter credit wins when both say so",
          OverseerDecisions::ClassifyBossKill(true, true, true) == BossKillBasis::Encounter);
    Check("an ordinary creature is not a boss",
          OverseerDecisions::ClassifyBossKill(true, false, false) == BossKillBasis::None);
    Check("nothing outside a dungeon is a run's boss",
          OverseerDecisions::ClassifyBossKill(false, true, true) == BossKillBasis::None);
}

void BossKillDetailNamesTheEncounter()
{
    CheckEq("an encounter kill names it",
            OverseerDecisions::BossKillDetail(BossKillBasis::Encounter, "Mr. Smite"),
            "encounter Mr. Smite");
    CheckEq("an encounter with no name says so",
            OverseerDecisions::BossKillDetail(BossKillBasis::Encounter, ""), "encounter");
    CheckEq("a flagged boss says how it was known",
            OverseerDecisions::BossKillDetail(BossKillBasis::BossFlag, "ignored"),
            "dungeon boss flag");
}

// ---------------------------------------------------------- run damage --

void DamageCountsInsideAnInstanceCopyOnly()
{
    Check("a hit in a dungeon copy counts",
          OverseerDecisions::RunDamageCounts(true, 77));
    Check("a hit outside a dungeon does not",
          !OverseerDecisions::RunDamageCounts(false, 77));
    Check("a dungeon map with no copy id does not",
          !OverseerDecisions::RunDamageCounts(true, 0));
}

void TheBookSumsPerCharacterPerCopy()
{
    RunDamageBook book;
    Check("first hit noted",
          OverseerDecisions::RunDamageNote(book, "Ugga", 36, 77, MeterKind::Damage, 100));
    OverseerDecisions::RunDamageNote(book, "Ugga", 36, 77, MeterKind::Damage, 50);
    OverseerDecisions::RunDamageNote(book, "Ugga", 36, 77, MeterKind::Healing, 30);
    OverseerDecisions::RunDamageNote(book, "Ugga", 36, 77, MeterKind::Taken, 20);
    OverseerDecisions::RunDamageNote(book, "Grog", 36, 77, MeterKind::Damage, 7);
    OverseerDecisions::RunDamageNote(book, "Ugga", 36, 78, MeterKind::Damage, 1);
    CheckU("three lines: two characters, two copies", book.size(), 3);
    OverseerDecisions::RunDamageKey const ugga{"Ugga", 36, 77};
    CheckU("damage is summed", book[ugga].damage, 150);
    CheckU("healing is its own column", book[ugga].healing, 30);
    CheckU("taken is its own column", book[ugga].taken, 20);
}

void AFullBookTakesNoNewLine()
{
    RunDamageBook book;
    for (std::size_t i = 0; i < OverseerDecisions::RUN_DAMAGE_MAX_KEYS; ++i)
        OverseerDecisions::RunDamageNote(book, "Ugga", 36, static_cast<std::uint32_t>(i + 1),
                                         MeterKind::Damage, 1);
    Check("a new line is refused when full",
          !OverseerDecisions::RunDamageNote(book, "Grog", 36, 1, MeterKind::Damage, 1));
    Check("an existing line still sums when full",
          OverseerDecisions::RunDamageNote(book, "Ugga", 36, 1, MeterKind::Damage, 1));
    CheckU("the book stayed bounded", book.size(), OverseerDecisions::RUN_DAMAGE_MAX_KEYS);
}

void TheFlushIsOneAdditiveUpsert()
{
    CheckEq("an empty book writes nothing", OverseerDecisions::RunDamageUpsertSql(RunDamageBook()),
            "");
    RunDamageBook book;
    OverseerDecisions::RunDamageNote(book, "Ugga", 36, 77, MeterKind::Damage, 150);
    OverseerDecisions::RunDamageNote(book, "O'Brien", 36, 77, MeterKind::Healing, 9);
    std::string const sql = OverseerDecisions::RunDamageUpsertSql(book);
    Check("writes the run damage table", Has(sql, "INSERT INTO overseer_run_damage "));
    Check("one row per line", Has(sql, "('Ugga',36,77,150,0,0)"));
    Check("a quote in a name is escaped", Has(sql, "('O\\'Brien',36,77,0,9,0)"));
    Check("a flush adds to what is there rather than replacing it",
          Has(sql, "damage = damage + VALUES(damage)") &&
              Has(sql, "healing = healing + VALUES(healing)") &&
              Has(sql, "taken = taken + VALUES(taken)"));
}

// ------------------------------------------- one vocabulary, two readers --

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The quoted words between the marker and the next closing parenthesis.
std::vector<std::string> QuotedListAfter(std::string const& text, std::string const& marker)
{
    std::vector<std::string> words;
    std::size_t at = text.find(marker);
    if (at == std::string::npos)
        return words;
    std::size_t const end = text.find(')', at);
    at += marker.size();
    while (true)
    {
        std::size_t const open = text.find('\'', at);
        if (open == std::string::npos || open > end)
            break;
        std::size_t const close = text.find('\'', open + 1);
        if (close == std::string::npos || close > end)
            break;
        words.push_back(text.substr(open + 1, close - open - 1));
        at = close + 1;
    }
    return words;
}

void TheHighlightVocabularyCoversTheRun()
{
    for (char const* kind : {"loot_won", "boss_kill", "death", "level_up", "quest_reward",
                             "item_equip", "achievement"})
        Check(kind, OverseerDecisions::IsRunHighlightKind(kind));
    Check("an accepted quest is not a highlight",
          !OverseerDecisions::IsRunHighlightKind("quest_accept"));
    Check("item_loot is the story, loot_won is the run's",
          !OverseerDecisions::IsRunHighlightKind("item_loot"));
}

void TheSummaryViewListsExactlyTheVocabulary()
{
    std::string const sql =
        ReadFile("data/sql/characters/base/2026_10_19_00_overseer_dungeon_run_summary.sql");
    Check("the summary migration is readable from the repo root", !sql.empty());
    Check("it defines the summary view",
          Has(sql, "CREATE OR REPLACE VIEW `overseer_dungeon_run_summary`"));
    Check("it defines the damage table", Has(sql, "CREATE TABLE IF NOT EXISTS `overseer_run_damage`"));
    Check("it defines the damage view",
          Has(sql, "CREATE OR REPLACE VIEW `overseer_dungeon_run_damage`"));

    std::vector<std::string> inView = QuotedListAfter(sql, "e.kind IN (");
    std::vector<std::string> inCode = OverseerDecisions::RunHighlightKinds();
    std::sort(inView.begin(), inView.end());
    std::sort(inCode.begin(), inCode.end());
    Check("the view's kind list is the code's highlight vocabulary", inView == inCode);
    if (inView != inCode)
    {
        std::string v, c;
        for (std::string const& w : inView) v += w + " ";
        for (std::string const& w : inCode) c += w + " ";
        std::printf("  view: %s\n  code: %s\n", v.c_str(), c.c_str());
    }
}

void CompletedRunsKeepTheirPortalKeyword()
{
    std::string const sql =
        ReadFile("data/sql/characters/base/2026_10_21_00_overseer_dungeon_run_portal.sql");
    std::string const source = ReadFile("src/mod_overseer.cpp");
    Check("portal keyword migration adds the run column",
          Has(sql, "ADD COLUMN `portal_keyword`"));
    Check("completed run accounting writes the selected portal keyword",
          Has(source, "SET portal_keyword = '{}'") &&
              Has(source, "portal.keyword") &&
              Has(source, "std::string(outcome) == \"complete\""));
}

}  // namespace

int main()
{
    AGreyWonIsStillNamedByTheNote();
    ANotableWinKeepsItsStoryGuid();
    SomebodyElsesStoreNamesNothing();
    TheOldGuidAnswerIsUnchanged();
    LootWonIsTheRostersInsideADungeon();
    LootWonDetailSaysHowAndFromWhat();
    ABossIsWhatTheCoreCreditsOrFlags();
    BossKillDetailNamesTheEncounter();
    DamageCountsInsideAnInstanceCopyOnly();
    TheBookSumsPerCharacterPerCopy();
    AFullBookTakesNoNewLine();
    TheFlushIsOneAdditiveUpsert();
    TheHighlightVocabularyCoversTheRun();
    TheSummaryViewListsExactlyTheVocabulary();
    CompletedRunsKeepTheirPortalKeyword();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok: dungeon run record\n");
    return 0;
}
