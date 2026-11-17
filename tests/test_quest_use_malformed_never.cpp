/*
 * kind='quest' use rows: a row the parser refuses as malformed is never worth
 * asking again, whichever verb it names (#1593). The main-hand verb's
 * malformed literal was left out of the retry table and read `later`.
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;
namespace R = OverseerDecisions::QuestUseRefusal;

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

void AMalformedMainHandRowIsNeverRetried()
{
    Expect(QuestUseRefusalRetry(R::MalformedMainHandSpell) == TownRetry::Never,
           "malformed cast-spell-on-mainhand: never");
}

// One malformed row per verb, through the parser, so the retry table is
// checked against what the parser really says and not a list kept by hand.
void EveryVerbsMalformedRowIsNeverRetried()
{
    char const* const rows[] = {
        "use-item-on",
        "use-item-on creature:0 item:5",
        "use-gameobject",
        "use-gameobject 0",
        "use-item-here",
        "use-item-here item:0",
        "use-spell-on",
        "use-spell-on creature:5 spell:0",
        "cast-spell-on-mainhand",
        "cast-spell-on-mainhand spell:0",
        "cast-spell-on-mainhand bogus",
    };
    for (char const* row : rows)
    {
        Expect(IsQuestUseRow(row), row);
        QuestUseRequest const parsed = ParseQuestUseRequest(row);
        std::string const error = parsed.error;
        if (error.empty())
        {
            std::fprintf(stderr, "FAIL: '%s' parsed, wanted a refusal\n", row);
            ++failures;
            continue;
        }
        if (QuestUseRefusalRetry(error) != TownRetry::Never)
        {
            std::fprintf(stderr, "FAIL: '%s' refused '%s' with retry '%s', wanted never\n", row,
                         error.c_str(), TownRetryWord(QuestUseRefusalRetry(error)));
            ++failures;
        }
    }
}

// The walls that fall on their own stay worth asking again.
void TheMainHandWallsThatPassStayLater()
{
    Expect(QuestUseRefusalRetry(R::MainHandSpellOnCooldown) == TownRetry::Later,
           "main-hand cooldown: later");
    Expect(QuestUseRefusalRetry(R::MainHandFocusMissing) == TownRetry::Later,
           "no Runeforge in range: later");
}

} // namespace

int main()
{
    AMalformedMainHandRowIsNeverRetried();
    EveryVerbsMalformedRowIsNeverRetried();
    TheMainHandWallsThatPassStayLater();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
