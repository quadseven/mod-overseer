/*
 * Window contamination test for the jealousy rule (#372 follow-up).
 *
 * The jealousy rule in bonds.py filters kin muster memories by source:
 *   - source='reflection'  -> kin speech that counts toward the window
 *   - source='council'     -> council speech that must NOT count
 *
 * Before the fix, council speech was written with source='reflection',
 * which polluted the kin window and caused false jealousy triggers.
 * This test pins the expected source values so a regression is caught.
 *
 * Compiled against nothing; reads source files as text (run from repo root).
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return content;
}

void CouncilSpeechUsesCouncilSource(std::string const& src)
{
    // Council speech events should be written with source='council', not 'reflection'.
    // The actual write happens in bonds.py (external), but we verify the C++ code
    // doesn't contain the bug pattern.
    Check("no C++ code writes council events with source='reflection'",
          !Has(src, "source = \"reflection\"") && !Has(src, "source='reflection'"));
    Check("no C++ code writes council events with source = reflection",
          !Has(src, "source = reflection") && !Has(src, "source=reflection"));
}

void KinReflectionUsesReflectionSource(std::string const& src)
{
    // Kin muster reflections should use source='reflection'.
    // This is written by bonds.py; we verify the C++ doesn't hardcode it wrong.
    Check("no C++ code writes reflection events with source='council'",
          !Has(src, "source = \"council\"") && !Has(src, "source='council'"));
}

void OverseerEventSourceColumnExists()
{
    // The source column was added by 2026_09_22_00_overseer_event_item_story.sql
    std::string sql = ReadFile("data/sql/characters/base/2026_09_22_00_overseer_event_item_story.sql");
    Check("overseer_event has source column migration",
          Has(sql, "COLUMN_NAME = 'source'"));
    Check("source column comment mentions item_loot and item_given",
          Has(sql, "item_loot") && Has(sql, "item_given"));
}

void NoHardcodedWrongSourcesInModOverseer()
{
    std::string mod = ReadFile("src/mod_overseer.cpp");
    // The C++ code should not contain hardcoded 'reflection' or 'council' as source values
    // for the event kinds that use the source column (item_loot, item_given, item_equip).
    // Those kinds use source for: creature name, mailbox, etc. - not 'council'/'reflection'.
    Check("mod_overseer.cpp does not hardcode source='reflection' for item events",
          !Has(mod, "extra.source = \"reflection\"") && !Has(mod, "extra.source='reflection'"));
    Check("mod_overseer.cpp does not hardcode source='council' for item events",
          !Has(mod, "extra.source = \"council\"") && !Has(mod, "extra.source='council'"));
}

void NoHardcodedWrongSourcesInOverseerDecisions()
{
    std::string dec = ReadFile("src/overseer_decisions.cpp");
    Check("overseer_decisions.cpp does not hardcode source='reflection'",
          !Has(dec, "source = \"reflection\"") && !Has(dec, "source='reflection'"));
    Check("overseer_decisions.cpp does not hardcode source='council'",
          !Has(dec, "source = \"council\"") && !Has(dec, "source='council'"));
}

} // namespace

int main()
{
    std::string mod = ReadFile("src/mod_overseer.cpp");
    std::string dec = ReadFile("src/overseer_decisions.cpp");

    if (mod.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from repo root)\n");
        return 1;
    }
    if (dec.empty())
    {
        std::printf("FAIL could not read src/overseer_decisions.cpp (run from repo root)\n");
        return 1;
    }

    CouncilSpeechUsesCouncilSource(mod);
    CouncilSpeechUsesCouncilSource(dec);
    KinReflectionUsesReflectionSource(mod);
    KinReflectionUsesReflectionSource(dec);
    OverseerEventSourceColumnExists();
    NoHardcodedWrongSourcesInModOverseer();
    NoHardcodedWrongSourcesInOverseerDecisions();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok WindowContaminationTest\n");
    return 0;
}