/*
 * Source contract for kind='disenchant' and DriveDisenchant (#444).
 *
 * The decisions are proved in test_drive_disenchant.cpp. What only the
 * migration and the adapter can get wrong is pinned here, read as text from
 * the repo root:
 *
 *  - THE MIGRATION KEEPS EVERY VERB. ALTER ... MODIFY replaces the whole ENUM,
 *    so the disenchant migration must list every value any earlier
 *    overseer_command migration listed. The earlier lists are read off the
 *    files themselves, not copied here: a first draft of this migration was
 *    checked against a hand-written list of twenty kinds and silently dropped
 *    'quest' and 'naturalize', which landed after that count was written.
 *  - THE ROW IS ROUTED AND DRIVEN: kind == "disenchant" reaches DoDisenchant,
 *    and the poll calls DriveDisenchant.
 *  - THE BAGS DECIDE, NOT THE CAST: the cast helper never ends a row, and the
 *    read-back counts the reagents with GetItemCount.
 *  - THE ERRAND STANDS on a step DisenchantErrandStands keeps.
 *  - NO BLOCKING READ, NO FREE TEXT IN SQL: the disenchant section reads
 *    nothing from the database, and its one UPDATE takes its values from the
 *    closed sets in OverseerDecisions (escaped too, as test_sql_text_escaped
 *    asks of every detail).
 *
 * Compiled against src/overseer_decisions.cpp.
 */

#include "overseer_decisions.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(char const* label, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", label);
    ++failures;
}

std::string Read(std::string const& path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string Between(std::string const& hay, std::string const& from, std::string const& to)
{
    std::size_t const a = hay.find(from);
    if (a == std::string::npos)
        return std::string();
    std::size_t const b = hay.find(to, a + from.size());
    if (b == std::string::npos)
        return std::string();
    return hay.substr(a, b - a);
}

// The values of every `kind` ENUM an ALTER TABLE `overseer_command` in `sql`
// lists, in order of appearance.
std::vector<std::vector<std::string>> CommandKindLists(std::string const& sql)
{
    std::vector<std::vector<std::string>> lists;
    std::string const enumOpen = "MODIFY COLUMN `kind` ENUM(";
    for (std::size_t at = sql.find(enumOpen); at != std::string::npos;
         at = sql.find(enumOpen, at + 1))
    {
        std::size_t const table = sql.rfind("ALTER TABLE `", at);
        if (table == std::string::npos ||
            sql.compare(table, 30, "ALTER TABLE `overseer_command`") != 0)
            continue;
        std::size_t const close = sql.find(')', at);
        std::string const body = sql.substr(at + enumOpen.size(), close - at - enumOpen.size());
        std::vector<std::string> values;
        for (std::size_t q = body.find('\''); q != std::string::npos;
             q = body.find('\'', body.find('\'', q + 1) + 1))
            values.push_back(body.substr(q + 1, body.find('\'', q + 1) - q - 1));
        lists.push_back(values);
    }
    return lists;
}

void MigrationKeepsEveryCommandKind()
{
    namespace fs = std::filesystem;
    std::string const dir = "data/sql/characters/base";
    std::string const mine = "2026_10_26_00_overseer_disenchant.sql";
    std::vector<std::string> names;
    for (fs::directory_entry const& entry : fs::directory_iterator(dir))
        names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());

    std::set<std::string> earlier;
    unsigned earlierLists = 0;
    for (std::string const& name : names)
    {
        if (name >= mine)
            break;
        for (std::vector<std::string> const& list : CommandKindLists(Read(dir + "/" + name)))
        {
            ++earlierLists;
            earlier.insert(list.begin(), list.end());
        }
    }
    Check("the earlier command-kind migrations are found", earlierLists >= 10);
    Check("the earlier lists include the newest verbs, quest and naturalize",
          earlier.count("quest") && earlier.count("naturalize"));

    std::vector<std::vector<std::string>> const ours = CommandKindLists(Read(dir + "/" + mine));
    Check("the disenchant migration has exactly one command-kind ENUM", ours.size() == 1);
    if (ours.size() != 1)
        return;
    std::set<std::string> const now(ours[0].begin(), ours[0].end());
    for (std::string const& kind : earlier)
        if (!now.count(kind))
        {
            std::printf("FAIL the disenchant migration drops kind '%s'\n", kind.c_str());
            ++failures;
        }
    Check("the disenchant migration adds disenchant", now.count("disenchant") == 1);
    Check("the disenchant migration adds nothing else", now.size() == earlier.size() + 1);
    Check("the default stays bot", Has(Read(dir + "/" + mine), "NOT NULL DEFAULT 'bot'"));
}

void TheRowIsRoutedAndDriven(std::string const& src)
{
    Check("kind='disenchant' reaches DoDisenchant",
          Has(src, "else if (kind == \"disenchant\")\n"
                   "                detail = DoDisenchant(player, command, status, rowResult, "
                   "_pendingDisenchants, id);"));
    Check("the poll drives the standing disenchants", Has(src, "        DriveDisenchant(sincePollMs);"));
    Check("a disenchant row is its own handler's, never a shared chat trigger",
          !OverseerDecisions::KindSharesChatTrigger("disenchant"));
    Check("the loot hook notes a disenchant window",
          Has(src, "loot->loot_type == LOOT_DISENCHANTING") &&
              Has(src, "OverseerWorldScript::NoteDisenchantLoot(player, lootGuid, *loot);"));
}

void TheBagsDecideAndTheErrandStands(std::string const& section)
{
    std::string const cast = Between(section, "static void CastDisenchant(", "\n    }\n");
    Check("the cast helper is found", !cast.empty());
    Check("the cast result never ends a row",
          !Has(cast, "FinishDisenchant") && !Has(cast, "DisenchantStep::Done"));
    Check("the cast goes out at an item target",
          Has(cast, "targets.SetItemTarget(item);") &&
              Has(cast, "bot->CastSpell(targets, info, nullptr, TRIGGERED_NONE)"));

    std::string const read = Between(section, "static bool ReadDisenchantReagents(", "\n    }\n");
    Check("the reagents are counted in the bags after the cast",
          Has(read, "r.now = bot->GetItemCount(entry);") &&
              Has(read, "OverseerDecisions::DisenchantReagentSeen(reagents)"));

    std::string const note = Between(section, "static void NoteDisenchantLoot(", "\n    }\n");
    Check("the hook takes nothing from the window",
          !note.empty() && !Has(note, "HandleAutostoreLootItemOpcode") &&
              !Has(note, "HandleLootReleaseOpcode"));

    std::string const drive = Between(section, "void DriveDisenchant(", "\n    }\n");
    Check("a step that stands keeps the errand for the next poll",
          Has(drive, "if (OverseerDecisions::DisenchantErrandStands(step))\n"
                     "                stillRunning.push_back(check);"));
    Check("only a step that does not stand ends the row",
          Has(drive, "            else\n                FinishDisenchant(check, step);"));
}

void NoBlockingReadAndNoFreeTextInSql(std::string const& section)
{
    Check("the disenchant section is found", !section.empty());
    Check("the disenchant section makes no blocking read", !Has(section, "CharacterDatabase.Query"));
    std::string const finish = Between(section, "static void FinishDisenchant(", "\n    }\n");
    Check("the row's end is enqueued", Has(finish, "CharacterDatabase.Execute("));
    Check("the row's end takes its values from the closed sets",
          Has(finish, "OverseerDecisions::DisenchantRowStatus(step),") &&
              Has(finish, "Esc(OverseerDecisions::DisenchantRowDetail(step)),") &&
              Has(finish, "EscLong(OverseerDecisions::DisenchantResultJson(check.ev, step)), "
                          "check.id, g_runToken);"));
    std::size_t calls = 0;
    for (std::size_t at = section.find("Database.Execute("); at != std::string::npos;
         at = section.find("Database.Execute(", at + 1))
        ++calls;
    Check("the row's end is the section's only database write", calls == 1);
    Check("the section reads nothing from the database, async or not",
          !Has(section, "Database.Query") && !Has(section, "Database.AsyncQuery"));
}

}  // namespace

int main()
{
    MigrationKeepsEveryCommandKind();
    std::string const src = Read("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is read", !src.empty());
    std::string const section =
        Between(src, "// ---------------------------------------------------------- disenchant --",
                "// -------------------------------------------------------------- hearth --");
    TheRowIsRoutedAndDriven(src);
    TheBagsDecideAndTheErrandStands(section);
    NoBlockingReadAndNoFreeTextInSql(section);
    if (failures)
        return 1;
    std::puts("ok test_disenchant_source");
    return 0;
}
